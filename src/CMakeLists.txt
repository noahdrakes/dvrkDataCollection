cmake_minimum_required(VERSION 3.10)

# Set the project name
project(DVRKDATACOLLECTION)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src/include)
include_directories(${CMAKE_SOURCE_DIR}/lib)

# Add the source files
set(SOURCES
    src/dataCollection.cpp
    src/ethernet_server.cpp
    lib/udp_tx.cpp
)

# Add the executable
add_executable(DVRKDATACOLLECTION ${SOURCES})

# Set the output directories for the executable
set_target_properties(DVRKDATACOLLECTION PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Example to set specific output directories for debug and release
set_target_properties(DVRKDATACOLLECTION PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
)
