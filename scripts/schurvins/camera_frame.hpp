#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <opencv2/core.hpp>
#include "camera_protocol.h"

// Owned image storage. Header also serves the existing on-disk dataset format.
struct LivePair {
 CameraHeader header{};
 cv::Mat left, right;
};
inline std::string executableDirectory() {
 char buf[4096];
 ssize_t n=readlink("/proc/self/exe",buf,sizeof(buf)-1);
 if (n<=0 || n>=ssize_t(sizeof(buf)-1)) throw std::runtime_error("Cannot locate executable");
 std::string path(buf,n);
 return path.substr(0,path.find_last_of('/'));
}
