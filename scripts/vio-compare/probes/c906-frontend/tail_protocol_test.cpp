// Execute the actual C906 handoff helpers with separate simulated CPU caches.
#include <chrono>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include "front_tail.h"
#include "front_intrinsics.h"
static_assert(FRONT_INTRINSICS_OFF+sizeof(front_intrinsics_channel)<=FRONT_TAIL_OFF,"Tail overlaps intrinsics channel");
static std::vector<unsigned char> arm(FRONT_BYTES),rv(FRONT_BYTES),ram(FRONT_TAIL_BYTES);
static std::mutex memory_mutex;
static uint64_t probe_clock(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/40;}
static unsigned char *local(uintptr_t address){return (unsigned char*)address;}
static unsigned offset(uintptr_t address){
 for(auto *p:{&arm,&rv})if(address>=(uintptr_t)p->data()+FRONT_TAIL_OFF && address<(uintptr_t)p->data()+FRONT_TAIL_OFF+FRONT_TAIL_BYTES)return address-(uintptr_t)p->data()-FRONT_TAIL_OFF;
 throw std::runtime_error("Cache operation outside tail channel");
}
static void clean_dcache_range(uintptr_t address,unsigned bytes){std::lock_guard<std::mutex> lock(memory_mutex);unsigned off=offset(address);if(off+bytes>ram.size())throw std::runtime_error("clean overflow");std::memcpy(ram.data()+off,local(address),bytes);}
static void inv_dcache_range(uintptr_t address,unsigned bytes){std::lock_guard<std::mutex> lock(memory_mutex);unsigned off=offset(address);if(off+bytes>ram.size())throw std::runtime_error("invalidate overflow");std::memcpy(local(address),ram.data()+off,bytes);}
#include "front_tail_firmware.h"
static void check(bool value){if(!value)throw std::runtime_error("Protocol assertion failed");}
int main(){try{
 for(int mode=0;mode<5;++mode){
  std::fill(ram.begin(),ram.end(),0);std::fill(arm.begin()+FRONT_TAIL_OFF,arm.begin()+FRONT_TAIL_OFF+FRONT_TAIL_BYTES,0);std::fill(rv.begin()+FRONT_TAIL_OFF,rv.begin()+FRONT_TAIL_OFF+FRONT_TAIL_BYTES,0);
  auto *host=FRONT_TAIL_PTR(front_tail_host,arm.data(),0);host->magic=FRONT_TAIL_MAGIC;host->timestamp=42;host->state=mode==0?FRONT_TAIL_IDLE:FRONT_TAIL_AVAILABLE;
  clean_dcache_range((uintptr_t)host,64);
  front_control f{};f.timestamp=42;front_tail_begin((uintptr_t)rv.data(),&f);
  lk_tpu_level a[5]{},b[5]{};a[0].planes[0]=FRONT_PYR_OFF+FRONT_CAM_BYTES;b[0].planes[0]=FRONT_PYR_OFF+3*FRONT_CAM_BYTES;
  front_point points[75]{};for(unsigned i=0;i<75;++i){points[i].x=i+.25f;points[i].y=i+.5f;}
  check(front_tail_offer(rv.data(),a,b,5,points,47)==0);
  check(front_tail_offer(rv.data(),a,a,5,points,75)==0);
  unsigned delegated=front_tail_offer(rv.data(),a,b,5,points,75);
  if(mode==0){check(!delegated);front_tail_end((uintptr_t)rv.data(),0);continue;}
  check(delegated==16);check(front_tail_offer(rv.data(),a,b,5,points,75)==0);
  std::thread cpu([&,mode]{
   inv_dcache_range((uintptr_t)arm.data()+FRONT_TAIL_OFF+FRONT_TAIL_REMOTE_OFF,64);
   auto *r=FRONT_TAIL_PTR(front_tail_remote,arm.data(),FRONT_TAIL_REMOTE_OFF);check(r->state==FRONT_TAIL_OFFERED && r->timestamp==42);
   inv_dcache_range((uintptr_t)arm.data()+FRONT_TAIL_OFF+FRONT_TAIL_TASK_OFF,sizeof(front_tail_task));
   auto *task=FRONT_TAIL_PTR(front_tail_task,arm.data(),FRONT_TAIL_TASK_OFF);check(task->count==16 && task->first==59 && task->timestamp==42);
   if(mode==4)return; // abandoned claimant: bounded firmware timeout
   std::this_thread::sleep_for(std::chrono::milliseconds(mode));
   auto *result=FRONT_TAIL_PTR(front_tail_result,arm.data(),FRONT_TAIL_RESULT_OFF);
   for(unsigned i=0;i<16;++i){result->output[i]={task->input[i].x+1,task->input[i].y-2};result->valid[i]=1;}
   clean_dcache_range((uintptr_t)result,sizeof(*result));host->state=mode==3?FRONT_TAIL_HOST_FAILED:FRONT_TAIL_HOST_DONE;clean_dcache_range((uintptr_t)host,64);
  });
  lk_tpu_point output[16]{};unsigned char valid[16]{};
  int result=front_tail_join((uintptr_t)rv.data(),output,valid);cpu.join();
  check(result==(mode==3?-71:mode==4?-72:0));
  if(!result)for(unsigned i=0;i<16;++i)check(valid[i] && output[i].x==points[i+59].x+1 && output[i].y==points[i+59].y-2);
  front_tail_end((uintptr_t)rv.data(),result);
  inv_dcache_range((uintptr_t)arm.data()+FRONT_TAIL_OFF+FRONT_TAIL_REMOTE_OFF,64);
  check(FRONT_TAIL_PTR(front_tail_remote,arm.data(),FRONT_TAIL_REMOTE_OFF)->state==FRONT_TAIL_FINISHED);
 }
 std::cout<<"PASS separate caches: unavailable, minimum size, single claim, success, failure, timeout\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
