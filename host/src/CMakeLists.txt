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

# Set the project name
project(dvrkDataCollection-HOST)

# Find the data collection library
find_package (dvrkDataCollection REQUIRED
              HINTS "${CMAKE_BINARY_DIR}/lib")

include_directories(${dvrkDataCollection_INCLUDE_DIR})

# Add the executable
add_executable(dvrkDataCollection-HOST dataCollection.cpp)

link_directories(${dvrkDataCollection_LIBRARY_DIR})
target_link_libraries(dvrkDataCollection-HOST PRIVATE ${dvrkDataCollection_LIBRARY})

# Set the output directories for the executable
set_target_properties(dvrkDataCollection-HOST PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Example to set specific output directories for debug and release
set_target_properties(dvrkDataCollection-HOST PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
)
