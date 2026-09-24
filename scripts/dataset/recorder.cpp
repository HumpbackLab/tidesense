// Standalone sensor recorder. Lossless residual+zstd, no estimator or ROS.
#include "live_camera.hpp"
#include "live_imu.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <zstd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
uint64_t now() {
 timespec t{}; if (clock_gettime(CLOCK_MONOTONIC, &t)) throw std::runtime_error("clock_gettime failed");
 return uint64_t(t.tv_sec)*1000000000 + t.tv_nsec;
}
void open(std::ofstream& f, const std::string& path) {
 f.exceptions(std::ios::failbit | std::ios::badbit);
 f.open(path, std::ios::binary); f << std::setprecision(17);
}
void yaml(const std::string& path, const YAML::Node& n) {
 YAML::Emitter e; e.SetDoublePrecision(17); e << n;
 if (!e.good()) throw std::runtime_error("YAML serialization failed");
 std::ofstream f; open(f,path); f << e.c_str() << '\n'; f.close();
}
void snapshot(const std::string& from, const std::string& to) {
 std::ifstream in(from, std::ios::binary);
 if (!in) throw std::runtime_error("Cannot read " + from);
 std::ofstream out; open(out,to); out << in.rdbuf(); out.close();
}
// One sequential image stream keeps SD metadata work out of the capture path.
class Writer {
 struct Batch { LivePair pair; std::vector<ImuSample> imu; };
 std::string dir_, error_;
 std::ofstream images_, imu_;
 std::mutex mutex_;
 std::condition_variable ready_;
 std::deque<Batch> queue_;
 std::thread thread_;
 bool stop_ = false;
 size_t capacity_, high_water_ = 0;
 uint64_t pairs_=0, samples_=0, first_=0, last_=0, imu_first_=0, imu_last_=0;
 uint64_t max_cam_gap_=0, max_imu_gap_=0, compressed_bytes_=0, compression_ns_=0;
 void space() {
  struct statvfs fs{};
  if (statvfs(dir_.c_str(),&fs)) throw std::runtime_error("Cannot query free space");
  if (fs.f_flag & ST_SYNCHRONOUS) throw std::runtime_error("SD is mounted sync; remount async");
  if (uint64_t(fs.f_bavail)*fs.f_frsize < 64ULL*1024*1024)
   throw std::runtime_error("Less than 64 MiB free on SD");
 }
 void work() {
  try {
   std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> compressor(ZSTD_createCCtx(), ZSTD_freeCCtx);
   if (!compressor) throw std::runtime_error("Cannot allocate zstd compressor");
   ZSTD_CCtx_setParameter(compressor.get(), ZSTD_c_compressionLevel, 1);
   ZSTD_CCtx_setParameter(compressor.get(), ZSTD_c_checksumFlag, 1);
   std::vector<uint8_t> pixels(sizeof(CameraHeader)+2*640*400);
   std::vector<uint8_t> compressed(ZSTD_compressBound(pixels.size()));
   while (true) {
    Batch b;
    {
     std::unique_lock<std::mutex> lock(mutex_);
     ready_.wait(lock,[&]{ return stop_ || !queue_.empty(); });
     if (queue_.empty()) break;
     b=std::move(queue_.front()); queue_.pop_front();
    }
    if (pairs_%20 == 0) space();
    const auto& h=b.pair.header;
    // Write IMU before the corresponding image; incomplete archives retain a prefix.
    for (const auto& s:b.imu) {
     imu_ << s.stamp_ns;
     for (double v:s.gyro) imu_ << ',' << v;
     for (double v:s.accel) imu_ << ',' << v;
     for (double v:s.raw_accel) imu_ << ',' << v;
     imu_ << '\n';
     if (!samples_) imu_first_=s.stamp_ns;
     if (imu_last_) max_imu_gap_=std::max(max_imu_gap_,s.stamp_ns-imu_last_);
     imu_last_=s.stamp_ns; ++samples_;
    }
    const uint64_t begin=now();
    std::memcpy(pixels.data(), &h, sizeof(h));
    size_t position=sizeof(h);
    for (const cv::Mat* im:{&b.pair.left,&b.pair.right}) {
     for (int y=0;y<im->rows;++y) {
      const uint8_t* row=im->ptr(y);
      uint8_t* dst=pixels.data()+position;
      dst[0]=row[0];
      for (int x=1;x<im->cols;++x) dst[x]=uint8_t(row[x]-row[x-1]);
      position+=im->cols;
     }
    }
    const size_t bytes=ZSTD_compress2(compressor.get(),compressed.data(),compressed.size(),pixels.data(),pixels.size());
    if (ZSTD_isError(bytes)) throw std::runtime_error(ZSTD_getErrorName(bytes));
    compression_ns_+=now()-begin;
    // AArch64 little endian: uint32 length + one checksummed independent zstd frame.
    const uint32_t length=static_cast<uint32_t>(bytes);
    images_.write(reinterpret_cast<const char*>(&length),sizeof(length));
    images_.write(reinterpret_cast<const char*>(compressed.data()),bytes);
    compressed_bytes_+=bytes+sizeof(length);
    if (!pairs_) first_=h.left_ns;
    if (last_) max_cam_gap_=std::max(max_cam_gap_,h.left_ns-last_);
    last_=h.left_ns; ++pairs_;
   }
  } catch (const std::exception& e) {
   std::lock_guard<std::mutex> lock(mutex_); error_=e.what(); queue_.clear();
  }
 }
 void join() {
  { std::lock_guard<std::mutex> lock(mutex_); stop_=true; ready_.notify_all(); }
  if (thread_.joinable()) thread_.join();
 }
 void metadata(bool complete) {
  YAML::Node n;
  n["schema"]="tidesense-dataset-v1"; n["complete"]=complete;
  n["clock"]="CLOCK_MONOTONIC nanoseconds";
  n["camera_timestamp"]="raw VI arrival PTS; not exposure time; no time offset applied";
  n["stereo"]="FSIN paired, original sensor orientation, mono8 640x400";
  n["scope"]="contiguous pairs after camera/IMU warmup; IMU starts before first pair and ends after last pair";
  n["camera_hz_nominal"]=20; n["imu_hz_nominal"]=200;
  n["pairs"]=pairs_; n["imu_samples"]=samples_;
  n["first_camera_ns"]=first_; n["last_camera_ns"]=last_;
  n["first_imu_ns"]=imu_first_; n["last_imu_ns"]=imu_last_;
  n["camera_hz_measured"]=last_>first_?(pairs_-1)*1e9/(last_-first_):0;
  n["imu_hz_measured"]=imu_last_>imu_first_?(samples_-1)*1e9/(imu_last_-imu_first_):0;
  n["max_camera_gap_ms"]=max_cam_gap_*1e-6; n["max_imu_gap_ms"]=max_imu_gap_*1e-6;
  n["writer_queue_capacity"]=capacity_; n["writer_queue_high_water"]=high_water_;
  n["writer_error"]=error_;
  n["compression"]="horizontal uint8 residual per row + zstd level 1, per-pair checksum";
  n["compressed_image_bytes"]=compressed_bytes_;
  n["compression_ms_per_pair"]=pairs_?compression_ns_*1e-6/pairs_:0;
  yaml(dir_+"/metadata.yaml",n);
 }
 public:
 Writer(const std::string& dir, size_t capacity):dir_(dir),capacity_(capacity) {
  if (mkdir(dir.c_str(),0755)) throw std::runtime_error("Output must be a NEW directory: "+dir+": "+strerror(errno));
  { std::ofstream f; open(f,dir+"/INCOMPLETE"); f << "Capture not finalized\n"; }
  space(); open(images_,dir+"/frames.bin"); open(imu_,dir+"/imu.csv");
  images_ << "TIDESENSE_ZST_V1\n";
  imu_ << "#timestamp_ns,gx,gy,gz,ax,ay,az,raw_ax,raw_ay,raw_az\n";
  metadata(false); thread_=std::thread(&Writer::work,this);
 }
 ~Writer() { join(); }
 void submit(LivePair&& pair, std::vector<ImuSample>&& samples) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!error_.empty()) throw std::runtime_error(error_);
  if (queue_.size()>=capacity_) throw std::runtime_error("Recording queue full: SD cannot sustain capture; dataset INCOMPLETE");
  queue_.push_back({std::move(pair),std::move(samples)});
  high_water_=std::max(high_water_,queue_.size()); ready_.notify_one();
 }
 void finish(bool complete, const std::string& reason) {
  join();
  if (!reason.empty() && error_.empty()) error_=reason;
  images_.close(); imu_.close();
  complete=complete && error_.empty() && pairs_>0;
  metadata(complete);
  // Flush this filesystem before publishing COMPLETE; includes delayed write errors.
  int fd=::open((dir_+"/frames.bin").c_str(),O_RDONLY);
  if (fd<0) throw std::runtime_error("Cannot open recording for sync");
  int result=syncfs(fd); ::close(fd);
  if (result) throw std::runtime_error("SD sync failed; dataset INCOMPLETE");
  if (complete) {
   if (rename((dir_+"/INCOMPLETE").c_str(),(dir_+"/COMPLETE").c_str())) throw std::runtime_error("Cannot finalize marker");
   fd=::open(dir_.c_str(),O_RDONLY|O_DIRECTORY);
   if (fd<0) throw std::runtime_error("Cannot open dataset directory");
   result=fsync(fd); ::close(fd);
   if (result) throw std::runtime_error("Cannot sync COMPLETE directory entry");
  }
  std::cout << (complete?"COMPLETE":"INCOMPLETE") << " pairs=" << pairs_ << " imu=" << samples_
            << " writer_queue_high_water=" << high_water_ << '/' << capacity_ << '\n';
  if (!complete) throw std::runtime_error(error_.empty()?"No frames recorded":error_);
 }
};
}
int main(int argc,char** argv) {
 std::string dir,ini="/mnt/data/sensor_cfg.ini",device="auto",calibration,helper=executableDirectory()+"/schurvins_camera";
 uint64_t seconds=0,capacity=64;
 try {
  for (int i=1;i<argc;++i) {
   std::string option=argv[i];
   if (option=="--help") {
    std::cout << "tidesense_record --output NEW_DIR [--seconds N] [--sensor-ini FILE]\n"
     "  [--imu auto|/dev/iio:deviceN] [--calibration SCHUR.yaml] [--queue-pairs 1..128]\n"
     "  [--camera-helper PATH]\n20Hz stereo / 200Hz IMU. No VINS or ROS. Ctrl-C stops and flushes.\n"; return 0;
   }
   if (++i>=argc) throw std::runtime_error("Missing option value");
   std::string value=argv[i];
   if (option=="--output") dir=value;
   else if (option=="--sensor-ini") ini=value;
   else if (option=="--imu") device=value;
   else if (option=="--calibration") calibration=value;
   else if (option=="--camera-helper") helper=value;
   else if (option=="--seconds" || option=="--queue-pairs") {
    if (value.empty() || value.find_first_not_of("0123456789")!=std::string::npos) throw std::runtime_error("Expected unsigned integer");
    uint64_t n=std::stoull(value);
    if (option=="--seconds") { if(n>86400) throw std::runtime_error("seconds must be 0..86400"); seconds=n; }
    else { if(n<1 || n>128) throw std::runtime_error("queue-pairs must be 1..128"); capacity=n; }
   } else throw std::runtime_error("Unknown option: "+option);
  }
  if (dir.empty()) throw std::runtime_error("--output NEW_DIR is required");
  int64_t offset=0;
  if (!calibration.empty()) {
   auto n=YAML::LoadFile(calibration);
   if (!n["cameras"] || n["cameras"].size()!=2) throw std::runtime_error("Expected stereo SchurVINS calibration");
   if (n["live_camera_time_offset_ns"]) offset=n["live_camera_time_offset_ns"].as<int64_t>();
   if (offset < -1000000000LL || offset>1000000000LL) throw std::runtime_error("Invalid camera time offset");
  }
  struct sigaction sa{}; sa.sa_handler=interrupt;
  sigaction(SIGINT,&sa,nullptr); sigaction(SIGTERM,&sa,nullptr);
  Writer writer(dir,capacity);
  snapshot(ini,dir+"/sensor_cfg.ini");
  if (!calibration.empty()) snapshot(calibration,dir+"/calibration.yaml");
  // As in live VIO: camera startup first, then enable buffered IIO acquisition.
  // FSIN itself keeps the IMU IRQ active even before its IIO buffer is enabled.
  LiveCamera camera(helper,ini,8);
  LivePair pair;
  if (!camera.next(pair,interrupted)) throw std::runtime_error("Interrupted during camera startup");
  LiveImu imu(device);
  yaml(dir+"/imu-calibration.yaml",imu.recordingMetadata());
  const uint64_t ready=now()+100000000ULL;
  uint64_t first=0,last=0,previous_imu=0,count=0,sequence=0;
  uint32_t left_ref=0,right_ref=0;
  std::string error;
  try {
   while (camera.next(pair,interrupted)) {
    const auto h=pair.header;
    if (h.left_ns<ready) continue;
    if (!(h.flags & CAMERA_FSIN)) throw std::runtime_error("Recording requires hw_sync=1 on both cameras");
    if (last && (h.sequence!=sequence+1 || h.left_ns-last>75000000 ||
                 uint32_t(h.left_ref-left_ref)!=1 || uint32_t(h.right_ref-right_ref)!=1))
     throw std::runtime_error("Camera sequence/reference/time gap: refusing silent frame loss");
    // Include coverage for raw stamps AND an optional calibrated camera shift.
    uint64_t edge=h.left_ns+uint64_t(std::max<int64_t>(offset,0));
    auto samples=imu.through(edge,interrupted);
    if (interrupted) break;
    if (samples.empty() || samples.back().stamp_ns<=edge ||
        (!first && samples.front().stamp_ns>=uint64_t(int64_t(h.left_ns)+std::min<int64_t>(offset,0))))
     throw std::runtime_error("IMU does not bracket the first/last camera timestamp");
    for (const auto& s:samples) {
     if (previous_imu && (s.stamp_ns<=previous_imu || s.stamp_ns-previous_imu>7500000))
      throw std::runtime_error("IMU sample gap >7.5ms: dataset incomplete");
     previous_imu=s.stamp_ns;
    }
    if (!first) first=h.left_ns;
    last=h.left_ns; sequence=h.sequence; left_ref=h.left_ref; right_ref=h.right_ref;
    writer.submit(std::move(pair),std::move(samples)); ++count;
    if (count%100==0) std::cout << "recorded_pairs=" << count << " elapsed_s=" << (last-first)*1e-9 << std::endl;
    if (seconds && last-first >= seconds*1000000000ULL) break;
   }
   imu.check();
  } catch (const std::exception& e) { error=e.what(); }
  if (camera.stop() && error.empty()) error="Camera helper failed during shutdown";
  imu.stop(); camera.report(); imu.report();
  writer.finish(error.empty(),error);
  return 0;
 } catch (const std::exception& e) { std::cerr << "Recording error: " << e.what() << '\n'; return 1; }
}
