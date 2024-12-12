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
project(dvrkDataCollection-ALL)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Shared header file (with Zynq)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../shared)

# Data collection library
add_subdirectory(lib/code)

# Data collection application
add_subdirectory(src)
