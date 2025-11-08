cmake_minimum_required(VERSION 3.20)
project(wpan_capture_sim LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure the ns-3 install is discoverable (see PHASE_0.md)
list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/opt/ns3")
find_package(ns3 REQUIRED)

add_executable(wpan-capture-sim
  wpan_capture_sim.cc
  helpers/channel_export.cc
  apps/packet_sprayer.cc
  apps/raw_l2_sink.cc
)
target_include_directories(wpan-capture-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wpan-capture-sim PRIVATE
  ns3::core ns3::network ns3::mobility ns3::internet ns3::applications
  ns3::lr-wpan ns3::sixlowpan ns3::spectrum ns3::propagation
  ns3::network
)
