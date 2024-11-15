#ifndef __DATACOLLECTION_H__
#define __DATACOLLECTION_H__

#include <chrono>
#include <string>

#include "udp_tx.hpp"

using namespace std;

class DataCollection{
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

        

        struct DataColectionMeta{
            string HWVers;
            uint8_t num_motors;
            uint8_t num_encoders;
            uint16_t data_buffer_size;
            uint16_t size_of_sample;
            uint16_t samples_per_packet;
        } dc_meta ;

        #define MAX_NUM_ENCODERS    8
        #define MAX_NUM_MOTORS      10

        struct ProcessedSample{
            float timestamp;
            int32_t encoder_position[MAX_NUM_ENCODERS];
            float encoder_velocity[MAX_NUM_ENCODERS];
            uint16_t motor_current[MAX_NUM_MOTORS];
            int16_t motor_status[MAX_NUM_MOTORS];
        }proc_sample;

        struct DC_Time{
            std::chrono::time_point<std::chrono::high_resolution_clock> start;
            std::chrono::time_point<std::chrono::high_resolution_clock> end;
            float elapsed;
        } curr_time ;

        // time variables for timed captures
        std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
        std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
        float time_elapsed;

        int sm_state;

        bool stop_data_collection_flag;

        bool collect_data_ret;

        bool isDataCollectionRunning;

        int data_capture_count = 1;

        int udp_data_packets_recvd_count = 0;

        ofstream myFile;

        string filename;

        int sock_id;
        #define METADATA_MAGIC_NUMBER   0xABCDEF12

        uint32_t data_buffer[UDP_REAL_MTU/4] = {0};

        bool collect_data();

        void load_meta_data(uint32_t *meta_data);

        // processes sample and uploads it to the 
        // proc sample struct
        void process_sample(uint32_t *data_buffer, int start_idx); 

        pthread_t collect_data_t;
    public:
        DataCollection();
        bool init(uint8_t boardID);
        bool start();
        bool stop();
        bool terminate();
};



#endif