cmake_minimum_required(VERSION 3.13)
project(tidesense_writer_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
find_package(OpenCV REQUIRED COMPONENTS core)
find_package(yaml-cpp REQUIRED)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../install/dataset-deps/zstd-1.5.7/build/cmake" zstd)
add_library(writer_io_faults SHARED writer_io_faults.cpp)
target_link_libraries(writer_io_faults PRIVATE dl pthread)
add_executable(writer_pipeline_test writer_pipeline_test.cpp)
target_include_directories(writer_pipeline_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../schurvins"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../tide_cam/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../install/dataset-deps/zstd-1.5.7/lib"
  ${OpenCV_INCLUDE_DIRS})
target_link_libraries(writer_pipeline_test PRIVATE ${OpenCV_LIBS} yaml-cpp libzstd_static pthread dl)
target_compile_options(writer_pipeline_test PRIVATE -Wall -Wextra -Werror)
target_link_options(writer_pipeline_test PRIVATE -Wl,--wrap=ZSTD_compress2)
enable_testing()
foreach(mode blocked overflow error stored_corruption codec_corruption blocked_sync sync_error)
  add_test(NAME writer_${mode} COMMAND ${CMAKE_COMMAND} -E env
    "LD_PRELOAD=$<TARGET_FILE:writer_io_faults>"
    $<TARGET_FILE:writer_pipeline_test> ${mode} ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(writer_${mode} PROPERTIES TIMEOUT 15)
endforeach()

add_executable(preview_http_test preview_http_test.cpp preview_encoder_mock.cpp ../preview.cpp ../downloads.cpp)
target_include_directories(preview_http_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../schurvins"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../tide_cam/include"
  ${OpenCV_INCLUDE_DIRS})
target_link_libraries(preview_http_test PRIVATE ${OpenCV_LIBS} yaml-cpp pthread)
target_compile_options(preview_http_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME preview_http COMMAND preview_http_test)
set_tests_properties(preview_http PROPERTIES TIMEOUT 15)
