// Experimental benchmark transport; little-endian wire format, not a product API.
#include <array>
#include <atomic>
#include <thread>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <sys/resource.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <semaphore.h>

class BenchSender {
  struct Slot { std::vector<unsigned char> bytes; size_t size=0; uint32_t id=0; };
  std::array<Slot, 4> slots;
  std::atomic<uint64_t> written{0}, read{0};
  std::atomic<bool> done{false};
  std::thread worker;
  sem_t ready;
  int fd=-1;
  double hz=0;
  uint64_t last_image=0;
  uint64_t packets=0, bytes=0, errors=0, dropped=0, messages=0, images=0;
  uint64_t worker_cpu=0;
  int priority_error=0;
  void run() {
    priority_error=setpriority(PRIO_PROCESS, 0, 5);
    auto begin=clockNs(CLOCK_THREAD_CPUTIME_ID);
    unsigned char datagram[1232];
    for (;;) {
      while(sem_wait(&ready) && errno==EINTR) {}
      auto r=read.load(std::memory_order_relaxed);
      if (r==written.load(std::memory_order_acquire)) {
        if(done.load()) break;
        continue;
      }
      auto& s=slots[r%slots.size()];
      uint32_t count=(s.size+1199)/1200;
      for(uint32_t i=0;i<count;++i) {
        uint32_t header[4]={0x54534431,s.id,i,count};
        memcpy(datagram,header,sizeof(header));
        size_t n=std::min(size_t(1200),s.size-size_t(i)*1200);
        memcpy(datagram+16,s.bytes.data()+size_t(i)*1200,n);
        auto sent=send(fd,datagram,n+16,MSG_DONTWAIT);
        if(sent==ssize_t(n+16)) { ++packets; bytes+=sent; } else ++errors;
      }
      read.store(r+1,std::memory_order_release);
    }
    worker_cpu=clockNs(CLOCK_THREAD_CPUTIME_ID)-begin;
  }
public:
  BenchSender() {
    const char* host=std::getenv("SCHUR_SEND_HOST");
    if(host) {
    if(const char* rate=std::getenv("SCHUR_SEND_IMAGE_HZ")) hz=std::stod(rate);
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(39281);
    if(inet_pton(AF_INET,host,&addr.sin_addr)!=1) throw std::runtime_error("Bad send IPv4");
    fd=socket(AF_INET,SOCK_DGRAM,0);
    if(fd<0 || connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)))
      throw std::runtime_error("UDP setup failed");
    }
    for(auto& s:slots) s.bytes.resize(1024*1024);
    if(sem_init(&ready,0,0)) throw std::runtime_error("sem_init failed");
    worker=std::thread([this]{run();});
  }
  void submit(uint64_t stamp, const double* pose, const cv::Mat& image) {
    if(fd<0) return;
    auto w=written.load(std::memory_order_relaxed);
    if(w-read.load(std::memory_order_acquire)>=slots.size()) { ++dropped; return; }
    auto& s=slots[w%slots.size()];
    bool include=hz>0 && (!last_image || stamp-last_image>=uint64_t(1e9/hz)-1000);
    uint32_t shape[2]={include?uint32_t(image.cols):0,include?uint32_t(image.rows):0};
    s.size=72+(include?image.total():0);
    if(s.size>s.bytes.size()) throw std::runtime_error("Image exceeds preallocated slot");
    memcpy(s.bytes.data(),&stamp,8); memcpy(s.bytes.data()+8,pose,56);
    memcpy(s.bytes.data()+64,shape,8);
    if(include) {
      for(int row=0;row<image.rows;++row)
        memcpy(s.bytes.data()+72+size_t(row)*image.cols,image.ptr(row),image.cols);
      last_image=stamp; ++images;
    }
    s.id=uint32_t(w); ++messages;
    written.store(w+1,std::memory_order_release);
    sem_post(&ready);
  }
  ~BenchSender() {
    done.store(true); sem_post(&ready); worker.join(); sem_destroy(&ready);
    if(fd>=0) close(fd);
    std::cout << "SEND messages="<<messages<<" images="<<images<<" packets="<<packets
      <<" bytes="<<bytes<<" errors="<<errors<<" queue_drops="<<dropped
      <<" worker_cpu_ms="<<worker_cpu/1e6<<" priority_error="<<priority_error<<'\n';
  }
};
