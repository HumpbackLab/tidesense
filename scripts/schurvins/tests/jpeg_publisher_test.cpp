#include "jpeg_publisher.hpp"
#include "tide_camera.hpp"
#include "ros_bridge.hpp"
#include "backend_mock.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>

int main() {
 try {
  mock_reset();
  tide_cam_config config;tide_cam_config_init(&config);config.jpeg_quality=80;config.queue=TIDE_CAM_FIFO;
  RosBridge poses;
  TideCamera camera(config);
  JpegPublisher publisher(camera,1000000000LL);
  std::string ready;
  if(!std::getline(std::cin,ready) || ready!="start") throw std::runtime_error("Receiver did not complete discovery");
  mock_push(3);
  volatile sig_atomic_t interrupted=0;
  for(unsigned i=1;i<=3;++i) {
   LivePair pair;
   if(!camera.next(pair,interrupted) || pair.header.sequence!=i) throw std::runtime_error("Unexpected mono8 sequence");
   const double position[3]={double(i),0,0},quaternion[4]={0,0,0,1};
   poses.submit(2000000000ULL+i*50000000ULL,true,position,quaternion);
  }
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(mock_jpeg_released()!=3 && std::chrono::steady_clock::now()<deadline)
   std::this_thread::sleep_for(std::chrono::milliseconds(2));
  if(mock_jpeg_released()!=3) throw std::runtime_error("JPEG worker failed to release all source pairs");
  camera.stop();publisher.finish();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  std::cout<<"JPEG ROS FIXTURE PASS: mono8 and JPEG workers share source pairs, all hardware views released\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
