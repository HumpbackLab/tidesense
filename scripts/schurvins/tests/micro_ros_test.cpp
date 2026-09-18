// SPDX-License-Identifier: GPL-3.0-or-later
// Transport fixture: known poses, then tracking loss. No camera/IMU required.
#include "micro_ros_bridge.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
int main(int argc, char** argv) {
  try {
    const int count = argc > 1 ? std::stoi(argv[1]) : 400;
    if (count < 1 || count > 10000) throw std::runtime_error("Invalid sample count");
    MicroRosBridge bridge;
    const double p[3] = {1.25, -2.5, 3.75}, q[4] = {0,0,0,1};
    for (int i = 0; i < count; ++i) {
      bridge.submit(1700000000000000000ULL + uint64_t(i)*100000000ULL,
                    (i % 50) < 40, p, q);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "transport fixture completed, compiled=" << bridge.compiled()
              << " enabled=" << bridge.enabled() << '\n';
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
