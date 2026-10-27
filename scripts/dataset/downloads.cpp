#include "downloads.hpp"
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
bool hex(const std::string& s){return s.size()==32 && s.find_first_not_of("0123456789abcdef")==std::string::npos;}
struct File {
 int fd=-1;std::string name;off_t size=0;
 ~File(){if(fd>=0)::close(fd);}
};
struct Worker {std::thread thread;std::atomic<bool> done{false};};
std::string small(const File& file) {
 if(file.size>65536)throw std::runtime_error("Recording metadata too large");
 std::string data(size_t(file.size),'\0');
 if(pread(file.fd,&data[0],data.size(),0)!=ssize_t(data.size()))throw std::runtime_error("Cannot read recording metadata");
 return data;
}
void reject(int fd,const char* status) {
 const std::string message=std::string("HTTP/1.1 ")+status+"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
 (void)send(fd,message.data(),message.size(),MSG_NOSIGNAL);
}
}
struct Downloads::Impl {
 std::string prefix;std::atomic<bool>& stopped;std::mutex mutex;
 std::map<std::string,std::vector<std::shared_ptr<File>>> grants;
 std::vector<std::unique_ptr<Worker>> workers;
 Impl(const std::string& p,std::atomic<bool>& s):prefix(p+"/download/"),stopped(s){}
 ~Impl(){for(auto& w:workers)if(w->thread.joinable())w->thread.join();}
 void sendFile(int socket,const File& file) {
  std::string header="HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+std::to_string(file.size)+"\r\n\r\n";
  size_t sent=0;off_t offset=0;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(!stopped && (sent<header.size() || offset<file.size)) {
   pollfd p{socket,POLLOUT,0};int ready=poll(&p,1,200);
   if(ready<0 && errno==EINTR)continue;
   if(ready<0 || (p.revents&(POLLERR|POLLHUP|POLLNVAL)))break;
   if(std::chrono::steady_clock::now()>deadline)break;
   if(!ready)continue;
   ssize_t bytes;
   if(sent<header.size())bytes=send(socket,header.data()+sent,header.size()-sent,MSG_NOSIGNAL);
   else bytes=sendfile(socket,file.fd,&offset,std::min<off_t>(1024*1024,file.size-offset));
   if(bytes>0) {
    if(sent<header.size())sent+=size_t(bytes);
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
   } else if(bytes==0 || (errno!=EAGAIN && errno!=EINTR))break;
  }
 }
};
Downloads::Downloads(const std::string& p,std::atomic<bool>& s):impl_(new Impl(p,s)){}
Downloads::~Downloads()=default;
std::string Downloads::prepare(const std::string& directory,const std::string& recording,const std::string& token) {
 if(directory.empty() || directory[0]!='/' || !hex(recording) || !hex(token))throw std::runtime_error("Invalid download authorization");
 int directoryFd=::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(directoryFd<0)throw std::runtime_error("Recording directory is unavailable");
 std::vector<std::shared_ptr<File>> files;
 try {
  struct stat st{};
  if(fstatat(directoryFd,"COMPLETE",&st,AT_SYMLINK_NOFOLLOW) || !S_ISREG(st.st_mode) ||
     !fstatat(directoryFd,"INCOMPLETE",&st,AT_SYMLINK_NOFOLLOW))throw std::runtime_error("Recording is incomplete");
  for(const char* name:{"metadata.yaml","camera-config.yaml","imu-source.yaml","imu.csv","frames.bin"}) {
   auto f=std::make_shared<File>();f->name=name;
   f->fd=openat(directoryFd,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
   if(f->fd<0 || fstat(f->fd,&st) || !S_ISREG(st.st_mode) || st.st_size<=0)throw std::runtime_error("Recording file is unavailable");
   f->size=st.st_size;files.push_back(f);
  }
  auto meta=YAML::Load(small(*files[0]));auto config=YAML::Load(small(*files[1]));
  if(!meta["complete"].as<bool>())throw std::runtime_error("Recording is incomplete");
  if(config["recording_id"].as<std::string>()!=recording)throw std::runtime_error("Recording has been replaced; refusing a different capture");
  ::close(directoryFd);
 } catch(...){::close(directoryFd);throw;}
 std::lock_guard<std::mutex> lock(impl_->mutex);
 if(impl_->grants.size()>=4 || impl_->grants.count(token))throw std::runtime_error("Too many downloads");
 impl_->grants[token]=files;
 std::string out="{\"ok\":true,\"path\":\""+impl_->prefix+token+"\",\"files\":[";
 bool first=true;for(const auto& file:files){if(!first)out+=',';first=false;out+="{\"name\":\""+file->name+"\",\"size\":"+std::to_string(file->size)+"}";}
 return out+"]}\n";
}
void Downloads::release(const std::string& token){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->grants.erase(token);}
int Downloads::serve(int socket,const std::string& target) {
 if(target.compare(0,impl_->prefix.size(),impl_->prefix))return 0;
 std::string remaining=target.substr(impl_->prefix.size());auto slash=remaining.find('/');
 std::string token=remaining.substr(0,slash),name=slash==std::string::npos?"":remaining.substr(slash+1);
 std::shared_ptr<File> file;
 {std::lock_guard<std::mutex> lock(impl_->mutex);auto grant=impl_->grants.find(token);
  if(grant!=impl_->grants.end())for(auto& f:grant->second)if(f->name==name)file=f;}
 if(!file){reject(socket,"404 Not Found");return -1;}
 for(auto it=impl_->workers.begin();it!=impl_->workers.end();) {
  if((*it)->done){(*it)->thread.join();it=impl_->workers.erase(it);}else ++it;
 }
 if(impl_->workers.size()>=2){reject(socket,"503 Service Unavailable");return -1;}
 auto worker=std::unique_ptr<Worker>(new Worker);Worker* state=worker.get();
 worker->thread=std::thread([this,socket,file,state]{
  try{impl_->sendFile(socket,*file);}catch(...){}
  ::close(socket);state->done=true;
 });
 impl_->workers.push_back(std::move(worker));return 1;
}
