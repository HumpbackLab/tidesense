cmake_minimum_required(VERSION 3.10)
project(tidesense_vins_fusion_offline CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS "-static")

set(UPSTREAM ${CMAKE_CURRENT_SOURCE_DIR}/VINS-Fusion)
set(STAGE /home/ncer/sophpi/install/schurvins-gcc11-static/stage)
set(CERES_STAGE ${CMAKE_CURRENT_SOURCE_DIR}/../deps/ceres)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${UPSTREAM}/camera_models/include
    ${UPSTREAM}/vins_estimator/src
    ${STAGE}/include
    ${CERES_STAGE}/include
    /usr/include/eigen3
)

set(CAM ${UPSTREAM}/camera_models/src)
set(EST ${UPSTREAM}/vins_estimator/src)
add_executable(vins_fusion_offline
    offline.cpp
    ${CAM}/camera_models/Camera.cc
    ${CAM}/camera_models/CameraFactory.cc
    ${CAM}/camera_models/CostFunctionFactory.cc
    ${CAM}/camera_models/PinholeCamera.cc
    ${CAM}/camera_models/PinholeFullCamera.cc
    ${CAM}/camera_models/CataCamera.cc
    ${CAM}/camera_models/EquidistantCamera.cc
    ${CAM}/camera_models/ScaramuzzaCamera.cc
    ${CAM}/gpl/gpl.cc
    ${CAM}/gpl/EigenQuaternionParameterization.cc
    ${EST}/estimator/parameters.cpp
    ${EST}/estimator/estimator.cpp
    ${EST}/estimator/feature_manager.cpp
    ${EST}/factor/pose_local_parameterization.cpp
    ${EST}/factor/projectionTwoFrameOneCamFactor.cpp
    ${EST}/factor/projectionTwoFrameTwoCamFactor.cpp
    ${EST}/factor/projectionOneFrameTwoCamFactor.cpp
    ${EST}/factor/marginalization_factor.cpp
    ${EST}/utility/utility.cpp
    ${EST}/initial/solve_5pts.cpp
    ${EST}/initial/initial_aligment.cpp
    ${EST}/initial/initial_sfm.cpp
    ${EST}/initial/initial_ex_rotation.cpp
    ${EST}/featureTracker/feature_tracker.cpp
)

target_link_libraries(vins_fusion_offline
    -Wl,--start-group
    ${CERES_STAGE}/lib/libceres.a
    ${STAGE}/lib/libopencv_calib3d.a
    ${STAGE}/lib/libopencv_features2d.a
    ${STAGE}/lib/libopencv_flann.a
    ${STAGE}/lib/libopencv_video.a
    ${STAGE}/lib/libopencv_imgproc.a
    ${STAGE}/lib/libopencv_core.a
    ${STAGE}/lib/libglog.a
    ${STAGE}/share/OpenCV/3rdparty/lib/libtegra_hal.a
    -Wl,--end-group
    ${STAGE}/share/OpenCV/3rdparty/lib/libzlib.a
    dl pthread rt m
)
