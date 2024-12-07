#
# (C) Copyright 2024 Johns Hopkins University (JHU), All Rights Reserved.
#
# --- begin cisst license - do not edit ---
#
# This software is provided "as is" under an open source license, with
# no warranty.  The complete license can be found in license.txt and
# http://www.cisst.org/cisst/license.txt.
#
# --- end cisst license ---

cmake_minimum_required(VERSION 3.10)

# Set the project name
project(dvrkDataCollection-LIB)

# Add the source files
set(SOURCES
    ../data_collection.h
    data_collection.cpp
    udp_tx.h
    udp_tx.cpp
)

find_package(Threads REQUIRED)

# Add the executable
add_library(dvrkDataCollection ${SOURCES})

target_link_libraries(dvrkDataCollection PRIVATE Threads::Threads)
# target_link_libraries (dvrkDataCollection  ${CMAKE_THREAD_LIBS_INIT})

set(THREADS_PREFER_PTHREAD_FLAG ON)

# Set the output directories for the executable
set_target_properties(dvrkDataCollection PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
