#include "writer.hpp"
#include "session.hpp"
#include "preview.hpp"
#include <future>
#include <sstream>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>

namespace {
// SDK diagnostics at console level 15 can serialize ISP warnings through the
// UART and make the 200 Hz IMU miss deadlines. Keep logs in the kernel ring,
// limit synchronous console output while capturing, and restore it on exit.
class ConsoleLevel {
 int original_=-1;
 static void set(int level) {
  std::ofstream out("/proc/sys/kernel/printk");out << level << '\n';
 }
 public:
 ConsoleLevel() {
  std::ifstream in("/proc/sys/kernel/printk");
  if(in >> original_) {if(original_>4)set(4);}
 }
 ~ConsoleLevel(){if(original_>=0)set(original_);}
};
std::string quoted(const std::string& s) {
 std::ostringstream out; out << '"';
 for (unsigned char c:s) {
  if (c=='"' || c=='\\') out << '\\' << c;
  else if (c<32) { const char* hex="0123456789abcdef"; out << "\\u00" << hex[c>>4] << hex[c&15]; }
  else out << c;
 }
 return out.str()+'"';
}
void sendAll(int fd,const void* bytes,size_t size) {
 const char* p=static_cast<const char*>(bytes);
 while (size) {
  ssize_t n=::write(fd,p,size);
  if (n<0 && errno==EINTR) continue;
  if (n<=0) throw std::runtime_error("Control connection closed");
  p+=n; size-=size_t(n);
 }
}
int unixConnection(const std::string& path) {
 if (path.size()>=sizeof(sockaddr_un::sun_path)) throw std::runtime_error("Socket path too long");
 int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
 if (fd<0) throw std::runtime_error("Cannot create Unix socket");
 sockaddr_un address{}; address.sun_family=AF_UNIX; std::strcpy(address.sun_path,path.c_str());
 if (connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))) {
  ::close(fd); throw std::runtime_error("Recorder session is not available");
 }
 return fd;
}
struct Shared {
 std::mutex mutex;
 std::string state="warming",error,path,pending,id;
 CameraHeader latest{};
 PreviewServer* preview=nullptr;
 std::deque<ImuSample> history;
 uint64_t pairs=0,first=0,last=0,duration=0,captured=0;
 bool stop=false,exit=false,complete=false;
};
std::string status(const Shared& s,bool ok,const std::string& error) {
 std::ostringstream o; o << std::setprecision(17);
 o << "{\"ok\":" << (ok?"true":"false") << ",\"state\":" << quoted(s.state)
   << ",\"recording_id\":" << quoted(s.id)
   << ",\"error\":" << quoted(error) << ",\"output\":" << quoted(s.path)
   << ",\"pairs\":" << s.pairs << ",\"captured\":" << s.captured
   << ",\"first_ns\":" << s.first << ",\"elapsed\":" << (s.last>s.first?(s.last-s.first)*1e-9:0)
   << ",\"complete\":" << (s.complete?"true":"false");
 if (s.latest.width) {
  const auto& h=s.latest;
  o << ",\"width\":" << h.width << ",\"height\":" << h.height
    << ",\"sequence\":" << h.sequence << ",\"left_ns\":" << h.left_ns << ",\"right_ns\":" << h.right_ns;
 }
 if (s.preview) {
  const auto p=s.preview->status();
  o << ",\"preview_port\":" << p.port << ",\"preview_width\":" << p.width
    << ",\"preview_height\":" << p.height << ",\"preview_clients\":" << p.clients
    << ",\"preview_sequence\":" << p.sequence << ",\"preview_frames\":" << p.frames
    << ",\"preview_bytes\":" << p.bytes << ",\"preview_cpu_ms\":" << p.cpu_ms
    << ",\"preview_error\":" << quoted(p.error);
 }
 return o.str()+"}\n";
}
class Control {
 Shared& shared_; std::string path_; int fd_=-1; std::thread worker_; std::atomic<bool> done_{false};
 void serve(int client) {
  timeval timeout{1,0}; setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
  std::string line;
  while (!done_) {
   pollfd p{client,POLLIN,0}; if (poll(&p,1,200)<=0) continue;
   char c; ssize_t n=recv(client,&c,1,0); if (n<=0) return;
   if (c!='\n') { if (line.size()>=8192) return; line+=c; continue; }
   std::string reply;
   {
    std::lock_guard<std::mutex> lock(shared_.mutex);
    try {
     auto request=YAML::Load(line); line.clear();
     std::string command=request["command"].as<std::string>();
     if (command=="start") {
      if (shared_.state!="preview") throw std::runtime_error("Recorder is busy");
      std::string path=request["output"].as<std::string>();
      if (path.empty() || path[0]!='/' || path.find('\0')!=std::string::npos) throw std::runtime_error("Expected an absolute new output directory");
      uint64_t seconds=request["seconds"]?request["seconds"].as<uint64_t>():0;
      if (seconds>86400) throw std::runtime_error("Duration must be 0..86400 seconds");
      shared_.id=request["recording_id"]?request["recording_id"].as<std::string>():std::to_string(now());
      if (shared_.id.empty() || shared_.id.size()>64) throw std::runtime_error("Invalid recording identifier");
      shared_.pending=path; shared_.path=path; shared_.pairs=shared_.first=shared_.last=0;
      shared_.duration=seconds; shared_.state="preparing"; shared_.error.clear(); shared_.complete=false;
     } else if (command=="stop") {
      if (shared_.state=="preparing") throw std::runtime_error("Wait for recording to start");
      shared_.stop=true;
     } else if (command=="close") {
      shared_.exit=true;
     } else if(command=="download") {
      if(shared_.state!="preview" && shared_.state!="files")throw std::runtime_error("Wait for recording to finish before downloading");
      reply=shared_.preview->prepareDownload(request["output"].as<std::string>(),request["recording_id"].as<std::string>(),request["token"].as<std::string>());
     } else if(command=="download_release") {
      shared_.preview->releaseDownload(request["token"].as<std::string>());
     } else if (command!="status") throw std::runtime_error("Unknown control command");
     if(reply.empty())reply=status(shared_,true,shared_.error);
    } catch (const std::exception& e) { line.clear(); reply=status(shared_,false,e.what()); }
   }
   sendAll(client,reply.data(),reply.size());
  }
 }
 void work() {
  while (!done_) {
   pollfd p{fd_,POLLIN,0}; if (poll(&p,1,200)<=0) continue;
   int client=accept4(fd_,nullptr,nullptr,SOCK_CLOEXEC);
   if (client<0) continue;
   try { serve(client); } catch (const std::exception&) {}
   ::close(client);
  }
 }
 public:
 Control(Shared& s,const std::string& path):shared_(s),path_(path) {
  if (path.size()>=sizeof(sockaddr_un::sun_path)) throw std::runtime_error("Socket path too long");
  struct stat st{};
  if (!lstat(path.c_str(),&st)) {
   if (!S_ISSOCK(st.st_mode) || st.st_uid!=geteuid()) throw std::runtime_error("Control path already exists");
   bool active=false; try { int probe=unixConnection(path); ::close(probe); active=true; } catch (...) {}
   if (active) throw std::runtime_error("Recorder session already running");
   if (unlink(path.c_str())) throw std::runtime_error("Cannot remove stale control socket");
  }
  fd_=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
  sockaddr_un address{}; address.sun_family=AF_UNIX; std::strcpy(address.sun_path,path.c_str());
  if (fd_<0 || bind(fd_,reinterpret_cast<sockaddr*>(&address),sizeof(address)) || chmod(path.c_str(),0600) || listen(fd_,4)) {
   if (fd_>=0) ::close(fd_);
   throw std::runtime_error("Cannot bind recorder control socket");
  }
  worker_=std::thread(&Control::work,this);
 }
 ~Control() { done_=true; if (worker_.joinable()) worker_.join(); ::close(fd_); unlink(path_.c_str()); }
};
}

int datasetRelay(int argc,char** argv) {
 try {
  if (argc!=2) throw std::runtime_error("Usage: tidesense_record --relay SOCKET");
  signal(SIGPIPE,SIG_IGN);
  int fd=unixConnection(argv[1]);
  char bytes[65536];
  while (true) {
   pollfd fds[]={{STDIN_FILENO,POLLIN,0},{fd,POLLIN,0}};
   int result=poll(fds,2,-1); if (result<0 && errno==EINTR) continue;
   if (result<0) break;
   bool ended=false;
   for (int i=0;i<2;++i) if (fds[i].revents&(POLLIN|POLLHUP|POLLERR)) {
    ssize_t n=::read(fds[i].fd,bytes,sizeof(bytes));
    if (n<=0) { ended=true; break; }
    sendAll(i==0?fd:STDOUT_FILENO,bytes,size_t(n));
   }
   if (ended) break;
  }
  ::close(fd); return 0;
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

int datasetSession(int argc,char** argv) {
 try {
  std::string socketPath="/run/tidesense-record.sock",device="auto",httpToken;
  tide_cam_config config; tide_cam_config_init(&config); config.queue=TIDE_CAM_FIFO; config.buffer_count=8;
  for (int i=1;i<argc;i+=2) {
   if (i+1>=argc) throw std::runtime_error("Missing session option value");
   std::string option=argv[i],value=argv[i+1];
   if (option=="--socket") socketPath=value;
   else if (option=="--http-token") httpToken=value;
   else if (option=="--imu") device=value;
   else if (option=="--width") config.mode.width=uint32_t(std::stoul(value));
   else if (option=="--height") config.mode.height=uint32_t(std::stoul(value));
   else throw std::runtime_error("Unknown session option");
  }
  struct sigaction sa{}; sa.sa_handler=interrupt; sigaction(SIGINT,&sa,nullptr); sigaction(SIGTERM,&sa,nullptr); signal(SIGPIPE,SIG_IGN);
  // This is a foreground session owned by its launcher (the SSH channel).
  // A lost backend/SSH connection must not leave an orphan holding the sensors.
  const pid_t parent=getppid();
  if (prctl(PR_SET_PDEATHSIG,SIGTERM)) throw std::runtime_error("Cannot watch recorder session parent");
  if (parent==1 || getppid()!=parent || interrupted) return 0;
  ConsoleLevel console;
  Shared shared;
  TideCamera camera(config); LivePair pair;
  if (!camera.next(pair,interrupted)) return 1;
  PreviewServer preview(config,httpToken);shared.preview=&preview;
  Control control(shared,socketPath);
  LiveImu imu(device);
  std::unique_ptr<Writer> writer;
  std::future<std::string> finishing;
  uint64_t first=0,last=0,previousImu=0,startBoundary=0,sequence=0;
  uint32_t leftRef=0,rightRef=0;
  auto finalize=[&](const std::string& reason) {
   if (!writer) return;
   { std::lock_guard<std::mutex> lock(shared.mutex); shared.state="finalizing"; shared.stop=false; }
   Writer* active=writer.release();
   finishing=std::async(std::launch::async,[active,reason]() {
    std::unique_ptr<Writer> owner(active);
    try { owner->finish(reason.empty(),reason); return std::string(); } catch (const std::exception& e) { return std::string(e.what()); }
   });
  };
  std::string fatal;
  try {
   while (camera.next(pair,interrupted)) {
    auto samples=imu.through(pair.header.left_ns,interrupted); if (interrupted) break;
    bool stop=false,exit=false; std::string pending;
    {
     std::lock_guard<std::mutex> lock(shared.mutex);
     for (const auto& s:samples) shared.history.push_back(s);
     while (shared.history.size()>100) shared.history.pop_front();
     shared.latest=pair.header; ++shared.captured;
     if (shared.state=="warming" && shared.history.size()>=20) shared.state="preview";
     stop=shared.stop; shared.stop=false; exit=shared.exit; pending.swap(shared.pending);
    }
    preview.submit(pair,writer || finishing.valid());
    if (finishing.valid() && finishing.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
     std::string error=finishing.get();
     std::lock_guard<std::mutex> lock(shared.mutex); shared.state="preview"; shared.complete=error.empty(); shared.error=error;
    }
    if (exit) break;
    if (!pending.empty()) {
     try {
      writer.reset(new Writer(pending,64,config));
      auto captureConfig=cameraConfiguration(config);
      { std::lock_guard<std::mutex> lock(shared.mutex); captureConfig["recording_id"]=shared.id; }
      yaml(pending+"/camera-config.yaml",captureConfig); yaml(pending+"/imu-source.yaml",imu.recordingMetadata());
      startBoundary=now(); first=last=previousImu=sequence=0;
      std::lock_guard<std::mutex> lock(shared.mutex); shared.path=pending; shared.pairs=shared.first=shared.last=0;
     } catch (const std::exception& e) {
      writer.reset(); std::lock_guard<std::mutex> lock(shared.mutex); shared.state="preview"; shared.error=e.what();
     }
    }
    if (stop) finalize("");
    if (!writer || pair.header.left_ns<startBoundary) continue;
    const auto h=pair.header;
    if (last && (h.sequence!=sequence+1 || h.left_ns-last>75000000 || uint32_t(h.left_ref-leftRef)!=1 || uint32_t(h.right_ref-rightRef)!=1))
     throw std::runtime_error("Recording camera discontinuity");
    if (!first) {
     std::lock_guard<std::mutex> lock(shared.mutex);
     samples.assign(shared.history.begin(),shared.history.end());
     if (samples.empty() || samples.front().stamp_ns>=h.left_ns) throw std::runtime_error("Missing initial IMU coverage");
    }
    for (const auto& sample:samples) {
     if (previousImu && (sample.stamp_ns<=previousImu || sample.stamp_ns-previousImu>7500000)) throw std::runtime_error("Recording IMU discontinuity");
     previousImu=sample.stamp_ns;
    }
    if (samples.empty() || samples.back().stamp_ns<=h.left_ns) throw std::runtime_error("Missing final IMU coverage");
    if (!first) first=h.left_ns;
    last=h.left_ns; sequence=h.sequence; leftRef=h.left_ref; rightRef=h.right_ref;
    writer->submit(std::move(pair),std::move(samples));
    uint64_t seconds;
    { std::lock_guard<std::mutex> lock(shared.mutex); shared.state="recording"; ++shared.pairs; shared.first=first; shared.last=last; seconds=shared.duration; }
    if (seconds && last-first>=seconds*1000000000ULL) finalize("");
   }
   imu.check();
  } catch (const std::exception& e) { fatal=e.what(); }
  finalize(fatal);
  if (finishing.valid()) {
   std::string result=finishing.get(); if (fatal.empty()) fatal=result;
  }
  imu.stop(); camera.stop();
  if (!fatal.empty()) throw std::runtime_error(fatal);
  return 0;
 } catch (const std::exception& e) { std::cerr << "Session error: " << e.what() << '\n'; return 1; }
}

int datasetFileServer(int argc,char** argv) {
 try {
  std::string socketPath,token;
  for(int i=1;i<argc;i+=2){if(i+1>=argc)throw std::runtime_error("Missing file-server option");
   std::string key=argv[i];if(key=="--socket")socketPath=argv[i+1];else if(key=="--http-token")token=argv[i+1];else throw std::runtime_error("Unknown file-server option");}
  struct sigaction sa{};sa.sa_handler=interrupt;sigaction(SIGINT,&sa,nullptr);sigaction(SIGTERM,&sa,nullptr);signal(SIGPIPE,SIG_IGN);
  pid_t parent=getppid();if(prctl(PR_SET_PDEATHSIG,SIGTERM))throw std::runtime_error("Cannot watch file-server parent");
  if(parent==1 || getppid()!=parent || interrupted)return 0;
  tide_cam_config config{};Shared shared;shared.state="files";
  PreviewServer http(config,token);shared.preview=&http;Control control(shared,socketPath);
  while(!interrupted){{std::lock_guard<std::mutex> guard(shared.mutex);if(shared.exit)break;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}
  return 0;
 } catch(const std::exception& e){std::cerr<<"Session error: "<<e.what()<<'\n';return 1;}
}
