
cmake_minimum_required(VERSION 3.14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_COMPILER icpx)

project(abcd)
find_package(IntelDPCPP REQUIRED)

add_executable(abcd
  abcd.cc)

target_compile_options(abcd
  PRIVATE
  -Wall
  -O3
  -std=c++2b
  -sycl-std=2020)

target_link_libraries(abcd
  PRIVATE
  wayland-client)

add_custom_target(run
  DEPENDS abcd
  COMMAND WAYLAND_DEBUG=1 ./abcd)
