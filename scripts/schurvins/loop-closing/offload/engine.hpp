// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
struct LoopMatchStats {
  size_t batches=0,pairs=0,tpu_calls=0,reductions=0;
  double submit_ms=0,device_ms=0,tpu_ms=0,reduction_ms=0,thread_cpu_ms=0;
};
class LoopMatchEngine {
 public:
  explicit LoopMatchEngine(const std::string& backend);
  ~LoopMatchEngine();
  void begin(size_t queries);
  void append(unsigned query,unsigned candidate,const uint8_t* a,const uint8_t* b);
  std::vector<loop_match_result> finish();
  const LoopMatchStats& stats() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
