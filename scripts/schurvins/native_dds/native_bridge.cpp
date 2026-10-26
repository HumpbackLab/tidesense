// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_bridge.hpp"
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <geometry_msgs/msg/pose_stamped.h>
#include <std_msgs/msg/bool.h>
#include <rosidl_runtime_c/string_functions.h>
#include <sys/resource.h>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace {
void check(rcl_ret_t value,const char* what) {
 if(value!=RCL_RET_OK) throw std::runtime_error(std::string(what)+": "+rcl_get_error_string().str);
}
void discard(rcl_ret_t value) {if(value!=RCL_RET_OK) rcl_reset_error();}
}
struct NativeBridge::Impl {
 struct Sample {uint64_t stamp;bool tracking;double p[3],q[4];};
 std::mutex mutex;
 std::condition_variable wake;
 std::deque<Sample> queue;
 bool stop=false;
 std::atomic<bool> failed{false};
 uint64_t published=0,dropped=0;
 std::thread worker;
 Impl():worker(&Impl::run,this) {}
 ~Impl() {
  {std::lock_guard<std::mutex> lock(mutex);stop=true;}
  wake.notify_one();worker.join();
  std::cerr<<"native-DDS: poses="<<published<<" overwritten="<<dropped<<" failed="<<failed<<'\n';
 }
 void run() noexcept {
  setpriority(PRIO_PROCESS,0,5);
  auto options=rcl_get_zero_initialized_init_options();
  rclc_support_t support{};
  auto node=rcl_get_zero_initialized_node();
  auto pose_pub=rcl_get_zero_initialized_publisher();
  auto tracking_pub=rcl_get_zero_initialized_publisher();
  geometry_msgs__msg__PoseStamped pose{};std_msgs__msg__Bool tracking{};
  bool options_ok=false,support_ok=false,node_ok=false,pose_ok=false,tracking_ok=false,message_ok=false;
  try {
   check(rcl_init_options_init(&options,rcl_get_default_allocator()),"options");options_ok=true;
   const char* domain=std::getenv("ROS_DOMAIN_ID");
   check(rcl_init_options_set_domain_id(&options,domain?std::stoul(domain):0),"domain");
   auto allocator=rcl_get_default_allocator();
   check(rclc_support_init_with_options(&support,0,nullptr,&options,&allocator),"support");support_ok=true;
   auto node_options=rcl_node_get_default_options();node_options.enable_rosout=false;
   check(rclc_node_init_with_options(&node,"tidesense","/tidesense",&support,&node_options),"node");node_ok=true;
   check(rclc_publisher_init_best_effort(&pose_pub,&node,ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs,msg,PoseStamped),"pose"),"pose");pose_ok=true;
   check(rclc_publisher_init_best_effort(&tracking_pub,&node,ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs,msg,Bool),"tracking"),"tracking");tracking_ok=true;
   if(!geometry_msgs__msg__PoseStamped__init(&pose)) throw std::runtime_error("pose allocation");
   message_ok=true;
   if(!rosidl_runtime_c__String__assign(&pose.header.frame_id,"odom")) throw std::runtime_error("frame_id");
   while(true) {
    Sample s;
    {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stop||!queue.empty();});
     if(stop) break;
     s=queue.front();queue.pop_front();}
    tracking.data=s.tracking;check(rcl_publish(&tracking_pub,&tracking,nullptr),"tracking publish");
    if(!s.tracking) continue;
    pose.header.stamp.sec=static_cast<int32_t>(s.stamp/1000000000ULL);
    pose.header.stamp.nanosec=static_cast<uint32_t>(s.stamp%1000000000ULL);
    pose.pose.position.x=s.p[0];pose.pose.position.y=s.p[1];pose.pose.position.z=s.p[2];
    pose.pose.orientation.x=s.q[0];pose.pose.orientation.y=s.q[1];pose.pose.orientation.z=s.q[2];pose.pose.orientation.w=s.q[3];
    check(rcl_publish(&pose_pub,&pose,nullptr),"pose publish");++published;
   }
  } catch(const std::exception& e) {failed=true;std::cerr<<"native-DDS failure: "<<e.what()<<'\n';rcl_reset_error();}
  if(tracking_ok) discard(rcl_publisher_fini(&tracking_pub,&node));
  if(pose_ok) discard(rcl_publisher_fini(&pose_pub,&node));
  if(node_ok) discard(rcl_node_fini(&node));
  if(support_ok) discard(rclc_support_fini(&support));
  if(message_ok) geometry_msgs__msg__PoseStamped__fini(&pose);
  if(options_ok) discard(rcl_init_options_fini(&options));
 }
};
NativeBridge::NativeBridge():impl_(new Impl) {}
NativeBridge::~NativeBridge()=default;
void NativeBridge::submit(uint64_t stamp,bool tracking,const double* p,const double* q) {
 if(impl_->failed) throw std::runtime_error("Native DDS worker failed");
 Impl::Sample s{};s.stamp=stamp;s.tracking=tracking;
 if(tracking) {std::memcpy(s.p,p,sizeof(s.p));std::memcpy(s.q,q,sizeof(s.q));}
 {std::lock_guard<std::mutex> lock(impl_->mutex);
  if(!tracking) {impl_->dropped+=impl_->queue.size();impl_->queue.clear();}
  if(impl_->queue.size()>=64) {impl_->queue.pop_front();++impl_->dropped;}
  impl_->queue.push_back(s);}
 impl_->wake.notify_one();
}
