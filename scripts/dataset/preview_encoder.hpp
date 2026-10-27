#pragma once
#include "tide_camera.hpp"
#include <vector>
#include <memory>

class PreviewEncoder {
 struct Impl;
 std::unique_ptr<Impl> impl_;
 public:
 PreviewEncoder(unsigned width,unsigned height);
 ~PreviewEncoder();
 std::vector<uint8_t> encode(const LivePair&);
 unsigned width() const;
 unsigned height() const;
};
