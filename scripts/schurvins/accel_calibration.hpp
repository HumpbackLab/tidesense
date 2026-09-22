#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

// Penguin Flight Console six-face result, in the unrotated IMU chip axes.
struct AccelCalibration {
 std::array<double,3> bias{{0,0,0}}, scale{{1,1,1}};
 static AccelCalibration parse(const YAML::Node& n, double iio_scale) {
  if (n["schema_version"].as<int>() != 1 ||
      n["board"].as<std::string>() != "tidesense" ||
      n["sensor"].as<std::string>() != "sc7u22" ||
      n["frame"].as<std::string>() != "imu_chip_xyz" ||
      n["sampling_frequency_hz"].as<int>() != 200)
   throw std::runtime_error("Incompatible SC7U22 accelerometer calibration metadata");
  double recorded_scale=n["accel_scale_m_s2_per_lsb"].as<double>();
  if (!std::isfinite(recorded_scale) || !std::isfinite(iio_scale) || iio_scale <= 0 ||
      std::abs(recorded_scale-iio_scale) > iio_scale*1e-6)
   throw std::runtime_error("Accelerometer calibration IIO scale mismatch");
  const auto b=n["bias_m_s2"], s=n["correction_scale"];
  if (!b.IsSequence() || b.size()!=3 || !s.IsSequence() || s.size()!=3)
   throw std::runtime_error("Accelerometer calibration requires XYZ bias and scale");
  AccelCalibration result;
  for (int i=0;i<3;++i) {
   result.bias[i]=b[i].as<double>(); result.scale[i]=s[i].as<double>();
   if (!std::isfinite(result.bias[i]) || !std::isfinite(result.scale[i]) || result.scale[i]<=0)
    throw std::runtime_error("Invalid accelerometer calibration coefficient");
  }
  return result;
 }
 double apply(int axis, double raw_si) const { return (raw_si-bias[axis])*scale[axis]; }
};
