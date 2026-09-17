cmake_minimum_required(VERSION 3.13)
project(tidesense_schurvins LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SCHUR_SOURCE "${SDK_ROOT}/SchurVINS" CACHE PATH "Official SchurVINS checkout")
set(WORK "${SDK_ROOT}/install/schurvins-gcc11-static" CACHE PATH "Build dependencies/staging directory")
set(DEPS "${SDK_ROOT}/install/schurvins-build/deps" CACHE PATH "Dependency source directory")
option(SCHUR_STATIC "Build a fully static executable" ON)
set(GLOG_LIBRARY "${WORK}/stage/lib/libglog.a" CACHE FILEPATH "Target glog static library")
if(SCHUR_STATIC)
  set(CMAKE_SKIP_RPATH TRUE)
else()
  set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN")
endif()
find_package(OpenCV 3.4 REQUIRED COMPONENTS core imgproc imgcodecs calib3d video highgui)
find_package(yaml-cpp REQUIRED)
set(MODULES rpg_common vikit/vikit_common vikit/vikit_cameras vikit/vikit_solver
    svo_common svo_vio_common svo_direct svo_tracker svo_img_align svo)
set(SOURCES)
foreach(module IN LISTS MODULES)
  include_directories("${SCHUR_SOURCE}/${module}/include")
  file(GLOB module_sources "${SCHUR_SOURCE}/${module}/src/*.cpp")
  list(APPEND SOURCES ${module_sources})
endforeach()
# These optional files require CUDA; the CPU image alignment implementation is built.
list(FILTER SOURCES EXCLUDE REGEX "/(frame_gpu|sparse_img_align_gpu|test_utils)\\.cpp$")
include_directories(${OpenCV_INCLUDE_DIRS} "${WORK}/sysroot/eigen/include/eigen3"
  "${WORK}/stage/include"
  "${DEPS}/minkindr/minkindr/include" "${DEPS}/fast_neon/include")
add_library(fast STATIC
  "${DEPS}/fast_neon/src/fast_9.cpp"
  "${DEPS}/fast_neon/src/fast_10.cpp"
  "${DEPS}/fast_neon/src/fast_10_score.cpp"
  "${DEPS}/fast_neon/src/nonmax_3x3.cpp")
# Use FAST-10 scalar fallback, keeping the upstream x86 FAST-10 semantics.
# Eigen/OpenCV still use AArch64 NEON. ROS, loop closure, global map and
# mono OpenGV initialization are optional and are not enabled for this stereo build.
if(SCHUR_STATIC)
  add_library(schurvins STATIC ${SOURCES})
else()
  add_library(schurvins SHARED ${SOURCES})
endif()
target_compile_definitions(schurvins PUBLIC SVO_DEPTHFILTER_IN_REPROJECTOR)
target_link_libraries(schurvins PUBLIC ${OpenCV_LIBS} yaml-cpp
  "${GLOG_LIBRARY}" fast pthread dl)
target_link_options(schurvins PRIVATE "-Wl,--no-undefined")
add_executable(schurvins_stereo stereo.cpp)
target_link_libraries(schurvins_stereo PRIVATE schurvins)
if(SCHUR_STATIC)
  target_link_options(schurvins_stereo PRIVATE -static -Wl,--gc-sections)
  install(TARGETS schurvins_stereo RUNTIME DESTINATION bin)
else()
  install(TARGETS schurvins schurvins_stereo LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
endif()
