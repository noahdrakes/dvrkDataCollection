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

#ifndef __DATACOLLECTION_H__
#define __DATACOLLECTION_H__

#include <chrono>
#include <string>

#include "udp_tx.h"
#include "data_collection_shared.h"

using namespace std;

class DataCollection {
    private:
        static void * collect_data_thread(void * args);
    protected:
        // prevent copies
        DataCollection(const DataCollection &);
        DataCollection& operator=(const DataCollection&);

        enum DataCollectionStateMachine {
            SM_READY = 0,
            SM_SEND_READY_STATE_TO_PS,
            SM_WAIT_FOR_PS_HANDSHAKE,
            SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS,
            SM_START_DATA_COLLECTION,
            SM_RECV_DATA_COLLECTION_META_DATA,
            SM_SEND_METADATA_RECV,
            SM_CLOSE_SOCKET,
            SM_EXIT
        };

        struct ProcessedSample {
            float timestamp;
            int32_t encoder_position[MAX_NUM_ENCODERS];
            float encoder_velocity[MAX_NUM_ENCODERS];
            uint16_t motor_current[MAX_NUM_MOTORS];
            uint16_t motor_status[MAX_NUM_MOTORS];
            uint32_t digital_io;
        } proc_sample;

        struct DC_Time {
            std::chrono::time_point<std::chrono::high_resolution_clock> start;
            std::chrono::time_point<std::chrono::high_resolution_clock> end;
            float elapsed;
        } curr_time;

        // time variables for timed captures
        std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
        std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
        float time_elapsed;

        DataCollectionMeta dc_meta;

        int sm_state;

        bool stop_data_collection_flag;

        bool collect_data_ret;

        bool isDataCollectionRunning;

        int data_capture_count = 1;

        int udp_data_packets_recvd_count = 0;

        int packet_misses_counter = 0;

        ofstream myFile;

        string filename;

        int sock_id;

        uint32_t data_packet[UDP_REAL_MTU/4] = {0};

        int collect_data();

        void load_meta_data(uint32_t *meta_data);

        // processes sample and uploads it to the
        // proc sample struct
        void process_sample(uint32_t *data_packet, int start_idx);

        pthread_t collect_data_t;
    public:
        DataCollection();
        bool init(uint8_t boardID);
        bool start();
        bool stop();
        bool terminate();
};

#endif
