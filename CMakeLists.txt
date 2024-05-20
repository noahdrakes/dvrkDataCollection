cmake_minimum_required(VERSION 3.10)
project(EthernetClient)

set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SOURCES
    ethernet_client.cpp
    udp_tx.cpp)

set(HEADER
    ethernet_client.hpp
    udp_tx.hpp)

add_library(udp_lib STATIC udp_tx.cpp udp_tx.hpp)

add_executable(ethernet_client ethernet_client.cpp)

target_link_libraries(ethernet_client PRIVATE udp_lib)