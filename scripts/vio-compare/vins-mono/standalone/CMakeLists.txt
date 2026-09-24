cmake_minimum_required(VERSION 3.16)
project(tidesense_vins_mono_offline CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -mcpu=cortex-a53")
set(VINS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../upstream/vins_estimator/src")
set(STAGE "/home/ncer/sophpi/install/schurvins-gcc11-static/stage")
set(CERES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../deps/ceres")
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${VINS_ROOT} ${CMAKE_CURRENT_SOURCE_DIR}/../../deps ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/eigen3 ${STAGE}/include ${CERES_ROOT}/include)
add_executable(vins-mono-offline
  main.cpp parameters.cpp
  ${VINS_ROOT}/estimator.cpp
  ${VINS_ROOT}/feature_manager.cpp
  ${VINS_ROOT}/factor/pose_local_parameterization.cpp
  ${VINS_ROOT}/factor/projection_factor.cpp
  ${VINS_ROOT}/factor/projection_td_factor.cpp
  ${VINS_ROOT}/factor/marginalization_factor.cpp
  ${VINS_ROOT}/utility/utility.cpp
  ${VINS_ROOT}/initial/solve_5pts.cpp
  ${VINS_ROOT}/initial/initial_aligment.cpp
  ${VINS_ROOT}/initial/initial_sfm.cpp
  ${VINS_ROOT}/initial/initial_ex_rotation.cpp)
target_link_libraries(vins-mono-offline
  ${CERES_ROOT}/lib/libceres.a ${STAGE}/lib/libglog.a
  -Wl,--start-group
  ${STAGE}/lib/libopencv_video.a
  ${STAGE}/lib/libopencv_calib3d.a
  ${STAGE}/lib/libopencv_features2d.a
  ${STAGE}/lib/libopencv_flann.a
  ${STAGE}/lib/libopencv_imgcodecs.a
  ${STAGE}/lib/libopencv_imgproc.a
  ${STAGE}/lib/libopencv_core.a
  ${STAGE}/share/OpenCV/3rdparty/lib/libtegra_hal.a
  ${STAGE}/share/OpenCV/3rdparty/lib/liblibpng.a
  ${STAGE}/share/OpenCV/3rdparty/lib/liblibjpeg-turbo.a
  ${STAGE}/share/OpenCV/3rdparty/lib/libzlib.a
  ${STAGE}/share/OpenCV/3rdparty/lib/libittnotify.a
  -Wl,--end-group pthread)
target_link_options(vins-mono-offline PRIVATE -static)
