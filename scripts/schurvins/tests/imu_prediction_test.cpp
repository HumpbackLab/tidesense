#include "imu_prediction.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
static void require(bool x,const char* msg){if(!x)throw std::runtime_error(msg);}
static ImuSample sample(uint64_t t,double ax=0,double wz=0){ImuSample s{};s.stamp_ns=t;s.accel={ax,0,9.80665};s.gyro={0,0,wz};return s;}
int main(){try{
 const uint64_t start=1000000000,dt=5000000;
 std::vector<PredictedState> out;int invalid=0;
 ImuPrediction predictor([&](const PredictedState&s,bool valid){if(valid)out.push_back(s);else ++invalid;},false,2000000000);
 for(int i=0;i<=10;++i)predictor.sample(sample(start+i*dt));
 require(out.empty(),"uninitialized poses leaked");
 PredictedState c;c.stamp_ns=start+22500000;predictor.correct(c); // split a 5 ms interval
 require(out.size()==1 && out.back().stamp_ns==start+10*dt,"correction must repropagate to latest sample");
 require(out.back().p.norm()<1e-10 && out.back().v.norm()<1e-10,"stationary gravity not cancelled");
 for(int i=11;i<=210;++i)predictor.sample(sample(start+i*dt,2));
 require(out.size()==201,"must output once per IMU sample");
 for(size_t i=1;i<out.size();++i)require(out[i].stamp_ns-out[i-1].stamp_ns==dt,"timestamps not 200Hz");
 require(std::abs(out.back().v.x()-1.995)<1e-9,"constant acceleration velocity wrong");
 auto n=out.size(); c.stamp_ns=start+1000*1000000ULL;c.p.x()=10;c.v.x()=3;c.ba.x()=2;
 predictor.correct(c);require(out.size()==n,"correction duplicated old output");
 predictor.sample(sample(start+211*dt,2));
 require(std::abs(out.back().v.x()-3)<1e-9 && std::abs(out.back().p.x()-10.165)<1e-9,"delayed correction/bias repropagation wrong");
 predictor.invalidate();predictor.sample(sample(start+212*dt));require(out.size()==n+1 && invalid==1,"loss did not suppress output");
 predictor.correct(c);predictor.sample(sample(start+213*dt));require(out.size()==n+1,"stale correction reactivated stream");
 predictor.finish();
 std::vector<PredictedState> spin;
 ImuPrediction rot([&](const PredictedState&s,bool valid){if(valid)spin.push_back(s);},false,2000000000);
 rot.sample(sample(start,0,1));c=PredictedState{};c.stamp_ns=start;rot.correct(c);
 for(int i=1;i<=200;++i)rot.sample(sample(start+i*dt,0,1));
 require(std::abs(Eigen::AngleAxisd(spin.back().q).angle()-1)<1e-9,"gyro rotation integration wrong");
 require(spin.back().p.norm()<1e-9,"yaw rotation introduced gravity drift");rot.finish();
 int valid_count=0,invalid_count=0;
 ImuPrediction gap([&](const PredictedState&,bool valid){valid?++valid_count:++invalid_count;},false,10000000);
 gap.sample(sample(start));c.stamp_ns=start;gap.correct(c);
 gap.sample(sample(start+dt));gap.sample(sample(start+2*dt));gap.sample(sample(start+3*dt));
 require(invalid_count==1,"stale correction not invalidated");
 gap.sample(sample(start+10*dt));c.stamp_ns=start+2*dt;gap.correct(c);gap.sample(sample(start+11*dt));
 require(valid_count==3,"gap should require fresh covered correction");gap.finish();
 int count=0;ImuPrediction threaded([&](const PredictedState&,bool valid){if(valid)++count;},true,2000000000);
 threaded.sample(sample(start));c=PredictedState{};c.stamp_ns=start;threaded.correct(c);
 for(int i=1;i<=200;++i)threaded.sample(sample(start+i*dt));threaded.finish();require(count==201,"threaded worker lost events");
 std::cout<<"PASS: stationary, rotation, acceleration, delayed fractional correction, bias, timestamps, gaps, staleness, worker drain\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
