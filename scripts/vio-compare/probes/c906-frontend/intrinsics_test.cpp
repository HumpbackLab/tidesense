// Stress concurrent publications and a reader that must never observe torn calibration.
#include "front_intrinsics.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
static void check(const front_intrinsics_data &data){
 for(unsigned i=1;i<FRONT_INTRINSICS_WORDS;++i)
  assert(data.words[i]==(data.words[0]*37+i));
}
int main(){
 std::vector<unsigned char> memory(FRONT_INTRINSICS_OFF+sizeof(front_intrinsics_channel));
 front_intrinsics_data last{};
 assert(!front_intrinsics_read(memory.data(),&last,nullptr) && !last.words[0]);
 auto publish=[&](unsigned version){front_intrinsics_data data{};data.words[0]=version;
  for(unsigned i=1;i<FRONT_INTRINSICS_WORDS;++i)data.words[i]=version*37+i;
  front_intrinsics_publish(memory.data(),&data,nullptr);};
 publish(1);assert(front_intrinsics_read(memory.data(),&last,nullptr));check(last);
 auto *channel=(front_intrinsics_channel*)(memory.data()+FRONT_INTRINSICS_OFF);
 publish(2);
 unsigned inactive=1u-__atomic_load_n(&channel->published,__ATOMIC_ACQUIRE);
 // Start writing v3, keeping the completed v2 active and readable.
 __atomic_store_n(&channel->slots[inactive].sequence,3,__ATOMIC_RELEASE);
 assert(front_intrinsics_read(memory.data(),&last,nullptr) && last.words[0]==2);check(last);
 __atomic_store_n(&channel->slots[inactive].sequence,2,__ATOMIC_RELEASE);
 std::atomic<bool> done{false};std::atomic<unsigned> reads{0};
 std::thread consumer([&](){front_intrinsics_data previous=last;
  while(!done.load(std::memory_order_acquire)){
   if(front_intrinsics_read(memory.data(),&previous,nullptr)){check(previous);++reads;}
  }
 });
 for(unsigned version=3;version<=200000;++version)publish(version);
 done.store(true,std::memory_order_release);consumer.join();
 assert(front_intrinsics_read(memory.data(),&last,nullptr) && last.words[0]==200000);check(last);
 std::cout<<"PASS latest snapshot: 200000 publications, "<<reads<<" coherent reads; no torn data or frame wait\n";
}
