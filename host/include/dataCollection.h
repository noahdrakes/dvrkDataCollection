#ifndef __DATACOLLECTION_H__
#define __DATACOLLECTION_H__

#include <chrono>
#include <string>
#include "../../shared/data_collection_shared.h"

using namespace std;

// struct DataColectionMeta{
//     string HWVers;
//     uint8_t num_motors;
//     uint8_t num_encoders;
//     uint16_t data_buffer_size;
//     uint16_t size_of_sample;
//     uint16_t samples_per_packet;
// } dc_meta;

struct DC_Time{
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    std::chrono::time_point<std::chrono::high_resolution_clock> end;
    float elapsed;
};

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

        // time variables for timed captures
        std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
        std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
        float time_elapsed;

        DC_Time time;
        DataColectionMeta dc_meta;
        int sm_state;

        bool stop_data_collection_flag;

        bool collect_data_ret;

        bool isDataCollectionRunning;

        int sock_id;
        #define METADATA_MAGIC_NUMBER   0xABCDEF12

        uint32_t data_buffer[UDP_REAL_MTU/4] = {0};

        bool collect_data();

        void load_meta_data(uint32_t *meta_data);

        pthread_t collect_data_t;
    public:
        DataCollection();
        bool init(uint8_t boardID);
        bool start();
        bool stop();
};



#endif