// SPDX-License-Identifier: GPL-3.0-or-later
// Build-selected ROS transport fixture: known poses, then tracking loss. No camera/IMU required.
#include "ros_bridge.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
int main(int argc, char** argv) {
  try {
    const int count = argc > 1 ? std::stoi(argv[1]) : 400;
    const int hz = argc > 2 ? std::stoi(argv[2]) : 10;
    if (hz != 10 && hz != 200) throw std::runtime_error("Rate must be 10 or 200 Hz");
    if (count < 1 || count > 10000) throw std::runtime_error("Invalid sample count");
    RosBridge bridge;
    const double p[3] = {1.25, -2.5, 3.75}, q[4] = {0,0,0,1};
    auto deadline = std::chrono::steady_clock::now();
    const auto period = std::chrono::nanoseconds(1000000000ULL/hz);
    for (int i = 0; i < count; ++i) {
      bridge.submit(1700000000000000000ULL + uint64_t(i)*(1000000000ULL/hz),
                    (i % 50) < 40, p, q);
      deadline += period;
      std::this_thread::sleep_until(deadline);
    }
    std::cout << "transport fixture completed, compiled=" << bridge.compiled()
              << " enabled=" << bridge.enabled() << '\n';
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
