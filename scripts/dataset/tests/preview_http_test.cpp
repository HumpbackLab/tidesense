#include "../preview.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <fstream>
#include <csignal>
#include <sys/stat.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
void require(bool good,const char* reason){if(!good)throw std::runtime_error(reason);}
int connect(unsigned port,const std::string& path) {
 int fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);require(fd>=0,"socket failed");
 timeval timeout{3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 require(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"connect failed");
 std::string request="GET "+path+" HTTP/1.1\r\nHost: localhost\r\n\r\n";
 require(send(fd,request.data(),request.size(),MSG_NOSIGNAL)==ssize_t(request.size()),"request failed");return fd;
}
std::string read(int fd) {
 std::string bytes;char buffer[16384];
 while(true){ssize_t n=recv(fd,buffer,sizeof(buffer),0);if(!n)break;require(n>0,"HTTP read timed out");bytes.append(buffer,size_t(n));}
 ::close(fd);return bytes;
}
}
int main() {
 try {
  signal(SIGPIPE,SIG_IGN);
  tide_cam_config config{};config.mode={640,400,TIDE_CAM_MONO8};
  std::string token(32,'a');PreviewServer preview(config,token);
  require(preview.status().frames==0,"Encoded without a viewer");
  auto submit=[&](uint64_t sequence){LivePair p;p.header.sequence=sequence;p.header.left_ns=sequence*50000000;
   p.left=cv::Mat::zeros(400,640,CV_8UC1);p.right=p.left;preview.submit(p,false);};
  submit(1);
  require(read(connect(preview.status().port,"/wrong/stream.mjpg")).find("404 Not Found")!=std::string::npos,"Accepted invalid token");
  std::string first=read(connect(preview.status().port,"/"+token+"/frame.jpg"));
  require(first.find("200 OK")!=std::string::npos && first.find("Content-Length: 262144")!=std::string::npos,"Snapshot framing failed");
  require(first.find("X-Sequence: 1\r\n")!=std::string::npos,"Snapshot has wrong sequence");
  submit(2);
  std::string second=read(connect(preview.status().port,"/"+token+"/frame.jpg"));
  require(second.find("X-Sequence: 2\r\n")!=std::string::npos,"Snapshot returned stale cached frame");
  int slow=connect(preview.status().port,"/"+token+"/stream.mjpg");
  // A blocked viewer cannot prevent a second viewer from receiving fresh data.
  submit(3);
  std::string third=read(connect(preview.status().port,"/"+token+"/frame.jpg"));
  require(third.find("X-Sequence: 3\r\n")!=std::string::npos,"Slow client blocked another client");
  ::close(slow);
  // Repeated connections must not accumulate clients or hold shutdown open.
  for(unsigned n=4;n<=8;++n){submit(n);auto reply=read(connect(preview.status().port,"/"+token+"/frame.jpg"));
   require(reply.find("X-Sequence: "+std::to_string(n)+"\r\n")!=std::string::npos,"Reconnect returned wrong frame");}
  std::string directory=std::string("/tmp/tide-download-test-XXXXXX");
  require(mkdtemp(&directory[0]),"fixture directory failed");
  std::string id(32,'b'),grant(32,'c');
  auto put=[&](const char* name,const std::string& data){std::ofstream f(directory+"/"+name);f<<data;};
  put("COMPLETE","");put("camera-config.yaml","recording_id: "+id+"\n");
  put("metadata.yaml","complete: true\n");put("imu-source.yaml","raw: true\n");
  put("imu.csv","123,1,2,3,4,5,6\n");put("frames.bin",std::string(1024*1024,'x'));
  bool rejected=false;
  try{preview.prepareDownload(directory,std::string(32,'d'),grant);}catch(const std::exception&){rejected=true;}
  require(rejected,"Authorized a replaced recording");
  preview.prepareDownload(directory,id,grant);
  const auto url="/"+token+"/download/"+grant+"/frames.bin";
  unlink((directory+"/frames.bin").c_str());put("frames.bin","new capture");
  std::string download=read(connect(preview.status().port,url));
  auto body=download.find("\r\n\r\n");
  require(body!=std::string::npos && download.substr(body+4)==std::string(1024*1024,'x'),"Download did not pin original inode");
  require(read(connect(preview.status().port,"/"+token+"/download/"+grant+"/../metadata.yaml")).find("404")!=std::string::npos,"Accepted arbitrary file path");
  preview.releaseDownload(grant);
  require(read(connect(preview.status().port,url)).find("404")!=std::string::npos,"Released authorization remained usable");
  put("INCOMPLETE","");rejected=false;
  try{preview.prepareDownload(directory,id,grant);}catch(const std::exception&){rejected=true;}
  require(rejected,"Authorized an incomplete recording");unlink((directory+"/INCOMPLETE").c_str());
  unlink((directory+"/frames.bin").c_str());require(symlink("/etc/passwd",(directory+"/frames.bin").c_str())==0,"fixture symlink failed");rejected=false;
  try{preview.prepareDownload(directory,id,grant);}catch(const std::exception&){rejected=true;}
  require(rejected,"Authorized a symlink");
  for(const char* name:{"COMPLETE","camera-config.yaml","metadata.yaml","imu-source.yaml","imu.csv","frames.bin"})unlink((directory+"/"+name).c_str());
  rmdir(directory.c_str());
  std::cout<<"HTTP framing, token, freshness, slow reader and reconnect passed\n";return 0;
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
