#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

// Maps nominal chip-axis SI gyro measurements to the calibrated IMU axes.
// Bias remains an estimator state; this matrix includes scale and misalignment.
struct GyroCalibration {
 std::array<std::array<double,3>,3> matrix{{{{1,0,0}},{{0,1,0}},{{0,0,1}}}};
 bool configured = false;
 bool input_is_corrected = false;

 static GyroCalibration parse(const YAML::Node& params) {
  GyroCalibration result;
  if (!params || !params.IsMap()) return result;
  if (params["gyro_input_is_corrected"])
   result.input_is_corrected = params["gyro_input_is_corrected"].as<bool>();
  const auto node = params["gyro_inverse_matrix"];
  if (!node) return result;
  if (!node.IsSequence() || node.size()!=3)
   throw std::runtime_error("gyro_inverse_matrix requires a 3x3 matrix");
  for (int r=0;r<3;++r) {
   if (!node[r].IsSequence() || node[r].size()!=3)
    throw std::runtime_error("gyro_inverse_matrix requires a 3x3 matrix");
   for (int c=0;c<3;++c) {
    result.matrix[r][c]=node[r][c].as<double>();
    if (!std::isfinite(result.matrix[r][c]))
     throw std::runtime_error("gyro_inverse_matrix must be finite");
   }
  }
  const auto& m=result.matrix;
  const double det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
                  -m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
                  +m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
  if (!std::isfinite(det) || det<=1e-12)
   throw std::runtime_error("gyro_inverse_matrix must be nonsingular and preserve handedness");
  result.configured=true;
  return result;
 }

 bool applies() const { return configured && !input_is_corrected; }
 std::array<double,3> apply(const std::array<double,3>& gyro) const {
  if (!applies()) return gyro;
  std::array<double,3> corrected{};
  for (int r=0;r<3;++r)
   for (int c=0;c<3;++c) corrected[r]+=matrix[r][c]*gyro[c];
  return corrected;
 }
};
