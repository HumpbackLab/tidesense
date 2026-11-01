#include "jpeg_publisher.hpp"
#include "tide_camera.hpp"
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <sensor_msgs/msg/compressed_image.h>
#include <rosidl_runtime_c/string_functions.h>
#include <exception>
#include <iostream>
#include <limits>
#include <thread>
#include <cstdlib>

namespace {
void check(rcl_ret_t status,const char* operation) {
 if(status!=RCL_RET_OK) throw std::runtime_error(std::string(operation)+": "+rcl_get_error_string().str);
}
void discard(rcl_ret_t status) {if(status!=RCL_RET_OK) rcl_reset_error();}
struct Session {
 rcl_init_options_t options=rcl_get_zero_initialized_init_options();
 rclc_support_t support{};
 rcl_node_t node=rcl_get_zero_initialized_node();
 rcl_publisher_t publishers[2]={rcl_get_zero_initialized_publisher(),rcl_get_zero_initialized_publisher()};
 sensor_msgs__msg__CompressedImage messages[2]{};
 bool options_ok=false,support_ok=false,node_ok=false,publisher_ok[2]{},message_ok[2]{};
 void init() {
  check(rcl_init_options_init(&options,rcl_get_default_allocator()),"JPEG init options");options_ok=true;
  const char* domain=std::getenv("ROS_DOMAIN_ID");
  check(rcl_init_options_set_domain_id(&options,domain?std::stoul(domain):0),"JPEG domain");
  auto allocator=rcl_get_default_allocator();
  auto result=rclc_support_init_with_options(&support,0,nullptr,&options,&allocator);
  support_ok=rcl_context_is_valid(&support.context);check(result,"JPEG support");
  auto node_options=rcl_node_get_default_options();node_options.enable_rosout=false;
  check(rclc_node_init_with_options(&node,"tidesense_images","/tidesense",&support,&node_options),"JPEG node");node_ok=true;
  for(unsigned p=0;p<2;++p) {
   auto options=rcl_publisher_get_default_options();options.qos=rmw_qos_profile_sensor_data;options.qos.depth=1;
   const char* topic=p?"cam1/image_raw/compressed":"cam0/image_raw/compressed";
   check(rcl_publisher_init(&publishers[p],&node,ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs,msg,CompressedImage),topic,&options),"JPEG publisher");
   publisher_ok[p]=true;
   if(!sensor_msgs__msg__CompressedImage__init(&messages[p])) throw std::runtime_error("JPEG message allocation");
   message_ok[p]=true;
   if(!rosidl_runtime_c__String__assign(&messages[p].header.frame_id,p?"cam1_optical_frame":"cam0_optical_frame") ||
      !rosidl_runtime_c__String__assign(&messages[p].format,"jpeg")) throw std::runtime_error("JPEG header allocation");
  }
 }
 ~Session() {
  for(unsigned p=0;p<2;++p) {
   if(publisher_ok[p]) discard(rcl_publisher_fini(&publishers[p],&node));
   if(message_ok[p]) sensor_msgs__msg__CompressedImage__fini(&messages[p]);
  }
  if(node_ok) discard(rcl_node_fini(&node));
  if(support_ok) discard(rclc_support_fini(&support));
  if(options_ok) discard(rcl_init_options_fini(&options));
 }
 void publish(unsigned p,const tide_cam_jpeg_frame& frame,int64_t offset) {
  if(frame.timestamp_ns>uint64_t(INT64_MAX) ||
     (offset>0 && frame.timestamp_ns>uint64_t(INT64_MAX-offset)) ||
     (offset<0 && frame.timestamp_ns<uint64_t(-offset))) throw std::runtime_error("JPEG timestamp overflow");
  const uint64_t stamp=uint64_t(int64_t(frame.timestamp_ns)+offset);
  if(stamp/1000000000ULL>uint64_t(std::numeric_limits<int32_t>::max())) throw std::runtime_error("JPEG ROS timestamp out of range");
  auto& message=messages[p];
  message.header.stamp.sec=static_cast<int32_t>(stamp/1000000000ULL);
  message.header.stamp.nanosec=static_cast<uint32_t>(stamp%1000000000ULL);
  // Current Cyclone RMW synchronously serializes this sequence during publish.
  // Borrow the VENC view rather than copying into a ROS-owned image buffer.
  message.data.data=const_cast<uint8_t*>(frame.data);
  message.data.size=message.data.capacity=frame.size;
  auto status=rcl_publish(&publishers[p],&message,nullptr);
  // Detach before checking errors or destroying the message: VENC owns these bytes.
  message.data.data=nullptr;message.data.size=message.data.capacity=0;
  check(status,"JPEG publish");
 }
};
}

struct JpegPublisher::Impl {
 TideCamera& camera;
 const int64_t offset;
 std::exception_ptr failure;
 uint64_t pairs=0,bytes=0;
 std::thread worker;
 Impl(TideCamera& c,int64_t clock_offset):camera(c),offset(clock_offset),worker(&Impl::run,this) {}
 void run() noexcept {
  try {
   Session session;session.init();
   while(const auto* pair=camera.getJpeg()) {
    try {
     for(unsigned p=0;p<2;++p) {session.publish(p,pair->frames[p],offset);bytes+=pair->frames[p].size;}
     ++pairs;
    } catch(...) {
     try {camera.releaseJpeg(pair);} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';}
     throw;
    }
    camera.releaseJpeg(pair);
   }
  } catch(...) {
   failure=std::current_exception();
   try {std::rethrow_exception(failure);} catch(const std::exception& e) {std::cerr<<"JPEG-DDS: "<<e.what()<<'\n';} catch(...) {}
   camera.stop(); // Wake the visual consumer too; do not leave it blocked on JPEG backpressure.
  }
 }
 void join() {if(worker.joinable())worker.join();}
 ~Impl() {camera.stop();join();std::cerr<<"JPEG-DDS: pairs="<<pairs<<" bytes="<<bytes<<'\n';}
};
JpegPublisher::JpegPublisher(TideCamera& camera,int64_t offset):impl_(new Impl(camera,offset)) {}
JpegPublisher::~JpegPublisher()=default;
void JpegPublisher::finish() {impl_->join();if(impl_->failure)std::rethrow_exception(impl_->failure);}
