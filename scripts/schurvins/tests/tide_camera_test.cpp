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
  for (unsigned mask:{1u,2u,3u}) {
   mock_reset();mock_cameras(mask);
   config.allow_partial=1;
   TideCamera camera(config);
   mock_push(1);LivePair pair;require(camera.next(pair,interrupted),"partial preview frame");
   require(pair.left.empty()==!(mask&1) && pair.right.empty()==!(mask&2),"camera availability/order");
   require(bool(pair.header.left_ns)==bool(mask&1) && bool(pair.header.right_ns)==bool(mask&2),"missing camera timestamp");
   // Recover the other camera without recreating the capture session.
   mock_cameras(3);mock_push(1);require(camera.next(pair,interrupted),"stereo recovery");
   require(!pair.left.empty() && !pair.right.empty(),"stereo did not recover");
  }
  mock_reset();mock_cameras(2);config.allow_partial=0;
  {
   TideCamera camera(config);mock_push(1);LivePair pair;bool rejected=false;
   try {camera.next(pair,interrupted);}catch(const std::runtime_error&){rejected=true;}
   require(rejected,"strict stereo accepted a partial frame");
  }
  std::cout<<"TIDE CAMERA ADAPTER PASS: dimensions, timestamps, owned Mat lifetime, fault propagation\n";
 } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
