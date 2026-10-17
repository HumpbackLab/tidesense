find_package(Eigen3 REQUIRED)
cmake_language(DEFER CALL target_sources fast PRIVATE
  "${DEPS}/fast_neon/src/faster_corner_10_sse.cpp")
function(add_estimator_update_test)
  add_executable(schurvins_estimator_update_test
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/tests/estimator_update_test.cpp")
  target_link_libraries(schurvins_estimator_update_test PRIVATE schurvins)
endfunction()
cmake_language(DEFER CALL add_estimator_update_test)
