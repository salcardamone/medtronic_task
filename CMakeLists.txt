cmake_minimum_required(VERSION 3.22)
project(medtronic_task)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(
  "-O3" "-Wall" "-Wextra" "-Wno-unused-function"
  )

find_package(spdlog REQUIRED)

add_subdirectory(src)
add_subdirectory(test)
