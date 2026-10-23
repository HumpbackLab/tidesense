cmake_minimum_required(VERSION 3.13)
project(tidesense_tpu_sad_probe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(DEPTH_BUILD_APP OFF CACHE BOOL "" FORCE)
set(DEPTH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(.. depth)
get_filename_component(SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(TPU "${SDK}/install/soc_sg2002_tidesense_arm64_sd/tpu_64bit/cvitek_tpu_sdk")
add_executable(tpu_sad_probe probe.cpp tpu_sad.cpp)
# TPU SDK bundles another OpenCV; search its runtime headers after our pinned stage.
target_compile_options(tpu_sad_probe PRIVATE -idirafter "${TPU}/include" -Wall -Wextra -Werror)
target_link_libraries(tpu_sad_probe PRIVATE depth_engine -Wl,--start-group
  "${TPU}/lib/libcviruntime-static.a" "${TPU}/lib/libcvikernel-static.a"
  "${TPU}/lib/libz.a" -Wl,--end-group pthread dl rt m)
target_link_options(tpu_sad_probe PRIVATE -static -Wl,--gc-sections)
