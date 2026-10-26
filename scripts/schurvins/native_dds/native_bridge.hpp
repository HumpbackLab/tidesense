// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
// Experimental pose-only transport, matching production MicroRosBridge's FIFO.
class NativeBridge {
 public:
  NativeBridge();
  ~NativeBridge();
  void submit(uint64_t stamp,bool tracking,const double* p,const double* q);
  static bool compiled() {return true;}
  bool enabled() const {return true;}
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
