// SPDX-License-Identifier: GPL-3.0-or-later
#include "matcher.hpp"
#include <array>
#include <iostream>
#include <random>
#include <stdexcept>
int main(int argc,char** argv) {
  try {
    if(argc!=2)throw std::runtime_error("usage: loop_matcher_test cpu-batch|linux-tpu|c906|c906-tpu");
    LoopMatchProvider matcher(argv[1],true);std::mt19937 rng(123);
    const std::array<std::array<unsigned,4>,5> sizes={{{{80,90,32,120}},{{500,500,100,120}},
      {{0,0,1,120}},{{17,4,0,0}},{{0,0,0,0}}}};
    unsigned cases=0;
    for(const auto& counts:sizes) {
      std::array<std::vector<cv::Mat>,4> data;
      std::array<std::vector<int>,4> nodes;
      for(unsigned set=0;set<4;++set) {
        for(unsigned i=0;i<counts[set];++i) {
          cv::Mat d(1,32,CV_8U);
          for(int j=0;j<32;++j)d.at<uint8_t>(0,j)=rng();
          data[set].push_back(d);nodes[set].push_back((i%4)+1);
        }
      }
      for(unsigned i=0;i<counts[1] && counts[0];++i) {
        data[1][i]=data[0][i%counts[0]].clone();nodes[1][i]=nodes[0][i%counts[0]];
      }
      for(unsigned i=0;i<counts[3] && counts[2];++i) {
        data[3][i]=data[2][i%counts[2]].clone();nodes[3][i]=nodes[2][i%counts[2]];
      }
      if(counts[0] && counts[1]>2) {
        data[1][0]=data[0][0].clone();data[1][1]=data[0][0].clone();
        nodes[1][0]=nodes[1][1]=nodes[0][0];
        data[1].back()=cv::Mat(); // Preserve upstream empty-descriptor behavior.
      }
      svo::LoopFeatureSets f{{&data[0],&data[1],&data[2],&data[3]}};
      svo::LoopNodeSets n{{&nodes[0],&nodes[1],&nodes[2],&nodes[3]}};
      for(int threshold:{0,1,48,256,257}) {Eigen::MatrixXd output;matcher.match(f,n,threshold,&output);++cases;}
    }
    matcher.report();std::cout << "MATCHER EQUIVALENCE PASS cases=" << cases << '\n';return 0;
  } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
