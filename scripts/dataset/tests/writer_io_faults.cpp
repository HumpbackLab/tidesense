// Deterministic file I/O blocking/failure, with no production writer hooks.
#include <atomic>
#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>

namespace {
std::atomic<int> mode{0};
std::atomic<bool> blocked{false};
std::atomic<bool> corrupted{false};
pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ready=PTHREAD_COND_INITIALIZER;
bool imageFile(int fd) {
 char path[64],target[4096];
 // Avoid stdio formatting or file access from inside write interposition.
 const char prefix[]="/proc/self/fd/";
 std::memcpy(path,prefix,sizeof(prefix)-1);
 char digits[20]; unsigned n=0,value=unsigned(fd);
 do {digits[n++]=char('0'+value%10);value/=10;} while(value);
 size_t size=sizeof(prefix)-1;
 while(n) path[size++]=digits[--n];
 path[size]=0;
 ssize_t length=readlink(path,target,sizeof(target)-1);
 if(length<=0) return false;
 target[length]=0;
 const char suffix[]="/frames.bin";
 return size_t(length)>=sizeof(suffix)-1 && !std::strcmp(target+length-(sizeof(suffix)-1),suffix);
}
bool gate(int fd) {
 if ((mode!=1 && mode!=2) || !imageFile(fd)) return true;
 pthread_mutex_lock(&mutex);
 blocked=true;
 while(mode==1) pthread_cond_wait(&ready,&mutex);
 bool success=mode!=2;
 pthread_mutex_unlock(&mutex);
 if(!success) errno=ENOSPC;
 return success;
}
}
extern "C" void writer_fault_mode(int value) {
 pthread_mutex_lock(&mutex); mode=value; pthread_cond_broadcast(&ready); pthread_mutex_unlock(&mutex);
}
extern "C" bool writer_fault_blocked() {return blocked;}
extern "C" ssize_t write(int fd,const void* data,size_t size) {
 static auto actual=reinterpret_cast<ssize_t(*)(int,const void*,size_t)>(dlsym(RTLD_NEXT,"write"));
 if (!gate(fd)) return -1;
 if (mode==3 && size>64 && imageFile(fd) && !corrupted.exchange(true)) {
  std::vector<char> copy(static_cast<const char*>(data),static_cast<const char*>(data)+size);
  copy.back()^=1;return actual(fd,copy.data(),copy.size());
 }
 return actual(fd,data,size);
}
extern "C" ssize_t writev(int fd,const iovec* data,int count) {
 static auto actual=reinterpret_cast<ssize_t(*)(int,const iovec*,int)>(dlsym(RTLD_NEXT,"writev"));
 if (!gate(fd)) return -1;
 if (mode==3 && count>0 && data[count-1].iov_len>64 && imageFile(fd) && !corrupted.exchange(true)) {
  std::vector<iovec> parts(data,data+count);
  const char* first=static_cast<const char*>(parts.back().iov_base);
  std::vector<char> copy(first,first+parts.back().iov_len);copy.back()^=1;
  parts.back().iov_base=copy.data();return actual(fd,parts.data(),count);
 }
 return actual(fd,data,count);
}

extern "C" int fsync(int fd) {
 static auto actual=reinterpret_cast<int(*)(int)>(dlsym(RTLD_NEXT,"fsync"));
 if ((mode==4 || mode==5) && imageFile(fd)) {
  pthread_mutex_lock(&mutex);blocked=true;
  while(mode==4)pthread_cond_wait(&ready,&mutex);
  bool success=mode!=5;pthread_mutex_unlock(&mutex);
  if(!success){errno=EIO;return -1;}
 }
 return actual(fd);
}
