cmake_minimum_required(VERSION 3.10)
project(DataCollection)

set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SOURCES
    dataCollection.cpp
    udp_tx.cpp)

set(HEADER
    dataCollection.h
    udp_tx.hpp)

add_library(udp_lib STATIC udp_tx.cpp udp_tx.hpp)

add_executable(dataCollection dataCollection.cpp)

target_link_libraries(dataCollection PRIVATE udp_lib)