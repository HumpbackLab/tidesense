#include "live_camera.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
static void require(bool condition, const char *message) {
 if (!condition) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
 if (argc != 2) return 2;
 volatile sig_atomic_t interrupted = 0;
 try {
  setenv("SCHUR_CAMERA_MOCK_MODE", "valid", 1);
  {
   LiveCamera camera(argv[1], "unused");
   LivePair first, latest;
   require(camera.next(first, interrupted), "first frame missing");
   auto value = first.left.at<unsigned char>(0,0);
   usleep(150000);
   require(camera.next(latest, interrupted), "latest frame missing");
   require(latest.header.sequence > first.header.sequence + 1, "old frames accumulated");
   require(first.left.at<unsigned char>(0,0) == value, "old image storage was overwritten");
   require(latest.left.at<unsigned char>(399,639) == latest.header.sequence % 251, "bad left payload");
   require(latest.right.at<unsigned char>(399,639) == 250-latest.header.sequence % 251, "bad right payload");
   camera.stop(); camera.report();
  }
  for (const char *mode : {"bad-header", "truncated", "backwards"}) {
   setenv("SCHUR_CAMERA_MOCK_MODE", mode, 1);
   bool rejected = false;
   try {
    LiveCamera camera(argv[1], "unused"); LivePair pair;
    for (int n = 0; n < 5; ++n) camera.next(pair, interrupted);
   } catch (const std::runtime_error&) { rejected = true; }
   require(rejected, "bad stream accepted");
   std::cout << "PASS rejected " << mode << '\n';
  }
  std::cout << "PASS bounded latest-pair queue, owned pixels, protocol errors, child cleanup\n";
  return 0;
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
