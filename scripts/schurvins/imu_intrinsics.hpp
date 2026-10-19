#pragma once
#include "live_imu.hpp"
#include <cmath>
#include <stdexcept>

// One inverse affine model, from nominal chip-axis SI to the calibrated body.
// a = Da*am + oa; w = Dw*wm + H*a + ow. Residual biases remain VIO states.
struct ImuIntrinsics {
 using Vector = std::array<double,3>;
 using Matrix = std::array<Vector,3>;
 Matrix accel{{{{1,0,0}},{{0,1,0}},{{0,0,1}}}};
 Matrix gyro{{{{1,0,0}},{{0,1,0}},{{0,0,1}}}};
 Matrix gyro_accel{};
 Vector accel_offset{}, gyro_offset{};
 bool configured = false, input_is_corrected = false;

 static Matrix readMatrix(const YAML::Node& n, const char* key, const Matrix& fallback,
                          bool invertible = true) {
  if (!n[key]) return fallback;
  const auto value = n[key];
  if (!value.IsSequence() || value.size()!=3)
   throw std::runtime_error(std::string(key)+" requires a 3x3 matrix");
  Matrix m{};
  for (int r=0;r<3;++r) {
   if (!value[r].IsSequence() || value[r].size()!=3)
    throw std::runtime_error(std::string(key)+" requires a 3x3 matrix");
   for (int c=0;c<3;++c) {
    m[r][c]=value[r][c].as<double>();
    if (!std::isfinite(m[r][c])) throw std::runtime_error(std::string(key)+" must be finite");
   }
  }
  const double det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
                  -m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
                  +m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
  if (invertible && (!std::isfinite(det) || det<=1e-12))
   throw std::runtime_error(std::string(key)+" must be nonsingular and preserve handedness");
  return m;
 }
 static Vector readVector(const YAML::Node& n, const char* key) {
  Vector v{};
  if (!n[key]) return v;
  if (!n[key].IsSequence() || n[key].size()!=3)
   throw std::runtime_error(std::string(key)+" requires three values");
  for (int i=0;i<3;++i) {
   v[i]=n[key][i].as<double>();
   if (!std::isfinite(v[i])) throw std::runtime_error(std::string(key)+" must be finite");
  }
  return v;
 }
 static ImuIntrinsics parse(const YAML::Node& n) {
  ImuIntrinsics result;
  if (!n || n.IsNull()) return result;
  if (!n.IsMap()) throw std::runtime_error("imu_params must be a mapping");
  if (n["gyro_input_is_corrected"])
   throw std::runtime_error("Use imu_input_is_corrected for a fully corrected IMU stream");
  if (n["imu_input_is_corrected"]) result.input_is_corrected=n["imu_input_is_corrected"].as<bool>();
  for (const char* key : {"accel_inverse_matrix", "gyro_inverse_matrix", "gyro_accel_matrix",
                          "accel_offset", "gyro_offset"})
   if (n[key]) result.configured=true;
  if (result.configured && (!n["input_domain"] || n["input_domain"].as<std::string>()!="raw_si"))
   throw std::runtime_error("IMU intrinsics require input_domain: raw_si; re-export legacy calibration");
  result.accel=readMatrix(n,"accel_inverse_matrix",result.accel);
  result.gyro=readMatrix(n,"gyro_inverse_matrix",result.gyro);
  result.gyro_accel=readMatrix(n,"gyro_accel_matrix",result.gyro_accel,false);
  result.accel_offset=readVector(n,"accel_offset");
  result.gyro_offset=readVector(n,"gyro_offset");
  return result;
 }
 bool applies() const { return configured && !input_is_corrected; }
 static Vector multiply(const Matrix& m, const Vector& v) {
  Vector out{};
  for (int r=0;r<3;++r) for (int c=0;c<3;++c) out[r]+=m[r][c]*v[c];
  return out;
 }
 ImuSample apply(const ImuSample& input) const {
  if (!applies()) return input;
  ImuSample out=input;
  out.accel=multiply(accel,input.accel);
  for (int i=0;i<3;++i) out.accel[i]+=accel_offset[i];
  out.gyro=multiply(gyro,input.gyro);
  const auto coupling=multiply(gyro_accel,out.accel);
  for (int i=0;i<3;++i) out.gyro[i]+=coupling[i]+gyro_offset[i];
  return out;
 }
};
