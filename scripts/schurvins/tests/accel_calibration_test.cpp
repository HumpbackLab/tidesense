#include "accel_calibration.hpp"
#include <iostream>
#include <stdexcept>
static void require(bool ok) { if (!ok) throw std::runtime_error("calibration test failed"); }
int main() {
 auto n=YAML::Load(R"({"schema_version":1,"board":"tidesense","sensor":"sc7u22",
 "frame":"imu_chip_xyz","sampling_frequency_hz":200,"accel_scale_m_s2_per_lsb":0.004788403,
 "bias_m_s2":[-0.46,0.16,-0.85],"correction_scale":[0.995,0.994,0.988]})");
 auto c=AccelCalibration::parse(n,0.004788403);
 for(int i=0;i<3;++i) {
  require(std::abs(c.apply(i,c.bias[i]))<1e-12);
  for(double g:{-9.80665,9.80665})
   require(std::abs(c.apply(i,g/c.scale[i]+c.bias[i])-g)<1e-12);
 }
 auto bad=[&](YAML::Node node,double scale) {
  bool rejected=false;
  try { AccelCalibration::parse(node,scale); } catch(const std::exception&) { rejected=true; }
  require(rejected);
 };
 bad(n,0.0023942015);
 auto a=YAML::Clone(n);a["correction_scale"][0]=-1;bad(a,0.004788403);
 a=YAML::Clone(n);a["bias_m_s2"][1]=".nan";bad(a,0.004788403);
 a=YAML::Clone(n);a["frame"]="camera";bad(a,0.004788403);
 a=YAML::Clone(n);a.remove("bias_m_s2");bad(a,0.004788403);
 std::cout<<"PASS six-face correction: signs, axes, scale and invalid metadata\n";
}
