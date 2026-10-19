#include "imu_intrinsics.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

static void require(bool value) {
 if (!value) throw std::runtime_error("IMU intrinsics test failed");
}
int main(int argc, char** argv) {
 if (argc==5 && std::string(argv[1])=="--transform") {
  const auto calibration=ImuIntrinsics::parse(YAML::LoadFile(argv[2])["imu_params"]);
  require(calibration.applies());
  std::ifstream input(argv[3]); std::ofstream output(argv[4]);
  require(bool(input) && bool(output));
  output << "#timestamp_ns,gx,gy,gz,ax,ay,az\n" << std::setprecision(17);
  std::string line;
  while (std::getline(input,line)) {
   if (line.empty() || line[0]=='#') continue;
   std::stringstream row(line); std::array<std::string,7> fields;
   for (auto& field:fields) require(bool(std::getline(row,field,',')));
   ImuSample sample{}; sample.stamp_ns=std::stoull(fields[0]);
   for (int i=0;i<3;++i) { sample.gyro[i]=std::stod(fields[i+1]); sample.accel[i]=std::stod(fields[i+4]); }
   const auto corrected=calibration.apply(sample);
   output << fields[0];
   for (double v:corrected.gyro) output << ',' << v;
   for (double v:corrected.accel) output << ',' << v;
   output << '\n';
  }
  require(bool(output));
  return 0;
 }
 require(argc==1);
 ImuSample input{}; input.stamp_ns=123; input.gyro={{1,2,3}}; input.accel={{1,2,3}};
 const auto identity=ImuIntrinsics::parse(YAML::Node());
 require(!identity.applies() && identity.apply(input).accel==input.accel);
 auto params=YAML::Load(R"(
input_domain: raw_si
accel_inverse_matrix: [[2,0.1,0],[0,3,0.2],[0.3,0,4]]
accel_offset: [0.2,0,-0.3]
gyro_inverse_matrix: [[1.1,0.02,0.03],[-0.04,0.9,0.05],[0.06,-0.07,1.2]]
gyro_accel_matrix: [[0.1,0,0],[0,0.2,0],[0,0,0.3]]
gyro_offset: [-0.01,0.02,0.03]
)");
 auto calibration=ImuIntrinsics::parse(params);
 const auto corrected=calibration.apply(input);
 const std::array<double,3> expected_a{{2.4,6.6,12}}, expected_w{{1.46,3.25,7.15}};
 for (int i=0;i<3;++i) {
  require(std::abs(corrected.accel[i]-expected_a[i])<1e-13);
  require(std::abs(corrected.gyro[i]-expected_w[i])<1e-13);
 }
 require(corrected.stamp_ns==input.stamp_ns);
 params["imu_input_is_corrected"]=true;
 calibration=ImuIntrinsics::parse(params);
 require(!calibration.applies());
 require(calibration.apply(corrected).accel==corrected.accel && calibration.apply(corrected).gyro==corrected.gyro);
 auto rejects=[](const std::string& text) {
  bool rejected=false;
  try { ImuIntrinsics::parse(YAML::Load(text)); } catch (const std::exception&) { rejected=true; }
  require(rejected);
 };
 rejects("accel_inverse_matrix: [[1,0,0],[0,1,0],[0,0,1]]");
 for (const char* key:{"accel_inverse_matrix","gyro_inverse_matrix"}) {
  for (const char* value:{"[[1,0],[0,1]]", "[[1,0,0],[0,1,0],[0,0]]", "[[.nan,0,0],[0,1,0],[0,0,1]]",
                          "[[1,0,0],[0,.inf,0],[0,0,1]]", "[[1,0,0],[1,0,0],[0,0,1]]", "[[-1,0,0],[0,1,0],[0,0,1]]"})
   rejects(std::string("input_domain: raw_si\n")+key+": "+value);
 }
 rejects("input_domain: raw_si\naccel_offset: [0, .nan, 0]");
 rejects("input_domain: raw_si\ngyro_accel_matrix: [[0,0],[0,0]]");
 std::cout << "PASS single-stage IMU: accel/gyro matrices, affine offsets, cross-sensitivity, timestamp, corrected-input bypass and invalid models\n";
}
