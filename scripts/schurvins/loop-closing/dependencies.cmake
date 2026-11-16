# Optional standalone loop detector. No ROS 1 / catkin or frontend ABI changes.
set(SCHUR_LOOP_DEPS "${SDK_ROOT}/install/schur-loop-closing/deps" CACHE PATH "Pinned DBoW2 and OpenGV sources")
set(SCHUR_LOOP_CERES "${SDK_ROOT}/install/vio-compare-20260924-121320/deps/ceres" CACHE PATH "AArch64 static Ceres prefix")
foreach(required "${SCHUR_LOOP_DEPS}/dbow2/include/DBoW2/DBoW2.h"
    "${SCHUR_LOOP_DEPS}/opengv/include/opengv/point_cloud/PointCloudAdapter.hpp"
    "${SCHUR_LOOP_CERES}/lib/libceres.a")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Missing ${required}; run scripts/schurvins/loop-closing/prepare.sh")
  endif()
endforeach()
add_library(schur_loop_dbow2 STATIC
  "${SCHUR_LOOP_DEPS}/dbow2/src/BowVector.cpp"
  "${SCHUR_LOOP_DEPS}/dbow2/src/FORB.cpp"
  "${SCHUR_LOOP_DEPS}/dbow2/src/FeatureVector.cpp"
  "${SCHUR_LOOP_DEPS}/dbow2/src/ScoringObject.cpp")
target_include_directories(schur_loop_dbow2 PUBLIC "${SCHUR_LOOP_DEPS}/dbow2/include")
target_include_directories(schur_loop_dbow2 PRIVATE "${SCHUR_LOOP_DEPS}/dbow2/include/DBoW2")
target_link_libraries(schur_loop_dbow2 PUBLIC ${OpenCV_LIBS})
# The native detector uses OpenCV for 2D geometry and OpenGV for 3D alignment.
add_library(schur_loop_opengv STATIC
  "${SCHUR_LOOP_DEPS}/opengv/src/point_cloud/methods.cpp"
  "${SCHUR_LOOP_DEPS}/opengv/src/point_cloud/PointCloudAdapter.cpp"
  "${SCHUR_LOOP_DEPS}/opengv/src/point_cloud/MAPointCloud.cpp"
  "${SCHUR_LOOP_DEPS}/opengv/src/math/arun.cpp"
  "${SCHUR_LOOP_DEPS}/opengv/src/math/cayley.cpp"
  "${SCHUR_LOOP_DEPS}/opengv/src/sac_problems/point_cloud/PointCloudSacProblem.cpp")
target_include_directories(schur_loop_opengv PUBLIC "${SCHUR_LOOP_DEPS}/opengv/include"
  "${WORK}/sysroot/eigen/include/eigen3/unsupported")
add_library(schurvins_loop_closing STATIC
  "${SCHUR_SOURCE}/svo_online_loopclosing/src/bow.cpp"
  "${SCHUR_SOURCE}/svo_online_loopclosing/src/geometric_verification.cpp"
  "${SCHUR_SOURCE}/svo_online_loopclosing/src/loop_closing.cpp"
  "${SCHUR_SOURCE}/svo_online_loopclosing/src/map_alignment.cpp"
  "${SCHUR_SOURCE}/svo_pgo/src/pgo.cpp")
target_include_directories(schurvins_loop_closing PUBLIC
  "${SCHUR_SOURCE}/svo_online_loopclosing/include" "${SCHUR_SOURCE}/svo_pgo/include"
  "${SCHUR_LOOP_CERES}/include")
target_link_libraries(schurvins_loop_closing PUBLIC schurvins schur_loop_dbow2 schur_loop_opengv
  "${SCHUR_LOOP_CERES}/lib/libceres.a" "${GLOG_LIBRARY}" pthread)
add_executable(schurvins_loop_geometry_test EXCLUDE_FROM_ALL tests/loop_geometry_test.cpp)
target_link_libraries(schurvins_loop_geometry_test PRIVATE schurvins_loop_closing)
add_executable(schur_loop_fastpath_test EXCLUDE_FROM_ALL tests/loop_fastpath_test.cpp)
target_link_libraries(schur_loop_fastpath_test PRIVATE schurvins_loop_closing)
if(SCHUR_STATIC)
  target_link_options(schurvins_loop_geometry_test PRIVATE -static -Wl,--gc-sections)
  target_link_options(schur_loop_fastpath_test PRIVATE -static -Wl,--gc-sections)
endif()
option(SCHUR_LOOP_OFFLOAD "Build experimental C906/TPU descriptor matcher" OFF)
if(SCHUR_LOOP_OFFLOAD)
  set(LOOP_TPU "${SDK_ROOT}/install/soc_sg2002_tidesense_arm64_sd/tpu_64bit/cvitek_tpu_sdk")
  add_library(schur_loop_offload STATIC loop-closing/offload/engine.cpp loop-closing/offload/jobs.c)
  target_include_directories(schur_loop_offload PRIVATE "${SDK_ROOT}/cvi_mpi/include")
  target_compile_options(schur_loop_offload PRIVATE -idirafter "${LOOP_TPU}/include")
  target_link_libraries(schur_loop_offload PUBLIC -Wl,--start-group
    "${SDK_ROOT}/cvi_mpi/lib/libsys.a" "${LOOP_TPU}/lib/libcviruntime-static.a"
    "${LOOP_TPU}/lib/libcvikernel-static.a" "${LOOP_TPU}/lib/libz.a" -Wl,--end-group pthread dl rt m)
  add_executable(schur_loop_match_probe EXCLUDE_FROM_ALL loop-closing/offload/probe.cpp)
  target_link_libraries(schur_loop_match_probe PRIVATE schur_loop_offload)
  target_link_options(schur_loop_match_probe PRIVATE -static -Wl,--gc-sections)
  target_link_libraries(schurvins_stereo PRIVATE schur_loop_offload)
  target_sources(schurvins_stereo PRIVATE loop-closing/offload/matcher.cpp)
  target_compile_definitions(schurvins_stereo PRIVATE SCHUR_LOOP_OFFLOAD=1)
  add_executable(schur_loop_matcher_test EXCLUDE_FROM_ALL loop-closing/offload/matcher_test.cpp loop-closing/offload/matcher.cpp)
  target_link_libraries(schur_loop_matcher_test PRIVATE schur_loop_offload schurvins_loop_closing)
  target_link_options(schur_loop_matcher_test PRIVATE -static -Wl,--gc-sections)
  option(SCHUR_LOOP_ORB_PROFILE "Build instrumented pinned OpenCV ORB probe" OFF)
  option(SCHUR_LOOP_IVE_ORB "Build IVE ST candidate variant of the pinned ORB implementation" OFF)
  option(SCHUR_LOOP_TPU_GAUSSIAN "Build approximate Q9 TPU Gaussian for ORB" "${SCHUR_LOOP_IVE_ORB}")
  if(SCHUR_LOOP_TPU_GAUSSIAN AND NOT SCHUR_LOOP_IVE_ORB)
    message(FATAL_ERROR "SCHUR_LOOP_TPU_GAUSSIAN requires SCHUR_LOOP_IVE_ORB=ON for the pinned ORB hook")
  endif()
  if(SCHUR_LOOP_IVE_ORB)
    target_compile_definitions(schurvins_stereo PRIVATE SCHUR_LOOP_IVE_ORB=1)
    target_sources(schurvins_stereo PRIVATE loop-closing/offload/orb_ive.cpp
      "${SDK_ROOT}/install/schur-loop-offload/orb-profile/orb.cpp")
    target_include_directories(schurvins_stereo PRIVATE
      "${SDK_ROOT}/cvi_mpi/include" "${SDK_ROOT}/osdrv/interdrv/include/common/uapi"
      "${SDK_ROOT}/osdrv/interdrv/include/chip/cv181x/uapi"
      "${DEPS}/opencv-3.4.20/modules/features2d/src" "${DEPS}/opencv-3.4.20/modules/core/include"
      "${WORK}/opencv/modules/features2d" "${WORK}/opencv")
    set_source_files_properties("${SDK_ROOT}/install/schur-loop-offload/orb-profile/orb.cpp"
      PROPERTIES COMPILE_DEFINITIONS __OPENCV_BUILD=1)
    target_sources(schur_loop_fastpath_test PRIVATE loop-closing/offload/orb_ive.cpp
      "${SDK_ROOT}/install/schur-loop-offload/orb-profile/orb.cpp")
    target_include_directories(schur_loop_fastpath_test PRIVATE
      "${SDK_ROOT}/cvi_mpi/include" "${SDK_ROOT}/osdrv/interdrv/include/common/uapi"
      "${SDK_ROOT}/osdrv/interdrv/include/chip/cv181x/uapi"
      "${DEPS}/opencv-3.4.20/modules/features2d/src" "${DEPS}/opencv-3.4.20/modules/core/include"
      "${WORK}/opencv/modules/features2d" "${WORK}/opencv")
    target_link_libraries(schur_loop_fastpath_test PRIVATE schur_loop_offload)
  endif()
  if(SCHUR_LOOP_TPU_GAUSSIAN)
    add_library(schur_gaussian STATIC loop-closing/offload/orb_tpu_blur.cpp loop-closing/offload/gaussian_jobs.c)
    target_include_directories(schur_gaussian PRIVATE "${SDK_ROOT}/cvi_mpi/include" ${OpenCV_INCLUDE_DIRS})
    target_compile_options(schur_gaussian PRIVATE -idirafter "${LOOP_TPU}/include")
    target_link_libraries(schur_gaussian PUBLIC schur_loop_offload ${OpenCV_LIBS})
    # Weak ORB hooks need their provider's object linked explicitly.
    foreach(consumer schurvins_stereo schur_loop_fastpath_test)
      target_link_libraries(${consumer} PRIVATE -Wl,--whole-archive schur_gaussian -Wl,--no-whole-archive)
    endforeach()
    target_compile_definitions(schurvins_stereo PRIVATE SCHUR_LOOP_TPU_GAUSSIAN=1)
    add_executable(schur_gaussian_probe EXCLUDE_FROM_ALL loop-closing/offload/gaussian_probe.cpp)
    target_sources(schur_gaussian_probe PRIVATE loop-closing/offload/gaussian_cpu_q9.cpp)
    target_link_libraries(schur_gaussian_probe PRIVATE schur_gaussian)
    target_link_options(schur_gaussian_probe PRIVATE -static -Wl,--gc-sections)
  endif()
  if(SCHUR_LOOP_ORB_PROFILE)
    add_executable(schur_orb_profile EXCLUDE_FROM_ALL loop-closing/offload/orb_probe.cpp
      "${SDK_ROOT}/install/schur-loop-offload/orb-profile/orb.cpp")
    target_include_directories(schur_orb_profile PRIVATE
      "${DEPS}/opencv-3.4.20/modules/features2d/src"
      "${DEPS}/opencv-3.4.20/modules/core/include"
      "${WORK}/opencv/modules/features2d" "${WORK}/opencv")
    target_compile_definitions(schur_orb_profile PRIVATE __OPENCV_BUILD=1)
    target_link_libraries(schur_orb_profile PRIVATE ${OpenCV_LIBS})
    target_link_options(schur_orb_profile PRIVATE -static -Wl,--gc-sections)
    add_executable(schur_orb_reference EXCLUDE_FROM_ALL loop-closing/offload/orb_probe.cpp)
    target_link_libraries(schur_orb_reference PRIVATE ${OpenCV_LIBS})
    target_link_options(schur_orb_reference PRIVATE -static -Wl,--gc-sections)
    if(SCHUR_LOOP_IVE_ORB)
      target_sources(schur_orb_profile PRIVATE loop-closing/offload/orb_ive.cpp)
      target_include_directories(schur_orb_profile PRIVATE "${SDK_ROOT}/cvi_mpi/include"
        "${SDK_ROOT}/osdrv/interdrv/include/common/uapi" "${SDK_ROOT}/osdrv/interdrv/include/chip/cv181x/uapi")
      target_link_libraries(schur_orb_profile PRIVATE schur_loop_offload)
    endif()
    if(SCHUR_LOOP_TPU_GAUSSIAN)
      target_link_libraries(schur_orb_profile PRIVATE -Wl,--whole-archive schur_gaussian -Wl,--no-whole-archive)
    endif()
  endif()
endif()
