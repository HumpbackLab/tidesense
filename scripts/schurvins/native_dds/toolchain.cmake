include("${CMAKE_CURRENT_LIST_DIR}/../a53-gcc11-toolchain.cmake")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
# Resolve target ROS libraries without falling back to host /opt/ros binaries.
list(APPEND CMAKE_FIND_ROOT_PATH "${SDK_ROOT}/install/native-dds/ws/install")
foreach(pkg rosidl_generator_py rosidl_typesupport_fastrtps_c rosidl_typesupport_fastrtps_cpp)
  set(CMAKE_DISABLE_FIND_PACKAGE_${pkg} TRUE CACHE BOOL "Not needed by DDS experiment" FORCE)
endforeach()
