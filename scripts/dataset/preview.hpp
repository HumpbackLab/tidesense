#pragma once
#include "tide_camera.hpp"
#include <memory>
#include <string>

// One latest-frame slot; hardware encoding and network sends never run in capture.
class PreviewServer {
 struct Impl;
 std::unique_ptr<Impl> impl_;
 public:
 struct Status {
  unsigned port=0,width=0,height=0,clients=0;
  uint64_t sequence=0,frames=0,bytes=0;
  double cpu_ms=0;
  std::string error;
 };
 PreviewServer(const tide_cam_config&,const std::string& token);
 ~PreviewServer();
 void submit(const LivePair&,bool recording);
 Status status();
 std::string prepareDownload(const std::string&,const std::string&,const std::string&);
 void releaseDownload(const std::string&);
};
