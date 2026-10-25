#include "tide_camera.hpp"
#include "backend_mock.h"
#include <iostream>
#include <stdexcept>

static void require(bool ok,const char* reason) { if (!ok) throw std::runtime_error(reason); }
int main() {
 try {
  volatile sig_atomic_t interrupted=0;
  tide_cam_caps caps{}; require(tide_cam_query_caps(&caps)==0,"capabilities");
  for (uint32_t m=0;m<caps.mode_count;++m) {
   mock_reset();
   tide_cam_config config; tide_cam_config_init(&config); config.mode=caps.modes[m];
   LivePair retained;
   {
    TideCamera camera(config);
    mock_push(1); require(camera.next(retained,interrupted),"first frame");
    require(retained.header.width==config.mode.width && retained.header.height==config.mode.height,"dimensions");
    require(retained.header.left_ns==1050000000ULL && retained.header.right_ns==1052600000ULL,"VI timestamps");
    require(retained.header.flags==CAMERA_FSIN,"synchronization");
    for (int i=0;i<12;++i) {
     mock_push(1); LivePair next; require(camera.next(next,interrupted),"next frame");
    }
    require(camera.stop()==0,"stop");
   }
   // Every Mat copy must stay valid after capture slot reuse AND close.
   for (int y=0;y<retained.left.rows;++y) for (int x=0;x<retained.left.cols;++x)
    require(retained.left.at<uint8_t>(y,x)==1 && retained.right.at<uint8_t>(y,x)==2,"retained image changed");
  }
  mock_reset();
  tide_cam_config config; tide_cam_config_init(&config);
  {
   TideCamera camera(config); mock_fail(TIDE_CAM_IO);
   LivePair pair; bool failed=false;
   try { camera.next(pair,interrupted); } catch (const std::runtime_error&) { failed=true; }
   require(failed,"backend fault was hidden");
  }
  require(!mock_active(),"camera leak");
  std::cout<<"TIDE CAMERA ADAPTER PASS: dimensions, timestamps, owned Mat lifetime, fault propagation\n";
 } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
