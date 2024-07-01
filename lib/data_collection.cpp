#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <cstdlib>
#include <getopt.h>
#include "udp_tx.hpp"
#include <sys/select.h>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <string>
#include "data_collection.hpp"
#include <time.h>
#include "stdio.h"
using namespace std;

///////////////////////
// PROTECTED METHODS //
///////////////////////

static float calculate_duration_as_float(chrono::high_resolution_clock::time_point start, chrono::high_resolution_clock::time_point end ){
    std::chrono::duration<float> duration = end - start;
    return duration.count();
}


void DataCollection::load_meta_data(uint32_t *meta_data){
    // store meta data into meta data struct
    string hwVersString;

    switch(meta_data[1]){

        case 0x514C4131:{
            hwVersString = "QLA1";
            break;
        case 0x64524131:
            hwVersString = "dRA1";
            break;
        case 0x44514C41:
            hwVersString = "dQLA";
            break;
        default:
            cout << "error" << endl;
            hwVersString = "ERROR";
            break;

        }
    };

    dc_meta.HWVers = hwVersString;
    dc_meta.num_encoders = (uint8_t) meta_data[2];
    dc_meta.num_motors = (uint8_t) meta_data[3];
    dc_meta.data_buffer_size = meta_data[4];
    dc_meta.size_of_sample = meta_data[5];
    dc_meta.samples_per_packet = meta_data[6];
}

void DataCollection:: process_sample(uint32_t *data_buffer, int start_idx){

    if (start_idx + dc_meta.size_of_sample > UDP_MAX_QUADLET_PER_PACKET){
        return;
    }

    int idx = start_idx;

    proc_sample.timestamp = *reinterpret_cast<float *> (&data_buffer[idx++]);
    // cout << "timestamp: " << proc_sample.timestamp << endl;
    

    for (int i = 0; i < dc_meta.num_encoders; i++){
        proc_sample.encoder_position[i] = *reinterpret_cast<int32_t *> (&data_buffer[idx++]);
        // printf("encoder_pos[%d] = 0x%X\n",i,  proc_sample.encoder_position[i]);
        proc_sample.encoder_velocity[i] = *reinterpret_cast<float *> (&data_buffer[idx++]);
        // printf("encoder_velocity[%d] = %f\n", i, proc_sample.encoder_velocity[i]);
    }

    for (int i = 0; i < dc_meta.num_motors; i++){
        proc_sample.motor_status[i] = (uint16_t) ((0xFFFF0000 & data_buffer[idx]) >> 16);
        // printf("motor status[%d] = 0x%X\n", i, proc_sample.motor_status[i]);
        proc_sample.motor_current[i] = (uint16_t) (0xFFFF) & data_buffer[idx];
        // printf("motor current[%d] = 0x%X\n", i,  proc_sample.motor_current[i]);
    }
}

bool DataCollection :: collect_data(){

    printf("enter collect data\n");

    if (isDataCollectionRunning){
        collect_data_ret = false;
        return false;
    }

    isDataCollectionRunning = true;
     stop_data_collection_flag = false;
    

    char startDataCollectionCMD[] = "HOST: START DATA COLLECTION";

    sm_state = SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS;

    std::cout << "finna start data collection" << endl;

    while(sm_state != SM_EXIT){

        switch(sm_state){
            case SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS:{
                std::cout << "sent start data collection command" << endl;

                // clear udp buffer
                while( udp_nonblocking_receive(sock_id, data_buffer, dc_meta.data_buffer_size) == UDP_DATA_IS_AVAILBLE){}

                

                udp_transmit(sock_id, startDataCollectionCMD, 28);

                

                sm_state = SM_START_DATA_COLLECTION;
                break;
                
            }

            case SM_START_DATA_COLLECTION:{
                curr_time.start = std::chrono::high_resolution_clock::now();
                float time_elapsed = 0; 
                int count = 0;

                char endDataCollectionCmd[] = "CLIENT: STOP_DATA_COLLECTION";
                uint32_t temp = 0;

                // std::cout << "stop data collection flag: " << stop_data_collection_flag << endl;

                ofstream myFile;

                
                time_t t = time(NULL);
                struct tm* ptr = localtime(&t);

                string date_and_time = asctime(ptr);
                date_and_time.pop_back(); // remove newline character

                string filename = "fe_data | " + date_and_time + ".csv";
                myFile.open(filename);

               

                memset(data_buffer, 0, sizeof(data_buffer));

                

                

            
                while(!stop_data_collection_flag){
                    
                    // look here maybe
                    int ret_code = udp_nonblocking_receive(sock_id, data_buffer, dc_meta.data_buffer_size);

                    if (ret_code > 0){
                        
                        // might need to use timestamp to verify that this is truly new data
                        // index 0 is always the time stamp
                        // so im confirming that the timestamp from this packet and the previous 
                        // packet are not the same
                        if (temp != data_buffer[0] && (data_buffer[0]!=0 )){
                            
                            // for (int i = 0; i < 350; i++){
                            //     printf("databuffer[%d] = %d\n", i, data_buffer[i]);
                            // }

                            // while(1){}

                            count++;
                            for (int i = 0; i < dc_meta.data_buffer_size / 4 ; i+= dc_meta.size_of_sample){
                                
                                // for (int j = i; j < i + dc_meta.size_of_sample; j++){

                                //     // printf("data_buffer[%d] = %d\n", j, data_buffer[j]);

                                

                                //     myFile << data_buffer[j];

                                //     if (j != (i + dc_meta.size_of_sample - 1)){
                                //         myFile << ",";
                                //     }
                                // }

                                process_sample(data_buffer, i);
                                myFile << proc_sample.timestamp << ",";

                                for (int j = 0; j < dc_meta.num_encoders; j++){
                                    myFile << proc_sample.encoder_position[j] << ",";
                                }

                                for (int j = 0; j < dc_meta.num_encoders; j++){
                                    myFile << proc_sample.encoder_velocity[j] << ",";
                                }

                                for (int j = 0; j < dc_meta.num_motors; j++){
                                    myFile << proc_sample.motor_current[j] << ",";
                                }

                                for (int j = 0; j < dc_meta.num_motors; j++){
                                    myFile << proc_sample.motor_status[j];

                                    if (j < (dc_meta.num_motors - 1)){
                                        myFile << ",";
                                    }
                                }


                                myFile << "\n";

                                memset(&proc_sample, 0, sizeof(proc_sample));

                            }

                            
                        }          

                        // while(1){}              
                    // check for udp errors
                    } 

                    // TODO: NEED TO ADD AN ERROR COUNTER TO TERMINATE CLIENT WHEN NO DATA IS AVAILABLE FOR LIKE
                    // 1000 PASSES. 
                    
                    temp = data_buffer[0]; 
                }

                myFile.close();

                cout << "here" << endl;

                // clear the udp buffer by reading until the buffer is empty
               

                curr_time.end = std::chrono::high_resolution_clock::now();
                curr_time.elapsed = calculate_duration_as_float(curr_time.start, curr_time.end);


                cout << "DATA COLLECTION COMPLETE! Time Elapsed: " << curr_time.elapsed << "s" << endl;
                cout << "data stored to data.csv" << endl;
                cout << "count: " << count << endl;

                collect_data_ret = true;
                // sm_state = SM_CLOSE_SOCKET;
                sm_state = SM_EXIT;
                break;
            }

            // TODO: need to add close socket protocol back to client
            // right now the socket isn't closing at all
            // prob dependent on isDataCollectionRunningFlag

            // case SM_CLOSE_SOCKET:{

            //     if (!collect_data_ret){
            //         cout << "[UDP_ERROR] - return code:  | Make sure that server application is executing on the processor! The udp connection may closed." << endl;
            //         close(sock_id);
            //         return collect_data_ret;
            //     }

            //     cout << "closing socket" << endl;
            //     close(sock_id);
            //     sm_state = SM_EXIT;
            //     break;
            // }
        }
    }

    cout << "do we return true" << endl;

    return true;
}

void * DataCollection::collect_data_thread(void * args){
    DataCollection *dc = static_cast<DataCollection *>(args);

    cout << "after casting void pointer to data collection object" << endl;
    dc->collect_data();
    return nullptr;
}



////////////////////
// PUBLIC METHODS //
////////////////////


DataCollection::DataCollection(){
    cout << "New Data Collection Object !" << endl;
    isDataCollectionRunning = false;
    stop_data_collection_flag = false;
}

// TODO: need to add useful return statements -> all the close socket cases are just returns
// make sure logic checks out 
bool DataCollection :: init(uint8_t boardID){    

    if(!udp_init(&sock_id, boardID)){
        return false;
    }
 
    sm_state = SM_SEND_READY_STATE_TO_PS;
    int ret_code = 0;

    char sendReadyStateCMD[] = "HOST: READY FOR DATA COLLECTION";
    char sendMetaDataRecvd[] = "HOST: RECEIVED METADATA";
    
    char recvBuffer[100] = {0}; 
    uint32_t meta_data[7] = {0};
    bool error_flag = false;
    
    // Handshaking PS
    while(1){
        switch(sm_state){
            case SM_SEND_READY_STATE_TO_PS:{
                udp_transmit(sock_id, sendReadyStateCMD , strlen(sendReadyStateCMD));
                sm_state = SM_RECV_DATA_COLLECTION_META_DATA;
                break;
            }

            case SM_RECV_DATA_COLLECTION_META_DATA:{
                ret_code = udp_nonblocking_receive(sock_id, meta_data, sizeof(meta_data));
                if (ret_code > 0){
                    if (meta_data[0] == METADATA_MAGIC_NUMBER){
                        cout << "Received Message from Zynq: RECEIVED METADATA" << endl;

                        load_meta_data(meta_data);

                        cout << "- DATA COLLECTION METADATA -" << endl;
                        cout << "Hardware Version: " << dc_meta.HWVers << endl;
                        cout << "Num of Encoders:  " <<  +dc_meta.num_encoders << endl;
                        cout << "Num of Motors: " << +dc_meta.num_motors << endl;
                        cout << "DataBuffer Size: " << dc_meta.data_buffer_size << endl;
                        cout << "Samples per Packet: " << dc_meta.samples_per_packet << endl;
                        cout << "Sizoef Samples (in bytes): " << dc_meta.size_of_sample << endl;
                        
                        sm_state = SM_SEND_METADATA_RECV;
                        break;
                    } else {
                        cout << "[ERROR] Host data collection is out of sync with Processor State Machine. Restart Server";
                        sm_state = SM_CLOSE_SOCKET;
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    sm_state = SM_SEND_READY_STATE_TO_PS;
                    break;
                } else {
                    // sm_state = SM_CLOSE_SOCKET;
                    cout << "[ERROR] - UDP fail, check connection with processor" << endl;
                    sm_state = SM_CLOSE_SOCKET;
                    break;
                }
            }

            case SM_SEND_METADATA_RECV:{
                udp_transmit(sock_id, sendMetaDataRecvd, sizeof(sendMetaDataRecvd));
                sm_state = SM_WAIT_FOR_PS_HANDSHAKE;
                break;
            }

            case SM_WAIT_FOR_PS_HANDSHAKE:{
                ret_code = udp_nonblocking_receive(sock_id, recvBuffer, sizeof(recvBuffer));
                if (ret_code > 0){
                    if (strcmp(recvBuffer, "ZYNQ: READY FOR DATA COLLECTION") == 0){
                        cout << "Received Message from Zynq: READY FOR DATA COLLECTION" << endl;
                        sm_state = SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS;
                        return true; 
                    } else {
                        cout << "[ERROR] Host data collection is out of sync with Processor State Machine. Restart Server";
                        sm_state = SM_CLOSE_SOCKET;
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    sm_state = SM_SEND_METADATA_RECV;
                    break;
                } else {
                    cout << "[ERROR] - UDP fail, check connection with processor" << endl;
                    sm_state = SM_CLOSE_SOCKET;
                    break;
                }
            } 

            case SM_CLOSE_SOCKET:{
                close(sock_id);
                return false;
            }
        }
    }
}


// TODO: need to call collect_data() in this start() function 
    // needs to be called as a seperate thread and detached 
    // then stop just sets the stop_data_collection flag to true
bool DataCollection :: start(){

    if (pthread_create(&collect_data_t, nullptr, DataCollection::collect_data_thread, this) != 0) {
        std::cerr << "Error collect data thread" << std::endl;
        return 1;
    }

    pthread_detach(collect_data_t);

    return true;
}

bool DataCollection :: stop(){

    

    char endDataCollectionCmd[] = "CLIENT: STOP_DATA_COLLECTION";

    // if (!isDataCollectionRunning){
    //     cout << "[ERROR] Data Collection is not running" << endl;
    //     return false;
    // }


    isDataCollectionRunning = false;
     
    stop_data_collection_flag = true;

    pthread_join(collect_data_t, nullptr);

   

    

    // send end data collection cmd
    if( !udp_transmit(sock_id, endDataCollectionCmd, sizeof(endDataCollectionCmd)) ){
        cout << "[ERROR]: UDP error. check connection with host!" << endl;
    }

     

    

    cout << "STOP DATA COLLECTION" << endl;

    

    

    

    usleep(1000000);

    

    
    return true;
}


bool DataCollection :: terminate(){
    char terminateClientAndServerCmd[] = "CLIENT: Terminate Server";
    char recvBuffer[100] = {0};

    if( !udp_transmit(sock_id, terminateClientAndServerCmd, sizeof(terminateClientAndServerCmd)) ){
        cout << "[ERROR]: UDP error. check connection with host!" << endl;
    }

    while (1){
        int ret = udp_nonblocking_receive(sock_id, recvBuffer, 100);

        if (ret > 0){
            if(strcmp(recvBuffer, "Server: Termination Successful") == 0){
                cout << "termination sucessful" << endl;
                break;
            } 
            
            else {
                cout << "bad data brahhhh" << endl;
                printf("data: %s", recvBuffer);
                return false;
            }
        }        
    }

    close(sock_id);
    return true;
}


