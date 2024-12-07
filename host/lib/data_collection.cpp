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
#include <cstring>
#include <pthread.h>
#include "../../shared/data_collection_shared.h"
using namespace std;


///////////////////////
// UTILITY METHODS //
///////////////////////

static float convert_chrono_duration_to_float(chrono::high_resolution_clock::time_point start, chrono::high_resolution_clock::time_point end ){
    std::chrono::duration<float> duration = end - start;
    return duration.count();
}

static uint32_t swap_endian(uint32_t value) {
    return ((value >> 24) & 0x000000FF) | 
           ((value >> 8)  & 0x0000FF00) | 
           ((value << 8)  & 0x00FF0000) | 
           ((value << 24) & 0xFF000000);  
}

static string return_filename(string filename){
    time_t t = time(NULL);
    struct tm* ptr = localtime(&t);

    string date_and_time = asctime(ptr);
    date_and_time.pop_back(); // remove newline character

    filename = "capture_" + date_and_time + ".csv";

    for (int i = 0; i < filename.length();i++){

        if (filename[i] == ' '){
            filename[i] = '_';
        } 
    }

    return filename;
}

static void hwVersToString(uint32_t val, char *str){
    val = swap_endian(val);

    const char *val_char =  reinterpret_cast<const char*> (&val); 
    char hw_vers[5];
    memcpy(str, val_char, 4);
    str[4] = '\0';
}


///////////////////////
// PROTECTED METHODS //
///////////////////////

void DataCollection:: process_sample(uint32_t *data_packet, int start_idx){

    if (start_idx + dc_meta.size_of_sample > UDP_MAX_QUADLET_PER_PACKET){
        return;
    }

    int idx = start_idx;

    proc_sample.timestamp = *reinterpret_cast<float *> (&data_packet[idx++]);

    for (int i = 0; i < dc_meta.num_encoders; i++){
        proc_sample.encoder_position[i] = *reinterpret_cast<int32_t *> (&data_packet[idx++]);
    }

    for (int i = 0; i < dc_meta.num_encoders; i++){
        proc_sample.encoder_velocity[i] = *reinterpret_cast<float *> (&data_packet[idx++]);
    }

    for (int i = 0; i < dc_meta.num_motors; i++){
        proc_sample.motor_status[i] = static_cast<int16_t> ((0xFFFF0000 & data_packet[idx]) >> 16);
        proc_sample.motor_current[i] = (uint16_t) (0x0000FFFF & data_packet[idx]);
        idx++;
        
    }

    proc_sample.digital_io = data_packet[idx++];
  
}

int DataCollection :: collect_data(){

    if (isDataCollectionRunning){
        collect_data_ret = false;
        return false;
    }

    printf("CAPTURE [%d] in Progress ... ! \n", data_capture_count);

    isDataCollectionRunning = true;
    stop_data_collection_flag = false;

    sm_state = SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS;

    while(sm_state != SM_EXIT){

        switch(sm_state){
            case SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS:{

                udp_transmit(sock_id, (char *) HOST_START_DATA_COLLECTION, 28);

                sm_state = SM_START_DATA_COLLECTION;
                break;
            }

            case SM_START_DATA_COLLECTION:{
                curr_time.start = std::chrono::high_resolution_clock::now();
                float time_elapsed = 0; 
                int count = 0;

                udp_data_packets_recvd_count = 0;
                
                filename = return_filename(filename);
                myFile.open(filename);
        
                memset(data_packet, 0, sizeof(data_packet));

                myFile << "TIMESTAMP,";

                for (int i = 1; i < dc_meta.num_encoders + 1; i++){
                    myFile << "ENCODER_POS_" << i << ",";
                }

                for (int i = 1; i < dc_meta.num_encoders + 1; i++){
                    myFile << "ENCODER_VEL_" << i << ",";
                }

                for (int i = 1; i < dc_meta.num_motors + 1; i++){
                    myFile << "MOTOR_CURRENT_" << i << ",";
                }

                for (int i = 1; i < dc_meta.num_motors + 1; i++){
                    myFile << "MOTOR_STATUS_" << i  << ",";
                }

                myFile << "DIGITAL_IO" << "\n";
                
                while(!stop_data_collection_flag ){
                    
                    // look here maybe
                    int ret_code = udp_nonblocking_receive(sock_id, data_packet, dc_meta.data_packet_size);

                    if (ret_code > 0){
                        
                        udp_data_packets_recvd_count++;
                        packet_misses_counter = 0;

                            count++;

                            for (int i = 0; i < dc_meta.data_packet_size / 4 ; i+= dc_meta.size_of_sample){
                            
                                process_sample(data_packet, i);
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
                                    myFile << proc_sample.motor_status[j] << ",";
                                }

                                myFile << proc_sample.digital_io << "\n";

                                memset(&proc_sample, 0, sizeof(proc_sample));

                            }
                    } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT) {
                        packet_misses_counter++;

                        if (packet_misses_counter == 100000 && udp_data_packets_recvd_count != 0){
                            cout << "[ERROR] Capture timeout. 100,000 data packet misses" << endl;
                            cout << "Restart Zynq and Host programs" << endl;
                            return SM_CAPTURE_TIMEOUT;
                        }
                    } else {
                        cout << "[ERROR] UDP ERROR (ret code: " << ret_code << "). Check connection if zynq program failed" << endl; 
                    }                   
                }

                sm_state = SM_EXIT;
                break;
            }

            // TODO: need to add close socket protocol back to client
            // right now the socket isn't closing at all
            // prob dependent on isDataCollectionRunningFlag
        }
    }

    return true;
}

void * DataCollection::collect_data_thread(void * args){
    DataCollection *dc = static_cast<DataCollection *>(args);

    dc->collect_data();
    return nullptr;
}



////////////////////
// PUBLIC METHODS //
////////////////////


DataCollection::DataCollection(){
    cout << "New Data Collection Object !" << endl << endl;
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
                ret_code = udp_nonblocking_receive(sock_id, &dc_meta, sizeof(meta_data));
                if (ret_code > 0){
                    if (dc_meta.magic_number == METADATA_MAGIC_NUMBER){
                        cout << "Received Message from Zynq: RECEIVED METADATA" << endl << endl;
                        
                        char hw_vers[5];
                        hwVersToString(dc_meta.hwvers, hw_vers);

                        cout << "---- DATA COLLECTION METADATA ---" << endl;
                        cout << "Hardware Version: " << hw_vers << endl;
                        cout << "Num of Encoders:  " <<  +dc_meta.num_encoders << endl;
                        cout << "Num of Motors: " << +dc_meta.num_motors << endl;
                        cout << "Packet Size (in bytes): " << dc_meta.data_packet_size << endl;
                        cout << "Samples per Packet: " << dc_meta.samples_per_packet << endl;
                        cout << "Sizoef Samples (in quadlets): " << dc_meta.size_of_sample << endl;
                        cout << "----------------------------------" << endl << endl;
                        
                        sm_state = SM_SEND_METADATA_RECV;
                        break;
                    } else {
                        cout << "[ERROR] Host data collection is out of sync with Zynq State Machine. Restart Zynq and Host Program";
                        cout << "Recvd Data: " << meta_data << endl;
                        sm_state = SM_CLOSE_SOCKET;
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT || ret_code == UDP_NON_UDP_DATA_IS_AVAILABLE){
                    sm_state = SM_RECV_DATA_COLLECTION_META_DATA;
                    break;
                } else {
                    cout << "[ERROR] - UDP fail, Check connection if zynq program failed" << endl;
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
                        cout << "Received Message " << ZYNQ_READY_CMD << endl;
                        sm_state = SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS;
                        return true; 
                    } else {
                        cout << "[ERROR] Host data collection is out of sync with Processor State Machine. Restart Server";
                        sm_state = SM_CLOSE_SOCKET;
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT || ret_code == UDP_NON_UDP_DATA_IS_AVAILABLE){
                    sm_state = SM_WAIT_FOR_PS_HANDSHAKE;
                    break;
                } else {
                    cout << "[ERROR] - UDP fail, Check connection if zynq program failed" << endl;
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

    return true;
}

bool DataCollection :: stop(){

    // send end data collection cmd
    if( !udp_transmit(sock_id,(char *) HOST_STOP_DATA_COLLECTION, sizeof(HOST_STOP_DATA_COLLECTION)) ){
        cout << "[ERROR]: UDP error. Check connection if zynq program failed!" << endl; // more descriptive eror message
    }

    usleep(1000);

    isDataCollectionRunning = false;
     
    stop_data_collection_flag = true;

    pthread_join(collect_data_t, nullptr);

    // clear the udp buffer by reading until the buffer is empty
    while( udp_nonblocking_receive(sock_id, data_packet, dc_meta.data_packet_size) > 0){}

    myFile.close();

    curr_time.end = std::chrono::high_resolution_clock::now();
    curr_time.elapsed = convert_chrono_duration_to_float(curr_time.start, curr_time.end);


    cout << "---------------------------------------------------------" << endl;
    printf("STOPPED CAPTURE [%d] ! Time Elapsed: %fs\n", data_capture_count++, curr_time.elapsed);
    cout << "Data stored to " << filename << "." << endl;
    
    cout << "---------------------------------------------------------" << endl << endl;

    collect_data_ret = true;

    usleep(1000);
    
    return true;
}


bool DataCollection :: terminate(){
    char terminateClientAndServerCmd[] = "CLIENT: Terminate Server";
    char recvBuffer[100] = {0};

    if( !udp_transmit(sock_id, terminateClientAndServerCmd, sizeof(terminateClientAndServerCmd)) ){
        cout << "[ERROR]: UDP error. check connection with host!" << endl;
    }

    while (1){
        int ret = udp_nonblocking_receive(sock_id, recvBuffer, 31);

        if (ret > 0){
            if(strcmp(recvBuffer,  ZYNQ_TERMINATATION_SUCCESSFUL) == 0){
                cout << "Received Message:  " << ZYNQ_TERMINATATION_SUCCESSFUL << endl;
                break;
            } else {
                // need something here
            }
        } else if (ret == UDP_SELECT_ERROR || ret == UDP_SOCKET_ERROR || ret == UDP_CONNECTION_CLOSED_ERROR) {
                cout << "Termination Failed: Check UDP connetion" << endl;
                return false;
            }   
    }

    close(sock_id);
    return true;
}


