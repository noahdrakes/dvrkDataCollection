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

cmake_minimum_required (VERSION 3.10)

project (DataCollectionZynq VERSION 0.1.0)

find_package (Amp1394 REQUIRED)

# Add some warnings
include (CheckCXXCompilerFlag)
check_cxx_compiler_flag ("-Wextra" CXX_SUPPORTS_WEXTRA)
if (CXX_SUPPORTS_WEXTRA)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wextra")
endif ()

check_cxx_compiler_flag ("-Wall" CXX_SUPPORTS_WALL)
if (CXX_SUPPORTS_WALL)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif ()

check_cxx_compiler_flag ("-fPIC" CXX_SUPPORTS_FPIC)
if (CXX_SUPPORTS_FPIC)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
endif ()

if (Amp1394_HAS_EMIO)

  include_directories(${Amp1394_INCLUDE_DIR})
  include_directories("../shared")

  link_directories(${Amp1394_LIBRARY_DIR})

  add_executable(dvrkDataCollection-ZYNQ dvrkDataCollection-ZYNQ.cpp)
  target_link_libraries(dvrkDataCollection-ZYNQ ${Amp1394_LIBRARIES})

  install (TARGETS dvrkDataCollection-ZYNQ
           RUNTIME DESTINATION bin)

else ()

  message (FATAL_ERROR "Amp1394 library not cross-compiled with EMIO support")

endif ()
