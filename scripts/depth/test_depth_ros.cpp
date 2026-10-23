// SPDX-License-Identifier: GPL-3.0-or-later
// Fragmentation/reconnection fixture: no physical sensors, exact known pixels.
#include "depth_ros.hpp"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc, char** argv) try {
  const int width=argc>1 ? std::stoi(argv[1]) : 320;
  const int count=argc>2 ? std::stoi(argv[2]) : 200;
  if ((width!=320 && width!=640) || count<1 || count>10000)
    throw std::runtime_error("Invalid fixture size/count");
  DepthIntrinsics info;
  info.width=width; info.height=width*5/8; info.fx=200; info.fy=201;
  info.cx=width/2.; info.cy=info.height/2.; info.baseline=.05;
  DepthRos ros(info);
  cv::Mat depth(info.height,width,CV_16UC1);
  for (int seq=0; seq<count; ++seq) {
    for (size_t i=0; i<depth.total(); ++i) depth.ptr<uint16_t>()[i]=uint16_t(i+seq*17);
    ros.submit(1700000000000000000ULL+uint64_t(seq)*100000000ULL,depth);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
