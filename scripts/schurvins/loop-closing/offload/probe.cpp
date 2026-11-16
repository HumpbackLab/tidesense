// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>
int main(int argc,char** argv) {
  try {
    if(argc<2 || argc>3)throw std::runtime_error("usage: loop_match_probe linux-tpu|c906|c906-tpu [repeats]");
    const int repeats=argc==3?std::stoi(argv[2]):10;
    if(repeats<1 || repeats>1000)throw std::runtime_error("Invalid repeats");
    LoopMatchEngine engine(argv[1]);std::mt19937 rng(42);
    std::vector<std::array<uint8_t,32>> a(1024),b(1024);
    std::vector<unsigned> expected(1024);
    for(size_t i=0;i<a.size();++i) {
      for(unsigned j=0;j<32;++j) {a[i][j]=rng();b[i][j]=rng();}
      if(i<257) {a[i].fill(0);b[i].fill(0);for(size_t k=0;k<i;++k)b[i][k/8]|=1u<<(k%8);}
      for(unsigned j=0;j<32;++j)expected[i]+=__builtin_popcount(unsigned(a[i][j]^b[i][j]));
    }
    const auto begin=std::chrono::steady_clock::now();
    for(int r=0;r<repeats;++r) {
      engine.begin(1024);
      for(size_t i=0;i<a.size();++i)engine.append(i,3,a[i].data(),b[i].data());
      // Equal-distance tie and cross-batch persistence: index 1 must replace index 3.
      for(size_t i=0;i<a.size();++i)engine.append(i,1,a[i].data(),b[i].data());
      auto results=engine.finish();
      for(size_t i=0;i<results.size();++i)
        if(results[i].distance!=expected[i] || results[i].candidate!=1)
          throw std::runtime_error("Hamming mismatch at "+std::to_string(i)+" expected="+
            std::to_string(expected[i])+" got="+std::to_string(results[i].distance));
    }
    // Partial batch and a query with no eligible candidates.
    engine.begin(2);engine.append(0,0,a[0].data(),b[0].data());auto partial=engine.finish();
    if(partial[0].distance || partial[1].candidate!=0xffff || partial[1].distance!=257)
      throw std::runtime_error("Partial Hamming batch failed");
    const auto& s=engine.stats();
    std::cout << "HAMMING PASS backend=" << argv[1] << " repeats=" << repeats << " pairs=" << s.pairs
      << " batches=" << s.batches << " tpu_calls=" << s.tpu_calls << " reductions=" << s.reductions
      << " submit_ms=" << s.submit_ms << " device_ms=" << s.device_ms << " tpu_ms=" << s.tpu_ms
      << " thread_cpu_ms=" << s.thread_cpu_ms << " elapsed_ms="
      << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count() << '\n';
    return 0;
  } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
