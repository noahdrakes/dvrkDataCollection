#
# (C) Copyright 2011-2024 Johns Hopkins University (JHU), All Rights Reserved.
#
# --- begin cisst license - do not edit ---
#
# This software is provided "as is" under an open source license, with
# no warranty.  The complete license can be found in license.txt and
# http://www.cisst.org/cisst/license.txt.
#
# --- end cisst license ---

project(Amp1394Tests VERSION 2.1.0)

include_directories(${Amp1394_INCLUDE_DIR})
include_directories(${CMAKE_SOURCE_DIR}../shared)


link_directories(${Amp1394_LIBRARY_DIR} ${Amp1394_EXTRA_LIBRARY_DIR})

add_executable(dvrkDataCollection-ZYNQ dvrkDataCollection-ZYNQ.cpp)
target_link_libraries(dvrkDataCollection-ZYNQ ${Amp1394_LIBRARIES} ${Amp1394_EXTRA_LIBRARIES}
                              ${Amp1394Console_LIBRARIES} ${Amp1394Console_EXTRA_LIBRARIES})                                    

install (TARGETS dvrkDataCollection-ZYNQ
         COMPONENT Amp1394-utils
         RUNTIME DESTINATION bin)

# add_executable (block1394eth block1394eth.cpp)
# target_link_libraries (block1394eth ${Amp1394_LIBRARIES} ${Amp1394_EXTRA_LIBRARIES})

# add_custom_command (TARGET block1394eth POST_BUILD
#                      COMMAND ${CMAKE_COMMAND} -E copy
#                         ${EXECUTABLE_OUTPUT_PATH}/${CMAKE_CFG_INTDIR}/block1394eth${CMAKE_EXECUTABLE_SUFFIX}
#                         ${EXECUTABLE_OUTPUT_PATH}/${CMAKE_CFG_INTDIR}/quad1394eth${CMAKE_EXECUTABLE_SUFFIX}
#                      COMMENT "Generating quad1394eth")
