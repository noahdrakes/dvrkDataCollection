/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-    */
/* ex: set filetype=cpp softtabstop=4 shiftwidth=4 tabstop=4 cindent expandtab: */

/*
  Author(s):  Noah Drakes

  (C) Copyright 2024 Johns Hopkins University (JHU), All Rights Reserved.

--- begin cisst license - do not edit ---

This software is provided "as is" under an open source license, with
no warranty.  The complete license can be found in license.txt and
http://www.cisst.org/cisst/license.txt.

--- end cisst license ---
*/

#ifndef __DATACOLLECTIONSHARED_H__
#define __DATACOLLECTIONSHARED_H__

#include <stdio.h>
#include <stdint.h>
#include <string.h>

const unsigned int MAX_NUM_ENCODERS = 8;
const unsigned int MAX_NUM_MOTORS = 10;

const uint32_t METADATA_MAGIC_NUMBER = 0xABCDEF12;

// Default MTU=1500 (does not count 18 bytes for Ethernet frame header and CRC)
const unsigned int MTU_DEFAULT = 1500;

// Size of IPv4 header (20) and UDP header (8), counts against MTU limit
const unsigned int IP_UDP_HEADER = 28;

// correct this to value in EthUdpPort.cpp in unsigned int EthUdpPort::GetMaxReadDataSize(void) method
// PK: Figure out whether following should instead be MTU_DEFAULT-IP_UDP_HEADER (1472)
const unsigned int UDP_REAL_MTU = 1446;

const unsigned int UDP_MAX_QUADLET_PER_PACKET = UDP_REAL_MTU/4;

// Arbitrary, used as a const for consistency. Can be changed later
const unsigned int MAX_CMD_STRING_SIZE = 100;

struct DataCollectionMeta {
    uint32_t hwvers;
    uint32_t num_motors;
    uint32_t num_encoders;
    uint32_t data_packet_size;
    uint32_t size_of_sample;
    uint32_t samples_per_packet;
};

// State Machine Return Codes
enum StateMachineReturnCodes {
    SM_SUCCESS, 
    SM_UDP_ERROR, 
    SM_BOARD_ERROR,
    SM_ZYNQ_FAIL,
    SM_HOST_FAIL,
    SM_CAPTURE_TIMEOUT,
    SM_FAIL
};

// Commands Sent between Host and Zynq
    // HOST
    #define HOST_START_DATA_COLLECTION                      "HOST: START DATA COLLECTION"
    #define HOST_READY_CMD                                  "HOST: READY FOR DATA COLLECTION"
    #define HOST_READY_CMD_W_PS_IO                          "HOST: READY FOR DATA COLLECTION - WITH PS IO"
    #define HOST_RECVD_METADATA                             "HOST: RECEIVED METADATA"
    #define HOST_STOP_DATA_COLLECTION                       "HOST: STOP DATA COLLECTION"
    // ZYNQ
    #define ZYNQ_READY_CMD                                  "ZYNQ: READY FOR DATA COLLECTION"
    #define ZYNQ_TERMINATATION_SUCCESSFUL                   "ZYNQ: TERMINATION SUCCESSFUL"

#endif
