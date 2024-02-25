enable_testing()
find_package(GTest REQUIRED)

# Pile all of our unit tests into a single executable
add_executable(medtronic_task_tests
  test_remote_logger.cpp
)
# Project headers are included publically while LLVM is private
target_include_directories(medtronic_task_tests
  PUBLIC ${PROJECT_SOURCE_DIR}
  )
target_link_libraries(medtronic_task_tests PRIVATE
   GTest::gtest_main GTest::gmock_main spdlog::spdlog
   )

# Discover all the tests for CTest 
include(GoogleTest)
gtest_discover_tests(medtronic_task_tests)
