#include "preview.hpp"
#include "preview_encoder.hpp"
#include "downloads.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
uint64_t monotonicNs(clockid_t clock=CLOCK_MONOTONIC) {
 timespec t{};clock_gettime(clock,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;
}
struct Frame {std::vector<uint8_t> jpeg;uint64_t sequence=0,stamp=0;};
struct Client {
 int fd=-1;std::string request,prefix;size_t offset=0;
 bool ready=false,single=false;uint64_t last=0,minimum=0,deadline=0;
 std::shared_ptr<const Frame> sending;
};
}
struct PreviewServer::Impl {
 std::mutex mutex;std::condition_variable changed;
 LivePair latest;bool recording=false;
 Status info;
 std::shared_ptr<const Frame> encoded;
 std::string path;
 int listener=-1;std::atomic<bool> done{false};std::atomic<unsigned> clients{0};
 std::thread encoderThread,httpThread;
 unsigned inputWidth=0,inputHeight=0;
 std::unique_ptr<PreviewEncoder> encoder;
 std::unique_ptr<Downloads> downloads;
 Impl(const tide_cam_config& config,const std::string& token):path("/"+token) {
  if(token.size()!=32 || token.find_first_not_of("0123456789abcdef")!=std::string::npos)
   throw std::runtime_error("Expected 32 hex digits for HTTP preview token");
  inputWidth=config.mode.width;inputHeight=config.mode.height;
  info.width=2*std::min(inputWidth,640u);info.height=inputWidth?((inputHeight*std::min(inputWidth,640u)/inputWidth+1)&~1u):0;
  listener=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
  if(listener<0) throw std::runtime_error("Cannot create preview HTTP socket");
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_ANY);
  socklen_t length=sizeof(address);
  if(bind(listener,reinterpret_cast<sockaddr*>(&address),length) || listen(listener,8) ||
     getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)) {
   ::close(listener);throw std::runtime_error("Cannot bind preview HTTP socket");
  }
  info.port=ntohs(address.sin_port);
  downloads.reset(new Downloads(path,done));
  try {encoderThread=std::thread(&Impl::encode,this);httpThread=std::thread(&Impl::http,this);}
  catch(...) {done=true;changed.notify_all();if(encoderThread.joinable())encoderThread.join();::close(listener);throw;}
 }
 ~Impl() {
  done=true;changed.notify_all();
  if(httpThread.joinable())httpThread.join();
  if(encoderThread.joinable())encoderThread.join();
  ::close(listener);
 }
 void encode() {
  uint64_t previous=0,next=0;
  while(!done) {
   LivePair pair;bool busy;
   {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_for(lock,std::chrono::milliseconds(50),[&]{return done ||
      (clients && !latest.left.empty() && latest.header.sequence!=previous && monotonicNs()>=next);});
    if(done)break;
    if(!clients || latest.left.empty() || latest.header.sequence==previous || monotonicNs()<next)continue;
    pair=latest;busy=recording;
   }
   previous=pair.header.sequence;next=monotonicNs()+(busy?500000000ULL:200000000ULL);
   try {
    uint64_t cpu=monotonicNs(CLOCK_THREAD_CPUTIME_ID);
    if(!encoder)encoder.reset(new PreviewEncoder(inputWidth,inputHeight));
    auto frame=std::make_shared<Frame>();frame->jpeg=encoder->encode(pair);
    frame->sequence=pair.header.sequence;frame->stamp=pair.header.left_ns;
    double elapsed=(monotonicNs(CLOCK_THREAD_CPUTIME_ID)-cpu)*1e-6;
    std::lock_guard<std::mutex> lock(mutex);encoded=std::move(frame);
    info.sequence=encoded->sequence;++info.frames;info.bytes+=encoded->jpeg.size();info.cpu_ms+=elapsed;info.error.clear();
   } catch(const std::exception& e) {
    std::lock_guard<std::mutex> lock(mutex);info.error=e.what();encoded.reset();
   }
  }
 }
 void http() {
  std::deque<Client> peers;
  auto remove=[&](std::deque<Client>::iterator it) {if(it->fd>=0)::close(it->fd);return peers.erase(it);};
  while(!done) {
   // All sockets are nonblocking. A slow viewer retains at most its current
   // JPEG; the next frame is selected from the latest slot only after send.
   std::vector<pollfd> fds{{listener,POLLIN,0}};
   std::shared_ptr<const Frame> frame;
   {std::lock_guard<std::mutex> lock(mutex);frame=encoded;}
   for(auto& c:peers) {
    if(c.ready && !c.sending && c.prefix.empty() && frame && frame->sequence!=c.last && frame->sequence>=c.minimum) {
     c.sending=frame;c.offset=0;c.deadline=monotonicNs()+2000000000ULL;
     c.prefix=(c.single?std::string():"--tideframe\r\n")+std::string("Content-Type: image/jpeg\r\nContent-Length: ")+
      std::to_string(frame->jpeg.size())+"\r\nX-Sequence: "+std::to_string(frame->sequence)+
      "\r\nX-Timestamp-Ns: "+std::to_string(frame->stamp)+"\r\n\r\n";
    }
    short events=c.ready?(c.sending || !c.prefix.empty()?POLLOUT:0):POLLIN;
    fds.push_back({c.fd,events,0});
   }
   int result=poll(fds.data(),fds.size(),30);
   if(result<0 && errno==EINTR)continue;
   if(result<0)break;
   size_t index=1;
   for(auto it=peers.begin();it!=peers.end();) {
    auto& c=*it;short events=fds[index++].revents;bool close=false;
    if(events&(POLLERR|POLLHUP|POLLNVAL))close=true;
    if(!close && !c.ready && events&POLLIN) {
     char input[1024];ssize_t n=recv(c.fd,input,sizeof(input),0);
     if(n<=0)close=n==0 || (errno!=EAGAIN && errno!=EINTR);
     else {
      c.request.append(input,size_t(n));
      if(c.request.size()>4096)close=true;
      else if(c.request.find("\r\n\r\n")!=std::string::npos) {
       const auto end=c.request.find(" HTTP/");
       const std::string target=c.request.substr(4,end==std::string::npos?0:end-4);
       if(!c.request.compare(0,4,"GET ") && end!=std::string::npos) {
        int handled=downloads->serve(c.fd,target);
        if(handled){if(handled>0)c.fd=-1;it=remove(it);continue;}
       }
       c.single=target==path+"/frame.jpg";
       if(c.request.compare(0,4,"GET ") || end==std::string::npos || (!inputWidth || (!c.single && target!=path+"/stream.mjpg"))) {
        const char reply[]="HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        (void)send(c.fd,reply,sizeof(reply)-1,MSG_NOSIGNAL);close=true;
       } else {
        {std::lock_guard<std::mutex> lock(mutex);c.minimum=latest.header.sequence;}
        c.ready=true;c.request.clear();c.deadline=monotonicNs()+3000000000ULL;
        c.prefix="HTTP/1.1 200 OK\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n";
        if(!c.single)c.prefix+="Content-Type: multipart/x-mixed-replace; boundary=tideframe\r\n\r\n";
        changed.notify_one();
       }
      }
     }
    }
    if(!close && c.ready && events&POLLOUT) {
     const size_t total=c.prefix.size()+(c.sending?c.sending->jpeg.size():0);
     const bool header=c.offset<c.prefix.size();
     const uint8_t* bytes=header?reinterpret_cast<const uint8_t*>(c.prefix.data())+c.offset:
      c.sending->jpeg.data()+c.offset-c.prefix.size();
     const size_t remaining=header?c.prefix.size()-c.offset:total-c.offset;
     ssize_t n=send(c.fd,bytes,remaining,MSG_NOSIGNAL);
     if(n>0)c.offset+=size_t(n);
     else if(n==0 || (errno!=EAGAIN && errno!=EINTR))close=true;
     if(c.offset==total) {
      if(c.sending) {
       c.last=c.sending->sequence;c.sending.reset();
       if(c.single)close=true;
       else c.prefix="\r\n";
      } else c.prefix.clear();
      c.offset=0;
     }
    }
    if(!close && monotonicNs()>c.deadline && (!c.ready || c.sending || !c.last))close=true;
    if(close)it=remove(it);else ++it;
   }
   if(fds[0].revents&POLLIN) {
    int fd=accept4(listener,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK);
    if(fd>=0) {
     if(peers.size()>=4)::close(fd);
     else {
      int size=32768;setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&size,sizeof(size));
      Client c;c.fd=fd;c.deadline=monotonicNs()+2000000000ULL;peers.push_back(std::move(c));
     }
    }
   }
   clients=unsigned(std::count_if(peers.begin(),peers.end(),[](const Client& c){return c.ready;}));
  }
  for(auto& c:peers)::close(c.fd);
  clients=0;
 }
};
PreviewServer::PreviewServer(const tide_cam_config& c,const std::string& t):impl_(new Impl(c,t)){}
PreviewServer::~PreviewServer()=default;
void PreviewServer::submit(const LivePair& pair,bool recording) {
 {std::lock_guard<std::mutex> lock(impl_->mutex);impl_->latest=pair;impl_->recording=recording;}
 impl_->changed.notify_one();
}
PreviewServer::Status PreviewServer::status() {
 std::lock_guard<std::mutex> lock(impl_->mutex);auto out=impl_->info;out.clients=impl_->clients.load();return out;
}

std::string PreviewServer::prepareDownload(const std::string& d,const std::string& r,const std::string& t){return impl_->downloads->prepare(d,r,t);}
void PreviewServer::releaseDownload(const std::string& t){impl_->downloads->release(t);}
