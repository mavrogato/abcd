
cmake_minimum_required(VERSION 3.14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_COMPILER icx)
set(CMAKE_CXX_COMPILER icpx)
set(CMAKE_CXX_FLAGS "-std=c++2b -sycl-std=2020")

project(abcd)
find_package(IntelDPCPP REQUIRED)

add_executable(abcd
  xdg-shell-private.c
  abcd.cc)

target_compile_options(abcd
  PRIVATE
  -Wall
  -O3)

target_link_libraries(abcd
  PRIVATE
  wayland-client)

add_custom_target(run
  DEPENDS abcd
  COMMAND WAYLAND_DEBUG=1 ./abcd)

add_custom_target(scan
  COMMAND wayland-scanner private-code ../xdg-shell-unstable-v6.xml ../xdg-shell-private.c
  COMMAND wayland-scanner client-header ../xdg-shell-unstable-v6.xml ../xdg-shell-client.h)
