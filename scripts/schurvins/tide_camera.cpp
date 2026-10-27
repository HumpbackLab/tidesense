#include "tide_camera.hpp"
#include <iostream>
#include <memory>
#include <cstdlib>

void loadCameraSettings(tide_cam_config& config) {
 const char* path=std::getenv("TIDE_CAM_CONFIG_PATH");
 if (!path || !*path) path="/mnt/data/tide-configurator/camera.conf";
 char error[256]{};
 int status=tide_cam_config_load_file(path,&config,error,sizeof(error));
 if (status && status!=TIDE_CAM_NOT_FOUND)
  throw std::runtime_error(std::string("Camera settings: ")+error);
}

TideCamera::TideCamera(const tide_cam_config& config) {
 char message[256]{};
 int status=tide_cam_open(&config,&camera_,message,sizeof(message));
 if (status) throw std::runtime_error(std::string("tide_cam open: ")+tide_cam_status_string(status)+": "+message);
 std::cout << "camera_exposure_max_us=" << config.exposure_max_us << '\n';
}
TideCamera::~TideCamera() {
 if (camera_) {
  int status=tide_cam_close(camera_);
  if (status) std::cerr<<"tide_cam close: "<<tide_cam_status_string(status)<<'\n';
 }
}
std::string TideCamera::error(int status) const {
 char message[256]{};
 tide_cam_get_error(camera_,message,sizeof(message));
 return std::string(tide_cam_status_string(status))+": "+message;
}
bool TideCamera::next(LivePair& pair,const volatile sig_atomic_t& interrupted) {
 while (!interrupted) {
  const tide_cam_frame_set* frames=nullptr;
  int status=tide_cam_acquire(camera_,&frames,100);
  if (status==TIDE_CAM_TIMEOUT) continue;
  if (status==TIDE_CAM_STOPPED) return false;
  if (status) throw std::runtime_error("tide_cam: "+error(status));
  auto release=[&](const tide_cam_frame_set* f){ tide_cam_release(camera_,f); };
  std::unique_ptr<const tide_cam_frame_set,decltype(release)> lease(frames,release);
  if (frames->camera_count!=2 || frames->clock!=TIDE_CAM_CLOCK_MONOTONIC ||
      frames->timestamp_kind!=TIDE_CAM_TIMESTAMP_VI_ARRIVAL)
   throw std::runtime_error("Stereo input requires two cameras with monotonic VI arrival timestamps");
  const auto& left=frames->frames[0]; const auto& right=frames->frames[1];
  if (left.camera_id!=0 || right.camera_id!=1 || left.width!=right.width || left.height!=right.height)
   throw std::runtime_error("Unexpected stereo camera order/dimensions");
  LivePair owned;
  owned.header={CAMERA_MAGIC,CAMERA_VERSION,left.width,left.height,frames->sequence,
   left.timestamp_ns,right.timestamp_ns,left.sensor_sequence,right.sensor_sequence,
   frames->sync==TIDE_CAM_SYNC_HARDWARE?CAMERA_FSIN:0u,0};
  cv::Mat* images[]={&owned.left,&owned.right};
  for (unsigned p=0;p<2;++p) {
   const auto& f=frames->frames[p];
   if (!f.data || f.pixel_format!=TIDE_CAM_MONO8 || f.stride<f.width ||
       f.data_size<uint64_t(f.stride)*f.height)
    throw std::runtime_error("Invalid mono8 capture buffer");
   *images[p]=cv::Mat(f.height,f.width,CV_8UC1,const_cast<uint8_t*>(f.data),f.stride).clone();
  }
  pair=std::move(owned);
  return true;
 }
 return false;
}
int TideCamera::stop() { return tide_cam_stop(camera_); }
void TideCamera::report() const {
 tide_cam_stats stats{};
 tide_cam_get_stats(camera_,&stats);
 std::cout<<"camera_source=tide_cam camera_received="<<stats.captured
          <<" replaced_pending="<<stats.replaced<<" camera_status="<<stats.terminal_status<<'\n';
}
YAML::Node cameraConfiguration(const tide_cam_config& config) {
 tide_cam_caps caps{};
 if (tide_cam_query_caps(&caps)) throw std::runtime_error("Cannot query camera capabilities");
 YAML::Node n;
 n["source"]="tide_cam"; n["camera_count"]=caps.camera_count;
 n["width"]=config.mode.width; n["height"]=config.mode.height; n["pixel_format"]="mono8";
 n["sync"]=config.sync==TIDE_CAM_SYNC_HARDWARE?"hardware":"free_run";
 const auto rate=config.sync==TIDE_CAM_SYNC_HARDWARE?caps.hardware_sync_fps:config.free_run_fps;
 n["nominal_fps_num"]=rate.num; n["nominal_fps_den"]=rate.den;
 n["exposure_max_us"]=config.exposure_max_us;
 n["timestamp"]="VI arrival PTS, no calibration offset"; n["clock"]="CLOCK_MONOTONIC";
 n["orientation"]="original";
 return n;
}
