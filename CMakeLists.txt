
cmake_minimum_required(VERSION 3.14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_COMPILER icx)
set(CMAKE_CXX_COMPILER icpx)
set(CMAKE_CXX_FLAGS "-std=c++2b -sycl-std=2020")
set(PROTOCOL_DIR "/usr/share/wayland-protocols/unstable/xdg-shell/")

project(abcd)
find_package(IntelDPCPP REQUIRED)

add_custom_command(
  OUTPUT xdg-shell-private.c
  COMMAND wayland-scanner client-header ${PROTOCOL_DIR}/xdg-shell-unstable-v6.xml xdg-shell-client.h
  COMMAND wayland-scanner private-code ${PROTOCOL_DIR}/xdg-shell-unstable-v6.xml xdg-shell-private.c)

include_directories(
  ${CMAKE_CURRENT_BINARY_DIR})

add_executable(abcd
  abcd.cc
  ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-private.c)

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

