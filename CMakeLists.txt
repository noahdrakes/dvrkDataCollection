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
    lib/udp_tx.cpp
    lib/data_collection.cpp
)

# Add the executable
add_executable(dataCollection ${SOURCES})

# Specify the output directory for the executable
set_target_properties(dataCollection PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Example to set specific output directories for debug and release
set_target_properties(dataCollection PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release
)
