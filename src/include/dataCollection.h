#ifndef __DATACOLLECTION_H__
#define __DATACOLLECTION_H__

#include <chrono>
#include <string>

using namespace std;

struct DataColectionMeta{
    string HWVers;
    uint8_t num_motors;
    uint8_t num_encoders;
    uint16_t data_buffer_size;
    uint16_t size_of_sample;
    uint16_t samples_per_packet;
} dc_meta;

class DataCollection{
    protected:
        // prevent copies
        DataCollection(const DataCollection &);
        DataCollection& operator=(const DataCollection&);

        // time variables for timed captures
        std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
        std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
        float time_elapsed;

        DataColectionMeta dc_meta;
        #define METADATA_MAGIC_NUMBER   0xABCDEF12
    public:
        DataCollection();
        void getSum();

};



#endif