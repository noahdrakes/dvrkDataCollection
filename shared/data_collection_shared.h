#ifndef __DATACOLLECTIONSHARED_H__
#define __DATACOLLECTIONSHARED_H__

#include <stdio.h>
#include <stdint.h>
#include <string.h>

using namespace std;

#define MAX_NUM_ENCODERS    8
#define MAX_NUM_MOTORS      10

const uint32_t METADATA_MAGIC_NUMBER = 0xABCDEF12;

struct DataCollectionMeta{
    uint32_t magic_number;
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
    #define HOST_START_DATA_COLLECTION "HOST: START DATA COLLECTION"
    #define HOST_READY_CMD  "HOST: READY FOR DATA COLLECTION"
    #define HOST_RECVD_METADATA "HOST: RECEIVED METADATA"
    #define HOST_STOP_DATA_COLLECTION "HOST: STOP DATA COLLECTION"
    // ZYNQ
    #define ZYNQ_READY_CMD "ZYNQ: READY FOR DATA COLLECTION"
    #define ZYNQ_TERMINATATION_SUCCESSFUL "ZYNQ: TERMINATION SUCCESSFUL"

#endif