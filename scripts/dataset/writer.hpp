#pragma once
// Standalone sensor recorder. Lossless residual+zstd, no estimator or ROS.
#include "tide_camera.hpp"
#include "live_imu.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#define XXH_NAMESPACE ZSTD_
#include <common/xxhash.h>
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
uint64_t threadCpuNow() {
 timespec t{}; if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t)) throw std::runtime_error("thread clock_gettime failed");
 return uint64_t(t.tv_sec)*1000000000 + t.tv_nsec;
}
constexpr int RECORDING_COMPRESSION_LEVEL=-100;
void open(std::ofstream& f, const std::string& path) {
 f.exceptions(std::ios::failbit | std::ios::badbit);
 f.open(path, std::ios::binary); f << std::setprecision(17);
}
void yaml(const std::string& path, const YAML::Node& n) {
 YAML::Emitter e; e.SetDoublePrecision(17); e << n;
 if (!e.good()) throw std::runtime_error("YAML serialization failed");
 std::ofstream f; open(f,path); f << e.c_str() << '\n'; f.close();
}
inline void snapshot(const std::string& from, const std::string& to) {
 std::ifstream in(from, std::ios::binary);
 if (!in) throw std::runtime_error("Cannot read " + from);
 std::ofstream out; open(out,to); out << in.rdbuf(); out.close();
}
// One sequential image stream keeps SD metadata work out of the capture path.
class Writer {
 struct Batch { LivePair pair; std::vector<ImuSample> imu; };
 struct Encoded { CameraHeader header{}; uint64_t digest=0; std::vector<uint8_t> bytes; std::vector<ImuSample> imu; };
 std::string dir_, error_;
 std::ofstream images_, imu_;
 int verify_fd_=-1;
 std::unique_ptr<XXH64_state_t,decltype(&XXH64_freeState)> expected_hash_{XXH64_createState(),XXH64_freeState};
 std::mutex mutex_;
 std::condition_variable raw_ready_, encoded_ready_;
 std::deque<Batch> queue_;
 std::deque<Encoded> encoded_;
 std::thread compressor_thread_, disk_thread_;
 bool stop_ = false, compression_done_ = false;
 size_t capacity_, pending_=0, high_water_=0, raw_high_water_=0, encoded_high_water_=0;
 uint64_t pairs_=0, samples_=0, first_=0, last_=0, imu_first_=0, imu_last_=0;
 uint64_t max_cam_gap_=0, max_imu_gap_=0, compressed_bytes_=0, compression_ns_=0, compression_cpu_ns_=0, image_write_ns_=0;
 uint64_t compression_max_ns_=0, image_write_max_ns_=0, image_write_cpu_ns_=0, encoded_pairs_=0;
 uint64_t finalize_drain_ns_=0, data_sync_ns_=0;
 tide_cam_config camera_config_;
 void space() {
  struct statvfs fs{};
  if (statvfs(dir_.c_str(),&fs)) throw std::runtime_error("Cannot query free space");
  if (fs.f_flag & ST_SYNCHRONOUS) throw std::runtime_error("SD is mounted sync; remount async");
  if (uint64_t(fs.f_bavail)*fs.f_frsize < 64ULL*1024*1024)
   throw std::runtime_error("Less than 64 MiB free on SD");
 }
 void fail(const std::string& reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (error_.empty()) error_=reason;
  stop_=true; queue_.clear(); encoded_.clear();
  raw_ready_.notify_all(); encoded_ready_.notify_all();
 }
 void compress() {
  try {
   std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> compressor(ZSTD_createCCtx(), ZSTD_freeCCtx);
   if (!compressor) throw std::runtime_error("Cannot allocate zstd compressor");
   auto parameter=[&](ZSTD_cParameter key,int value) {
    size_t result=ZSTD_CCtx_setParameter(compressor.get(),key,value);
    if (ZSTD_isError(result)) throw std::runtime_error(ZSTD_getErrorName(result));
   };
   // The pinned, statically linked zstd 1.5.7 disables literal Huffman coding
   // at negative levels unless requested explicitly. Sensor residuals benefit
   // from it: avoid spending CPU on LZ searches without inflating SD traffic.
   parameter(ZSTD_c_compressionLevel,RECORDING_COMPRESSION_LEVEL);
   parameter(ZSTD_c_literalCompressionMode,ZSTD_ps_enable);
   parameter(ZSTD_c_checksumFlag,1);
   std::vector<uint8_t> pixels(sizeof(CameraHeader)+2*size_t(camera_config_.mode.width)*camera_config_.mode.height);
   std::vector<uint8_t> compressed(ZSTD_compressBound(pixels.size()));
   std::vector<uint8_t> restored(pixels.size());
   std::unique_ptr<ZSTD_DCtx,decltype(&ZSTD_freeDCtx)> decoder(ZSTD_createDCtx(),ZSTD_freeDCtx);
   if (!decoder) throw std::runtime_error("Cannot allocate frame verification context");
   while (true) {
    Batch b;
    {
     std::unique_lock<std::mutex> lock(mutex_);
     raw_ready_.wait(lock,[&]{ return stop_ || !queue_.empty(); });
     if (!error_.empty() || queue_.empty()) break;
     b=std::move(queue_.front()); queue_.pop_front();
    }
    const auto h=b.pair.header;
    const uint64_t begin=now(), cpuBegin=threadCpuNow();
    std::memcpy(pixels.data(),&h,sizeof(h));
    size_t position=sizeof(h);
    for (const cv::Mat* im:{&b.pair.left,&b.pair.right}) {
     for (int y=0;y<im->rows;++y) {
      const uint8_t* row=im->ptr(y); uint8_t* dst=pixels.data()+position;
      dst[0]=row[0];
      for (int x=1;x<im->cols;++x) dst[x]=uint8_t(row[x]-row[x-1]);
      position+=im->cols;
     }
    }
    const size_t bytes=ZSTD_compress2(compressor.get(),compressed.data(),compressed.size(),pixels.data(),pixels.size());
    if (ZSTD_isError(bytes)) throw std::runtime_error(ZSTD_getErrorName(bytes));
    const uint64_t digest=XXH64(compressed.data(),bytes,0);
    const size_t decoded=ZSTD_decompressDCtx(decoder.get(),restored.data(),restored.size(),compressed.data(),bytes);
    if (ZSTD_isError(decoded) || decoded!=pixels.size() || std::memcmp(restored.data(),pixels.data(),pixels.size()))
     throw std::runtime_error("Encoded frame verification failed before queue, sequence="+std::to_string(h.sequence)+": "+
      (ZSTD_isError(decoded)?ZSTD_getErrorName(decoded):"pixel bytes differ"));
    Encoded packet; packet.header=h; packet.imu=std::move(b.imu);
    packet.digest=digest;
    packet.bytes.assign(compressed.begin(),compressed.begin()+bytes);
    if (XXH64(packet.bytes.data(),packet.bytes.size(),0)!=packet.digest)
     throw std::runtime_error("Encoded packet copy verification failed, sequence="+std::to_string(h.sequence));
    const uint64_t duration=now()-begin;
    compression_ns_+=duration; compression_max_ns_=std::max(compression_max_ns_,duration);
    compression_cpu_ns_+=threadCpuNow()-cpuBegin; ++encoded_pairs_;
    {
     std::lock_guard<std::mutex> lock(mutex_);
     if (!error_.empty()) break;
     encoded_.push_back(std::move(packet));
     encoded_high_water_=std::max(encoded_high_water_,encoded_.size());
    }
    encoded_ready_.notify_one();
   }
  } catch (const std::exception& e) { fail(e.what()); }
  { std::lock_guard<std::mutex> lock(mutex_); compression_done_=true; }
  encoded_ready_.notify_all();
 }
 void write() {
  try {
   std::vector<uint8_t> readback(ZSTD_compressBound(sizeof(CameraHeader)+2*size_t(camera_config_.mode.width)*camera_config_.mode.height));
   while (true) {
    Encoded packet;
    {
     std::unique_lock<std::mutex> lock(mutex_);
     encoded_ready_.wait(lock,[&]{ return compression_done_ || !encoded_.empty() || !error_.empty(); });
     if (!error_.empty() || encoded_.empty()) break;
     packet=std::move(encoded_.front()); encoded_.pop_front();
    }
    if (pairs_%20==0) space();
    const auto& h=packet.header;
    // One disk thread owns both streams and preserves a contiguous prefix.
    for (const auto& s:packet.imu) {
     imu_ << s.stamp_ns;
     for (double v:s.gyro) imu_ << ',' << v;
     for (double v:s.accel) imu_ << ',' << v;
     imu_ << '\n';
     if (!samples_) imu_first_=s.stamp_ns;
     if (imu_last_) max_imu_gap_=std::max(max_imu_gap_,s.stamp_ns-imu_last_);
     imu_last_=s.stamp_ns; ++samples_;
    }
    if (XXH64(packet.bytes.data(),packet.bytes.size(),0)!=packet.digest)
     throw std::runtime_error("Encoded packet queue verification failed, sequence="+std::to_string(h.sequence));
    const uint32_t length=static_cast<uint32_t>(packet.bytes.size());
    XXH64_update(expected_hash_.get(),&length,sizeof(length));
    XXH64_update(expected_hash_.get(),packet.bytes.data(),packet.bytes.size());
    const uint64_t begin=now(),cpuBegin=threadCpuNow();
    images_.write(reinterpret_cast<const char*>(&length),sizeof(length));
    images_.write(reinterpret_cast<const char*>(packet.bytes.data()),packet.bytes.size());
    images_.flush();
    // Read the just-written dirty pages, not the whole file or the SD again.
    // This checks the file bytes independently of the source packet memory.
    const off_t offset=17+compressed_bytes_;
    uint32_t storedLength=0;
    if(pread(verify_fd_,&storedLength,sizeof(storedLength),offset)!=ssize_t(sizeof(storedLength)) || storedLength!=length)
     throw std::runtime_error("Written frame length verification failed, sequence="+std::to_string(h.sequence));
    size_t received=0;
    while(received<length) {
     ssize_t n=pread(verify_fd_,readback.data()+received,length-received,offset+sizeof(length)+received);
     if(n<0 && errno==EINTR)continue;
     if(n<=0)throw std::runtime_error("Written frame readback failed, sequence="+std::to_string(h.sequence));
     received+=size_t(n);
    }
    if(XXH64(readback.data(),length,0)!=packet.digest)
     throw std::runtime_error("Written frame bytes differ from verified encoding, sequence="+std::to_string(h.sequence));
    const uint64_t duration=now()-begin;
    image_write_ns_+=duration; image_write_max_ns_=std::max(image_write_max_ns_,duration);
    image_write_cpu_ns_+=threadCpuNow()-cpuBegin;
    if (XXH64(packet.bytes.data(),packet.bytes.size(),0)!=packet.digest)
     throw std::runtime_error("Encoded packet changed while writing, sequence="+std::to_string(h.sequence));
    compressed_bytes_+=packet.bytes.size()+sizeof(length);
    if (!pairs_) first_=h.left_ns;
    if (last_) max_cam_gap_=std::max(max_cam_gap_,h.left_ns-last_);
    last_=h.left_ns; ++pairs_;
    { std::lock_guard<std::mutex> lock(mutex_); --pending_; }
   }
  } catch (const std::exception& e) { fail(e.what()); }
 }
 void join() {
  { std::lock_guard<std::mutex> lock(mutex_); stop_=true; }
  raw_ready_.notify_all();
  if (compressor_thread_.joinable()) compressor_thread_.join();
  if (disk_thread_.joinable()) disk_thread_.join();
 }
 void syncFile(const std::string& name,bool optional=false) {
  const std::string path=dir_+"/"+name;
  int fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC);
  if(fd<0 && optional && errno==ENOENT)return;
  if(fd<0)throw std::runtime_error("Cannot open recording file for sync: "+name+": "+strerror(errno));
  int result=fsync(fd),error=errno;::close(fd);
  if(result)throw std::runtime_error("Cannot sync recording file: "+name+": "+strerror(error));
 }
 void metadata(bool complete) {
  YAML::Node n;
  n["schema"]="tidesense-dataset-v2"; n["complete"]=complete;
  n["imu_value_domain"]="raw_si";
  n["clock"]="CLOCK_MONOTONIC nanoseconds";
  n["camera_timestamp"]="raw VI arrival PTS; not exposure time; no time offset applied";
  n["stereo"]="FSIN paired, original sensor orientation, mono8";
  n["width"]=camera_config_.mode.width; n["height"]=camera_config_.mode.height;
  n["camera_source"]="tide_cam";
  n["scope"]="contiguous pairs after camera/IMU warmup; IMU starts before first pair and ends after last pair";
  n["camera_hz_nominal"]=20; n["imu_hz_nominal"]=200;
  n["pairs"]=pairs_; n["imu_samples"]=samples_;
  n["first_camera_ns"]=first_; n["last_camera_ns"]=last_;
  n["first_imu_ns"]=imu_first_; n["last_imu_ns"]=imu_last_;
  n["camera_hz_measured"]=last_>first_?(pairs_-1)*1e9/(last_-first_):0;
  n["imu_hz_measured"]=imu_last_>imu_first_?(samples_-1)*1e9/(imu_last_-imu_first_):0;
  n["max_camera_gap_ms"]=max_cam_gap_*1e-6; n["max_imu_gap_ms"]=max_imu_gap_*1e-6;
  n["writer_queue_capacity"]=capacity_; n["writer_queue_high_water"]=high_water_;
  n["raw_queue_high_water"]=raw_high_water_; n["encoded_queue_high_water"]=encoded_high_water_;
  n["writer_error"]=error_;
  n["encoding_verified"]=complete;
  n["cached_write_verified"]=complete;
  n["expected_frames_xxh64"]=uint64_t(XXH64_digest(expected_hash_.get()));
  n["archive_verified"]=false;
  n["verification"]="encode roundtrip, queue digests and immediate cached write readback; full archive validation required on host";
  n["finalize_drain_ms"]=finalize_drain_ns_*1e-6;
  n["data_sync_ms"]=data_sync_ns_*1e-6;
  n["compression"]="horizontal uint8 residual per row + zstd fast, per-pair checksum";
  n["compression_level"]=RECORDING_COMPRESSION_LEVEL;
  n["literal_huffman"]=true; n["encoded_pairs"]=encoded_pairs_;
  n["compressed_image_bytes"]=compressed_bytes_;
  n["compression_ms_per_pair"]=encoded_pairs_?compression_ns_*1e-6/encoded_pairs_:0;
  n["compression_cpu_ms_per_pair"]=encoded_pairs_?compression_cpu_ns_*1e-6/encoded_pairs_:0;
  n["image_write_ms_per_pair"]=pairs_?image_write_ns_*1e-6/pairs_:0;
  n["image_write_cpu_ms_per_pair"]=pairs_?image_write_cpu_ns_*1e-6/pairs_:0;
  n["compression_max_ms"]=compression_max_ns_*1e-6;
  n["image_write_max_ms"]=image_write_max_ns_*1e-6;
  yaml(dir_+"/metadata.yaml",n);
 }
 public:
 struct Statistics { size_t pending,raw,encoded; };
 Statistics statistics() {
  std::lock_guard<std::mutex> lock(mutex_);
  return {pending_,queue_.size(),encoded_.size()};
 }
 Writer(const std::string& dir, size_t capacity,const tide_cam_config& config):dir_(dir),capacity_(capacity),camera_config_(config) {
  if(!expected_hash_)throw std::runtime_error("Cannot allocate recording digest");
  XXH64_reset(expected_hash_.get(),0);
  if (mkdir(dir.c_str(),0755)) throw std::runtime_error("Output must be a NEW directory: "+dir+": "+strerror(errno));
  { std::ofstream f; open(f,dir+"/INCOMPLETE"); f << "Capture not finalized\n"; }
  space(); open(images_,dir+"/frames.bin"); open(imu_,dir+"/imu.csv");
  images_ << "TIDESENSE_ZST_V1\n";
  XXH64_update(expected_hash_.get(),"TIDESENSE_ZST_V1\n",17);
  imu_ << "#timestamp_ns,gx,gy,gz,ax,ay,az\n";
  metadata(false);
  try {
   verify_fd_=::open((dir_+"/frames.bin").c_str(),O_RDONLY|O_CLOEXEC);
   if(verify_fd_<0)throw std::runtime_error("Cannot open written-frame verifier");
   compressor_thread_=std::thread(&Writer::compress,this);
   disk_thread_=std::thread(&Writer::write,this);
  } catch (...) { join(); if(verify_fd_>=0)::close(verify_fd_); throw; }
 }
 ~Writer() { join(); if(verify_fd_>=0)::close(verify_fd_); }
 void submit(LivePair&& pair, std::vector<ImuSample>&& samples) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!error_.empty()) throw std::runtime_error(error_);
  if (stop_) throw std::runtime_error("Recording writer is stopping");
  if (pending_>=capacity_) throw std::runtime_error("Recording queue full: compression/write throughput cannot sustain capture; dataset INCOMPLETE");
  queue_.push_back({std::move(pair),std::move(samples)});
  ++pending_; high_water_=std::max(high_water_,pending_);
  raw_high_water_=std::max(raw_high_water_,queue_.size()); raw_ready_.notify_one();
 }
 void finish(bool complete, const std::string& reason) {
  const uint64_t drainBegin=now();
  join();
  finalize_drain_ns_=now()-drainBegin;
  if (!reason.empty() && error_.empty()) error_=reason;
  try { images_.close(); } catch (const std::exception& e) { if (error_.empty()) error_=e.what(); }
  try { imu_.close(); } catch (const std::exception& e) { if (error_.empty()) error_=e.what(); }
  complete=complete && error_.empty() && pairs_>0;
  const uint64_t syncBegin=now();
  try {
   syncFile("frames.bin");syncFile("imu.csv");syncFile("INCOMPLETE");
   for(const char* name:{"camera-config.yaml","imu-source.yaml","calibration.yaml"})syncFile(name,true);
  }
  catch(const std::exception& e) {if(error_.empty())error_=e.what();complete=false;}
  data_sync_ns_=now()-syncBegin;
  // COMPLETE means drained and durable. Encoding was checked while recording;
  // stored bytes are fully validated on the host before calibration/export.
  metadata(complete);syncFile("metadata.yaml");
  if (complete) {
   if (rename((dir_+"/INCOMPLETE").c_str(),(dir_+"/COMPLETE").c_str())) throw std::runtime_error("Cannot finalize marker");
   try {
    const size_t slash=dir_.find_last_of('/');
    const std::string parent=slash==std::string::npos?".":slash==0?"/":dir_.substr(0,slash);
    for(const auto& path:{dir_,parent}) {
     int fd=::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
     if(fd<0)throw std::runtime_error("Cannot open recording directory for sync");
     int result=fsync(fd),error=errno;::close(fd);
     if(result)throw std::runtime_error("Cannot sync recording directory: "+std::string(strerror(error)));
    }
   } catch(...) {
    // A failed durable commit must not leave a success marker behind.
    rename((dir_+"/COMPLETE").c_str(),(dir_+"/INCOMPLETE").c_str());throw;
   }
  }
  std::cout << (complete?"COMPLETE":"INCOMPLETE") << " pairs=" << pairs_ << " imu=" << samples_
            << " writer_queue_high_water=" << high_water_ << '/' << capacity_ << '\n';
  if (!complete) throw std::runtime_error(error_.empty()?"No frames recorded":error_);
 }
};
}
