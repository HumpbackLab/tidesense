#include "writer.hpp"
#include "session.hpp"
int main(int argc,char** argv) {
 if(argc>1 && std::string(argv[1])=="--file-server")return datasetFileServer(argc-1,argv+1);
 if (argc>1 && std::string(argv[1])=="--session") return datasetSession(argc-1,argv+1);
 if (argc>1 && std::string(argv[1])=="--relay") return datasetRelay(argc-1,argv+1);
 std::string dir,device="auto",calibration;
 tide_cam_config camera_config; tide_cam_config_init(&camera_config);
 camera_config.queue=TIDE_CAM_FIFO; camera_config.buffer_count=8;
 uint64_t seconds=0,capacity=64;
 try {
  for (int i=1;i<argc;++i) {
   std::string option=argv[i];
   if (option=="--help") {
    std::cout << "tidesense_record --output NEW_DIR [--seconds N] [--width W] [--height H]\n"
     "  [--imu auto|/dev/iio:deviceN] [--calibration SCHUR.yaml] [--queue-pairs 1..128]\n"
     "Hardware-synced stereo / 200Hz IMU via tide_cam. Camera settings: TIDE_CAM_CONFIG_PATH.\n"
     "Session: --session --http-token HEX32 [--socket PATH] [--imu DEVICE] [--width W] [--height H]\n"
     "SSH transport: --relay SOCKET (for Unix socket connections, not capture)\n"
     "Use the same raw recording for datasets, calibration and VIO replay. Ctrl-C stops and flushes.\n"; return 0;
   }
   if (++i>=argc) throw std::runtime_error("Missing option value");
   std::string value=argv[i];
   if (option=="--output") dir=value;
   else if (option=="--imu") device=value;
   else if (option=="--calibration") calibration=value;
   else if (option=="--seconds" || option=="--queue-pairs" || option=="--width" || option=="--height") {
    if (value.empty() || value.find_first_not_of("0123456789")!=std::string::npos) throw std::runtime_error("Expected unsigned integer");
    uint64_t n=std::stoull(value);
    if (option=="--seconds") { if(n>86400) throw std::runtime_error("seconds must be 0..86400"); seconds=n; }
    else if (option=="--queue-pairs") { if(n<1 || n>128) throw std::runtime_error("queue-pairs must be 1..128"); capacity=n; }
    else {
     if (!n || n>UINT32_MAX) throw std::runtime_error("Invalid camera dimension");
     if (option=="--width") camera_config.mode.width=uint32_t(n);
     else camera_config.mode.height=uint32_t(n);
    }
   } else throw std::runtime_error("Unknown option: "+option);
  }
  if (dir.empty()) throw std::runtime_error("--output NEW_DIR is required");
  loadCameraSettings(camera_config);
  int64_t offset=0;
  if (!calibration.empty()) {
   auto n=YAML::LoadFile(calibration);
   if (!n["cameras"] || n["cameras"].size()!=2) throw std::runtime_error("Expected stereo SchurVINS calibration");
   for (const auto& c:n["cameras"]) {
    const auto model=c["camera"];
    if (!model || model["image_width"].as<uint32_t>()!=camera_config.mode.width ||
        model["image_height"].as<uint32_t>()!=camera_config.mode.height)
     throw std::runtime_error("Calibration dimensions differ from capture mode");
   }
   if (n["live_camera_time_offset_ns"]) offset=n["live_camera_time_offset_ns"].as<int64_t>();
   if (offset < -1000000000LL || offset>1000000000LL) throw std::runtime_error("Invalid camera time offset");
  }
  struct sigaction sa{}; sa.sa_handler=interrupt;
  sigaction(SIGINT,&sa,nullptr); sigaction(SIGTERM,&sa,nullptr);
  Writer writer(dir,capacity,camera_config);
  yaml(dir+"/camera-config.yaml",cameraConfiguration(camera_config));
  if (!calibration.empty()) snapshot(calibration,dir+"/calibration.yaml");
  // As in live VIO: camera startup first, then enable buffered IIO acquisition.
  // FSIN itself keeps the IMU IRQ active even before its IIO buffer is enabled.
  TideCamera camera(camera_config);
  LivePair pair;
  if (!camera.next(pair,interrupted)) throw std::runtime_error("Interrupted during camera startup");
  LiveImu imu(device);
  yaml(dir+"/imu-source.yaml",imu.recordingMetadata());
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
