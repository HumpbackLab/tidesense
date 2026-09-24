#include "recording.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <sstream>
#include <unistd.h>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<std::string> rows(const std::string& path) {
  std::ifstream file(path); std::vector<std::string> result; std::string s;
  while (std::getline(file, s)) if (!s.empty() && s[0]!='#') result.push_back(s);
  return result;
}
int main(int argc, char** argv) {
  if (argc!=3) return 2;
  const std::string dir=argv[2];
  try {
    auto calibration=YAML::LoadFile(argv[1]);
    calibration["live_camera_time_offset_ns"]=10461667;
    cv::Mat padded(240,328,CV_8UC1);
    cv::Mat texture=padded.colRange(0,320);
    cv::RNG rng(42); rng.fill(texture,cv::RNG::UNIFORM,0,256);
    cv::GaussianBlur(texture,texture,cv::Size(3,3),0.8);
    cv::Mat right=cv::Mat::zeros(texture.size(),CV_8UC1);
    texture.colRange(8,320).copyTo(right.colRange(0,312));
    LivePair pair; pair.left=texture; pair.right=right;
    pair.header.flags=CAMERA_FSIN;
    std::vector<ImuSample> samples;
    uint64_t last=95000000;
    VioRecording recorder(dir,calibration,argv[1]);
    YAML::Node meta; meta["six_face_applied"]=true; recorder.setImuMetadata(meta);
    for (uint64_t i=0;i<6;++i) {
      uint64_t t=1000000000+i*50000000;
      samples.clear();
      while (last<=t) {
        last+=5000000;
        ImuSample s{}; s.stamp_ns=last;
        s.accel[2]=calibration["imu_params"]["g"].as<double>();
        s.raw_accel[2]=9.123456789012345;
        samples.push_back(s);
      }
      pair.header.sequence=i;
      pair.header.left_ns=t-10461667; pair.header.right_ns=pair.header.left_ns+12345;
      recorder.submit(t,pair,samples);
      usleep(50000);
    }
    recorder.finish();
    require(access((dir+"/COMPLETE").c_str(),F_OK)==0,"No COMPLETE marker");
    require(access((dir+"/INCOMPLETE").c_str(),F_OK)!=0,"Unexpected incomplete marker");
    require(rows(dir+"/stereo.csv").size()==6,"Frame count mismatch");
    auto imu=rows(dir+"/imu.csv");
    require(imu.front().find("100000000,")==0,"Missing IMU startup history");
    require(imu.back().find("1255000000,")==0,"Missing final IMU interpolation edge");
    auto raw=rows(dir+"/imu-raw.csv");
    require(std::stod(raw[0].substr(raw[0].find_last_of(',')+1))==9.123456789012345,"Raw IMU precision lost");
    require(cv::norm(texture,cv::imread(dir+"/left/1000000000.pgm",cv::IMREAD_GRAYSCALE),cv::NORM_INF)==0,"Left image changed");
    require(cv::norm(right,cv::imread(dir+"/right/1000000000.pgm",cv::IMREAD_GRAYSCALE),cv::NORM_INF)==0,"Right image changed");
    require(YAML::LoadFile(dir+"/calibration.yaml")["live_camera_time_offset_ns"].as<int>()==10461667,"Original time shift lost");
    require(YAML::LoadFile(dir+"/replay.yaml")["live_camera_time_offset_ns"].as<int>()==0,"Double time correction");
    bool refused=false;
    try { VioRecording again(dir,calibration,argv[1]); } catch (...) { refused=true; }
    require(refused,"Existing directory overwritten");
    {
      VioRecording failed(dir+"-failed",calibration,argv[1]);
      failed.submit(1000000000,pair,{});
      refused=false;
      try { failed.finish(); } catch (...) { refused=true; }
      require(refused,"Invalid IMU batch accepted");
    }
    require(access((dir+"-failed/COMPLETE").c_str(),F_OK)!=0,"Failed recording marked complete");
    require(access((dir+"-failed/INCOMPLETE").c_str(),F_OK)==0,"Failed recording lacks marker");
    std::cout << "RECORDING TEST PASS: images, timestamps, IMU precision, snapshots, completion/error handling\n";
  } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
