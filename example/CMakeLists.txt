cmake_minimum_required(VERSION 3.20)
project(line_wsn LANGUAGES CXX)

# Set C++20 for compatibility with ns3 headers that use std::remove_cvref_t
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tell CMake where to find the installed ns-3 package
list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/opt/ns3")

find_package(ns3 REQUIRED)  # provides imported targets ns3::*

add_executable(line-lr-wpan src/line-lr-wpan.cc)
target_link_libraries(line-lr-wpan PRIVATE
  ns3::core
  ns3::network
  ns3::mobility
  ns3::internet
  ns3::applications
  ns3::lr-wpan
  ns3::sixlowpan
  ns3::spectrum
)

