cmake_minimum_required(VERSION 3.13)
project(tidesense_vio_depth LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(SDK "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SENSOR "${SDK}/scripts/schurvins")
set(TPU "${SDK}/install/soc_sg2002_tidesense_arm64_sd/tpu_64bit/cvitek_tpu_sdk")
set(MICRO_ROS "${SDK}/install/micro-ros/firmware/build")
set(DEPTH_BUILD_APP OFF CACHE BOOL "" FORCE)
set(DEPTH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DEPTH_STEREOBM_ALL64 OFF CACHE BOOL "" FORCE)
# Build the existing estimator unchanged, with the shipping optimization profile.
add_subdirectory("${SENSOR}" schur EXCLUDE_FROM_ALL)
add_subdirectory("${SDK}/scripts/depth" depth EXCLUDE_FROM_ALL)
add_library(joint_depth STATIC pipeline.cpp "${SDK}/scripts/depth/tpu-match/tpu_sad.cpp")
target_include_directories(joint_depth PUBLIC "${SDK}/scripts/depth/tpu-match" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(joint_depth PRIVATE -idirafter "${TPU}/include" -Wall -Wextra -Werror)
target_link_libraries(joint_depth PUBLIC depth_engine -Wl,--start-group
  "${TPU}/lib/libcviruntime-static.a" "${TPU}/lib/libcvikernel-static.a"
  "${TPU}/lib/libz.a" -Wl,--end-group pthread dl rt m)
add_executable(tidesense_vio_depth main.cpp combined_ros.cpp
  "${SENSOR}/live_camera.cpp" "${SENSOR}/live_imu.cpp")
foreach(module rpg_common vikit/vikit_common vikit/vikit_cameras vikit/vikit_solver
    svo_common svo_vio_common svo_direct svo_tracker svo_img_align svo)
  target_include_directories(tidesense_vio_depth PRIVATE "${SDK}/SchurVINS/${module}/include")
endforeach()
target_include_directories(tidesense_vio_depth PRIVATE "${SENSOR}" "${MICRO_ROS}/include"
  "${SDK}/install/schurvins-gcc11-static/sysroot/eigen/include/eigen3"
  "${SDK}/install/schurvins-gcc11-static/stage/include"
  "${SDK}/install/schurvins-build/deps/minkindr/minkindr/include"
  "${SDK}/install/schurvins-build/deps/fast_neon/include")
file(GLOB ros_dirs LIST_DIRECTORIES TRUE "${MICRO_ROS}/include/*")
foreach(dir IN LISTS ros_dirs)
  if(IS_DIRECTORY "${dir}")
    target_include_directories(tidesense_vio_depth PRIVATE "${dir}")
  endif()
endforeach()
# Upstream headers generate warnings; keep strict warnings on our TPU/pipeline code.
target_link_libraries(tidesense_vio_depth PRIVATE schurvins joint_depth "${MICRO_ROS}/libmicroros.a" rt m)
target_link_options(tidesense_vio_depth PRIVATE -static -Wl,--gc-sections)
add_executable(joint_depth_test test_pipeline.cpp)
target_link_libraries(joint_depth_test PRIVATE joint_depth)
target_compile_options(joint_depth_test PRIVATE -Wall -Wextra -Werror)
target_link_options(joint_depth_test PRIVATE -static -Wl,--gc-sections)
