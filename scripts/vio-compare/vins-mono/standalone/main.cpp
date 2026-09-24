#include "estimator.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>

struct Imu { double t; Eigen::Vector3d w,a; };
struct Frame { double t; std::string filename; };
struct Track { int id, age; cv::Point2f p; };

static std::vector<std::string> split(const std::string &s) {
  std::vector<std::string> v; std::stringstream ss(s); std::string part;
  while(std::getline(ss,part,',')) v.push_back(part);
  return v;
}
static std::vector<Imu> readImu(const std::string &path) {
  std::ifstream in(path); std::vector<Imu> out; std::string line;
  while(std::getline(in,line)) { if(line.empty()||line[0]=='#')continue;
    auto f=split(line); if(f.size()<7)continue;
    out.push_back({std::stod(f[0])*1e-9,
      {std::stod(f[1]),std::stod(f[2]),std::stod(f[3])},
      {std::stod(f[4]),std::stod(f[5]),std::stod(f[6])}});
  } return out;
}
static std::vector<Frame> readFrames(const std::string &path) {
  std::ifstream in(path); std::vector<Frame> out; std::string line;
  while(std::getline(in,line)) { if(line.empty()||line[0]=='#')continue;
    auto f=split(line); if(f.empty())continue;
    out.push_back({std::stod(f[0])*1e-9,f[0]+".pgm"});
  } return out;
}

int main(int argc,char **argv) {
  if(argc<3) { std::cerr<<"usage: vins-mono-offline RECORD_DIR OUTPUT_CSV [MAX_FRAMES]\n";return 2; }
  const std::string dir=argv[1]; const int max_frames=argc>3?std::stoi(argv[3]):1000000;
  auto imu=readImu(dir+"/imu.csv"); auto frames=readFrames(dir+"/frames.csv");
  if(imu.empty()||frames.empty()) {std::cerr<<"input CSV empty\n";return 2;}
  // Kalibr T_cam_imu inverted: RIC/TIC are imu_T_cam. Source:
  // cali/tidesense-imucam3-kalibr-20260924-101247/imucam-camchain-imucam.yaml
  Eigen::Matrix3d ric; ric <<
    -0.9999975263265998, 0.0014013565129626768, 0.001727292854493307,
    -0.001402572693579215,-0.9999987692290236,-0.0007030862517255539,
     0.0017263054540933636,-0.0007055071663113217,0.9999982610630468;
  RIC={ric}; TIC={Eigen::Vector3d(-0.0376222418,0.00224501017,0.0169882601)};
  VINS_RESULT_PATH=argv[2];
  // Upstream constructs Estimator in static storage; clearState expects zeroed pointers.
  static Estimator est; est.setParameter();
  cv::Mat K=(cv::Mat_<double>(3,3)<<406.8500365550254,0,364.3910090114785,0,406.5040196131933,199.20471812656527,0,0,1);
  cv::Mat D=(cv::Mat_<double>(1,4)<<-0.3122959552590484,0.0790866692026351,0.0019470805244765283,-0.0009124507326154742);
  std::ofstream poses(argv[2]); poses<<"#timestamp_ns,tx,ty,tz,qx,qy,qz,qw,features,initialized,processing_ms\n";
  size_t ii=0; double current=-1; Imu last=imu.front();
  cv::Mat prev; std::vector<Track> tracks; int next_id=0, initialized_count=0;
  auto start=std::chrono::steady_clock::now();
  int processed=0;
  for(const auto &fr:frames) {
    if(processed>=max_frames)break;
    cv::Mat img=cv::imread(dir+"/left/"+fr.filename,cv::IMREAD_GRAYSCALE);
    if(img.empty()) { std::cerr<<"missing image "<<fr.filename<<"\n"; break; }
    auto frame_start=std::chrono::steady_clock::now();
    if(!prev.empty()&&!tracks.empty()) {
      std::vector<cv::Point2f> p0,p1; std::vector<uchar> good; std::vector<float> err;
      for(auto &tr:tracks)p0.push_back(tr.p);
      cv::calcOpticalFlowPyrLK(prev,img,p0,p1,good,err,cv::Size(21,21),3);
      std::vector<Track> keep; std::vector<cv::Point2f> u0,u1;
      for(size_t i=0;i<tracks.size();i++) if(good[i]&&p1[i].x>=2&&p1[i].y>=2&&p1[i].x<img.cols-2&&p1[i].y<img.rows-2) {
        keep.push_back({tracks[i].id,tracks[i].age+1,p1[i]});u0.push_back(p0[i]);u1.push_back(p1[i]); }
      if(keep.size()>=8) {
        std::vector<uchar> inlier; cv::findFundamentalMat(u0,u1,cv::FM_RANSAC,1.0,0.99,inlier);
        std::vector<Track> filtered;for(size_t i=0;i<keep.size();i++)if(inlier[i])filtered.push_back(keep[i]);
        keep.swap(filtered);
      } tracks.swap(keep);
    }
    std::sort(tracks.begin(),tracks.end(),[](const Track&a,const Track&b){return a.age>b.age;});
    cv::Mat mask(img.size(),CV_8UC1,cv::Scalar(255));
    std::vector<Track> spaced;
    for(const auto &tr:tracks)if(mask.at<uchar>(tr.p)==255){spaced.push_back(tr);cv::circle(mask,tr.p,25,cv::Scalar(0),-1);}
    tracks.swap(spaced);
    if(tracks.size()<150){std::vector<cv::Point2f> found;
      cv::goodFeaturesToTrack(img,found,150-tracks.size(),0.01,25,mask);
      for(auto &p:found)tracks.push_back({next_id++,1,p});}
    std::vector<cv::Point2f> pix;for(auto &tr:tracks)pix.push_back(tr.p);
    std::vector<cv::Point2f> norm;cv::undistortPoints(pix,norm,K,D);
    std::map<int,std::vector<std::pair<int,Eigen::Matrix<double,7,1>>>> obs;
    for(size_t i=0;i<tracks.size();i++)if(tracks[i].age>1){
      Eigen::Matrix<double,7,1> x;x<<norm[i].x,norm[i].y,1,pix[i].x,pix[i].y,0,0;
      obs[tracks[i].id].push_back({0,x});}
    // frames.csv timestamp is already camera time aligned to IMU; no second shift.
    while(ii<imu.size()&&imu[ii].t<=fr.t){auto &s=imu[ii];
      if(current<0)current=s.t;
      est.processIMU(std::max(0.0,s.t-current),s.a,s.w); current=s.t;last=s;ii++;}
    if(ii<imu.size()&&current>=0&&current<fr.t){auto &next=imu[ii];
      double alpha=(fr.t-current)/(next.t-current);
      est.processIMU(fr.t-current,last.a*(1-alpha)+next.a*alpha,last.w*(1-alpha)+next.w*alpha);current=fr.t;}
    std_msgs::Header hdr;hdr.stamp.value=fr.t;est.processImage(obs,hdr);
    bool ok=est.solver_flag==Estimator::NON_LINEAR;if(ok)initialized_count++;
    Eigen::Quaterniond q(est.Rs[est.frame_count]);auto p=est.Ps[est.frame_count];
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frame_start).count();
    poses<<std::fixed<<std::setprecision(9)<<fr.t*1e9<<','<<p.x()<<','<<p.y()<<','<<p.z()<<','<<q.x()<<','<<q.y()<<','<<q.z()<<','<<q.w()<<','<<obs.size()<<','<<ok<<','<<ms<<'\n';
    prev=img;processed++;
    if(processed%25==0)std::cerr<<"frame="<<processed<<" features="<<obs.size()<<" initialized="<<ok<<" t_ms="<<ms<<'\n';
  }
  double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  std::cerr<<"complete frames="<<processed<<" imu_consumed="<<ii<<" initialized_frames="<<initialized_count<<" wall_s="<<seconds<<'\n';
  return processed?0:1;
}
