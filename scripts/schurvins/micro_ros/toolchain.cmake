include("${CMAKE_CURRENT_LIST_DIR}/../a53-gcc11-toolchain.cmake")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# Host ament/rosidl generators must be found; every target runtime dependency is
# built in the micro-ROS workspace. Never link a host ROS library into the ELF.
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
# The host ROS installation also advertises Python/C++ and Fast DDS message
# generators. This target uses only generated C + micro-XRCE type support.
foreach(pkg rosidl_generator_py rosidl_generator_cpp rosidl_typesupport_cpp
    rosidl_typesupport_fastrtps_c rosidl_typesupport_fastrtps_cpp
    rosidl_typesupport_introspection_cpp)
  set(CMAKE_DISABLE_FIND_PACKAGE_${pkg} TRUE CACHE BOOL "Not a target micro-ROS dependency" FORCE)
endforeach()
set(STATIC_ROSIDL_TYPESUPPORT_C rosidl_typesupport_microxrcedds_c CACHE STRING "Static micro-ROS dispatch only" FORCE)
