add_executable(medtronic_task main.cpp)
# Project headers are included publically while LLVM is private
target_include_directories(medtronic_task
  PUBLIC ${PROJECT_SOURCE_DIR}
  )
# May need this to be public for unit testing in the future
target_link_libraries(medtronic_task
  PRIVATE spdlog::spdlog
  )
