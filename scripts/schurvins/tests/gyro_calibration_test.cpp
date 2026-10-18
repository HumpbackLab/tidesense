#include "gyro_calibration.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok) {
 if (!ok) throw std::runtime_error("gyro calibration test failed");
}

// Also prepares a reference CSV with the exact reader-side operation, so a
// replay can compare raw+matrix against corrected+already-applied end to end.
int main(int argc, char** argv) {
 if (argc==5 && std::string(argv[1])=="--transform") {
  const auto calibration=GyroCalibration::parse(YAML::LoadFile(argv[2])["imu_params"]);
  require(calibration.applies());
  std::ifstream input(argv[3]); std::ofstream output(argv[4]);
  require(bool(input) && bool(output));
  output << "#timestamp_ns,gx,gy,gz,ax,ay,az\n" << std::setprecision(17);
  std::string line;
  while (std::getline(input,line)) {
   if (line.empty() || line[0]=='#') continue;
   std::stringstream row(line); std::array<std::string,7> fields;
   for (auto& field:fields) require(bool(std::getline(row,field,',')));
   const auto gyro=calibration.apply({{std::stod(fields[1]),std::stod(fields[2]),std::stod(fields[3])}});
   output << fields[0];
   for (double value:gyro) output << ',' << value;
   for (int i=4;i<7;++i) output << ',' << fields[i];
   output << '\n';
  }
  require(bool(output));
  return 0;
 }
 require(argc==1);
 const std::array<double,3> input{{1,2,3}};
 const auto empty=GyroCalibration::parse(YAML::Node());
 require(!empty.applies() && empty.apply(input)==input);
 auto params=YAML::Load("gyro_inverse_matrix: [[1.1,0.02,0.03],[-0.04,0.9,0.05],[0.06,-0.07,1.2]]");
 auto calibration=GyroCalibration::parse(params);
 require(calibration.applies());
 const auto corrected=calibration.apply(input);
 const std::array<double,3> expected{{1.23,1.91,3.52}};
 for (int i=0;i<3;++i) require(std::abs(corrected[i]-expected[i])<1e-14);
 // Already-calibrated replay input must not get scaled/rotated a second time.
 params["gyro_input_is_corrected"]=true;
 calibration=GyroCalibration::parse(params);
 require(calibration.configured && !calibration.applies());
 require(calibration.apply(corrected)==corrected);
 auto rejects=[](const std::string& text) {
  bool rejected=false;
  try { GyroCalibration::parse(YAML::Load(text)); }
  catch (const std::exception&) { rejected=true; }
  require(rejected);
 };
 rejects("gyro_inverse_matrix: [[1,0],[0,1]]");
 rejects("gyro_inverse_matrix: [[1,0,0],[0,1,0],[0,0]]");
 rejects("gyro_inverse_matrix: [[.nan,0,0],[0,1,0],[0,0,1]]");
 rejects("gyro_inverse_matrix: [[1,0,0],[0,.inf,0],[0,0,1]]");
 rejects("gyro_inverse_matrix: [[1,0,0],[1,0,0],[0,0,1]]");
 rejects("gyro_inverse_matrix: [[-1,0,0],[0,1,0],[0,0,1]]");
 std::cout << "PASS gyro matrix: scale, cross-axis terms, default application, already-corrected input, invalid matrices\n";
}
