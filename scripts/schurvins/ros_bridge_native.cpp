// SPDX-License-Identifier: GPL-3.0-or-later
#include "ros_bridge.hpp"
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <geometry_msgs/msg/pose_stamped.h>
#include <std_msgs/msg/bool.h>
#include <rosidl_runtime_c/string_functions.h>
#include <sys/resource.h>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
namespace {
bool requested() {
 const char* value=std::getenv("SCHUR_ROS");
 if(!value || std::string(value)=="0") return false;
 if(std::string(value)=="1") return true;
 throw std::runtime_error("SCHUR_ROS must be 0 or 1");
}
unsigned domainId() {
 const char* value=std::getenv("ROS_DOMAIN_ID");
 if(!value) return 0;
 const std::string text(value);
 if(text.empty() || text.find_first_not_of("0123456789")!=std::string::npos)
  throw std::runtime_error("ROS_DOMAIN_ID must be an integer from 0 to 232");
 const auto number=std::stoull(text);
 if(number>232) throw std::runtime_error("ROS_DOMAIN_ID must be from 0 to 232");
 return static_cast<unsigned>(number);
}
void check(rcl_ret_t value,const char* what) {
 if(value!=RCL_RET_OK) throw std::runtime_error(std::string(what)+": "+rcl_get_error_string().str);
}
void discard(rcl_ret_t value) {if(value!=RCL_RET_OK) rcl_reset_error();}
}
struct RosBridge::Impl {
 struct Sample {uint64_t stamp;bool tracking;double p[3],q[4];};
 const unsigned domain;
 std::mutex mutex;
 std::condition_variable wake;
 std::deque<Sample> queue;
 bool stop=false;
 uint64_t published=0,dropped=0,connections=0;
 std::thread worker;
 Impl():domain(domainId()),worker(&Impl::run,this) {}
 ~Impl() {
  {std::lock_guard<std::mutex> lock(mutex);stop=true;}
  wake.notify_one();worker.join();
  std::cerr<<"native-DDS: connections="<<connections<<" poses="<<published<<" overwritten="<<dropped<<'\n';
 }
 bool stopped() {std::lock_guard<std::mutex> lock(mutex);return stop;}
 void session() {
  auto options=rcl_get_zero_initialized_init_options();
  rclc_support_t support{};
  auto node=rcl_get_zero_initialized_node();
  auto pose_pub=rcl_get_zero_initialized_publisher();
  auto tracking_pub=rcl_get_zero_initialized_publisher();
  geometry_msgs__msg__PoseStamped pose{};std_msgs__msg__Bool tracking{};
  bool options_ok=false,support_ok=false,node_ok=false,pose_ok=false,tracking_ok=false,message_ok=false;
  auto cleanup=[&] {
   if(tracking_ok) discard(rcl_publisher_fini(&tracking_pub,&node));
   if(pose_ok) discard(rcl_publisher_fini(&pose_pub,&node));
   if(node_ok) discard(rcl_node_fini(&node));
   if(support_ok) discard(rclc_support_fini(&support));
   if(message_ok) geometry_msgs__msg__PoseStamped__fini(&pose);
   if(options_ok) discard(rcl_init_options_fini(&options));
  };
  try {
   check(rcl_init_options_init(&options,rcl_get_default_allocator()),"options");options_ok=true;
   check(rcl_init_options_set_domain_id(&options,domain),"domain");
   auto allocator=rcl_get_default_allocator();
   const auto result=rclc_support_init_with_options(&support,0,nullptr,&options,&allocator);
   support_ok=rcl_context_is_valid(&support.context);
   check(result,"support");
   auto node_options=rcl_node_get_default_options();node_options.enable_rosout=false;
   check(rclc_node_init_with_options(&node,"tidesense","/tidesense",&support,&node_options),"node");node_ok=true;
   check(rclc_publisher_init_best_effort(&pose_pub,&node,ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs,msg,PoseStamped),"pose"),"pose");pose_ok=true;
   check(rclc_publisher_init_best_effort(&tracking_pub,&node,ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs,msg,Bool),"tracking"),"tracking");tracking_ok=true;
   if(!geometry_msgs__msg__PoseStamped__init(&pose)) throw std::runtime_error("pose allocation");
   message_ok=true;
   if(!rosidl_runtime_c__String__assign(&pose.header.frame_id,"odom")) throw std::runtime_error("frame_id allocation");
   ++connections;std::cerr<<"native-DDS: ready domain="<<domain<<" (VIO remains independent)\n";
   while(true) {
    Sample s;
    {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stop||!queue.empty();});
     if(stop) break;
     s=queue.front();queue.pop_front();}
    tracking.data=s.tracking;check(rcl_publish(&tracking_pub,&tracking,nullptr),"tracking publish");
    if(!s.tracking || s.stamp/1000000000ULL>uint64_t(std::numeric_limits<int32_t>::max())) continue;
    pose.header.stamp.sec=static_cast<int32_t>(s.stamp/1000000000ULL);
    pose.header.stamp.nanosec=static_cast<uint32_t>(s.stamp%1000000000ULL);
    pose.pose.position.x=s.p[0];pose.pose.position.y=s.p[1];pose.pose.position.z=s.p[2];
    pose.pose.orientation.x=s.q[0];pose.pose.orientation.y=s.q[1];pose.pose.orientation.z=s.q[2];pose.pose.orientation.w=s.q[3];
    check(rcl_publish(&pose_pub,&pose,nullptr),"pose publish");++published;
   }
  } catch(...) {cleanup();throw;}
  cleanup();
 }
 void run() noexcept {
  setpriority(PRIO_PROCESS,0,5);
  while(!stopped()) {
   try {session();}
   catch(const std::exception& e) {
    std::cerr<<"native-DDS: "<<e.what()<<"; retrying (VIO remains independent)\n";rcl_reset_error();
   }
   std::unique_lock<std::mutex> lock(mutex);
   wake.wait_for(lock,std::chrono::seconds(1),[&]{return stop;});
  }
 }
};
RosBridge::RosBridge() {if(requested()) impl_.reset(new Impl);}
RosBridge::~RosBridge()=default;
bool RosBridge::compiled() {return true;}
const char* RosBridge::backend() {return "native-dds";}
bool RosBridge::enabled() const {return bool(impl_);}
void RosBridge::submit(uint64_t stamp,bool tracking,const double* p,const double* q) {
 if(!impl_) return;
 Impl::Sample s{};s.stamp=stamp;s.tracking=tracking&&p&&q;
 if(s.tracking) {std::memcpy(s.p,p,sizeof(s.p));std::memcpy(s.q,q,sizeof(s.q));}
 {std::lock_guard<std::mutex> lock(impl_->mutex);
  if(!s.tracking) {impl_->dropped+=impl_->queue.size();impl_->queue.clear();}
  if(impl_->queue.size()>=64) {impl_->queue.pop_front();++impl_->dropped;}
  impl_->queue.push_back(s);}
 impl_->wake.notify_one();
}
