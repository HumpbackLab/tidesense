// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "engine.hpp"
#include <svo/online_loopclosing/geometric_verification.h>
class LoopMatchProvider final : public svo::FeatureMatchingAccelerator {
 public:
  explicit LoopMatchProvider(const std::string& backend,bool verify);
  void match(const svo::LoopFeatureSets&,const svo::LoopNodeSets&,int,Eigen::MatrixXd*) override;
  void report() const;
 private:
  LoopMatchEngine engine_;
  std::string backend_;
  bool verify_;
  size_t calls_=0,verified_=0;
  double wall_ms_=0,cpu_ms_=0,reference_ms_=0;
};
