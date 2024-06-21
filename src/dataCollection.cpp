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
#include "dataCollection.h"

using namespace std;


const char *dataCollectionCMD;

std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
std::chrono::time_point<std::chrono::high_resolution_clock> end_time;



// NOTE:
// HOST_IP_ADDRESS = "169.254.255.252"
// PS IP ADDRESS    (ETH1) = "169.254.10.0"  

// needs to be running continuously 
// could program to start on key press 
// could be in one big while loop -> NEED TO BE NON-BLOCKING 

// could use relative time 
    // call getitme function on start 
    // for each packet record relative to start



DataCollection::DataCollection(){
    cout << "New Data Collection Object !" << endl;
    isDataCollectionRunning = false;
    stop_data_collection_flag = false;
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

// TODO: need to add useful return statements -> all the close socket cases are just returns
// make sure logic checks out 
bool DataCollection :: init(uint8_t boardID){    

    udp_init(&sock_id, boardID);
 
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
                if (ret_code == UDP_DATA_IS_AVAILBLE){
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
                if (ret_code == UDP_DATA_IS_AVAILBLE){
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

static float calculate_duration_as_float(chrono::high_resolution_clock::time_point start, chrono::high_resolution_clock::time_point end ){
    std::chrono::duration<float> duration = end - start;
    return duration.count();
}


bool DataCollection :: collect_data(){

    printf("enter collect data\n");

    if (isDataCollectionRunning){
        collect_data_ret = false;
        return false;
    }

    isDataCollectionRunning = true;

    char startDataCollectionCMD[] = "HOST: START DATA COLLECTION";

    while(sm_state != SM_EXIT){

        switch(sm_state){
            case SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS:{
                udp_transmit(sock_id, startDataCollectionCMD, 28);
                sm_state = SM_START_DATA_COLLECTION;
                break;
            }

            case SM_START_DATA_COLLECTION:{
                time.start = std::chrono::high_resolution_clock::now();
                float time_elapsed = 0; 
                int count = 0;

                char endDataCollectionCmd[] = "CLIENT: STOP_DATA_COLLECTION";
                uint32_t temp = 0;

                ofstream myFile;
                myFile.open("data.csv");
                

                while(!stop_data_collection_flag){

                    int ret_code = udp_nonblocking_receive(sock_id, data_buffer, dc_meta.data_buffer_size);

                    if (ret_code == UDP_DATA_IS_AVAILBLE){
                        
                        // might need to use timestamp to verify that this is truly new data
                        // index 0 is always the time stamp
                        // so im confirming that the timestamp from this packet and the previous 
                        // packet are not the same
                        if (temp != data_buffer[0]){

                            count = 0;
                            for (int i = 0; i < dc_meta.data_buffer_size / 4 ; i+= dc_meta.size_of_sample){
                                
                                for (int j = i; j < i + dc_meta.size_of_sample; j++){
                                    myFile << data_buffer[j];

                                    if (j != (i + dc_meta.size_of_sample - 1)){
                                        myFile << ",";
                                    }
                                }
                                myFile << "\n";

                            }
                        }                        
                    // check for udp errors
                    } else if (ret_code != UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                        collect_data_ret = false;
                        return false;
                    }
                    
                    temp = data_buffer[0]; 
                }

                myFile.close();

                cout << "here" << endl;

                time.end = std::chrono::high_resolution_clock::now();
                time.elapsed = calculate_duration_as_float(time.start, time.end);


                cout << "DATA COLLECTION COMPLETE! Time Elapsed: " << time.elapsed << "s" << endl;
                cout << "data stored to data.csv" << endl;

                collect_data_ret = true;
                sm_state = SM_CLOSE_SOCKET;
                break;
            }

            case SM_CLOSE_SOCKET:{

                if (!collect_data_ret){
                    cout << "[UDP_ERROR] - return code:  | Make sure that server application is executing on the processor! The udp connection may closed." << endl;
                    close(sock_id);
                    return collect_data_ret;
                }

                cout << "closing socket" << endl;
                close(sock_id);
                sm_state = SM_EXIT;
                break;
            }
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

// TODO: need to call collect_data() in this start() function 
    // needs to be called as a seperate thread and detached 
    // then stop just sets the stop_data_collection flag to true
bool DataCollection :: start(){

    if (pthread_create(&collect_data_t, nullptr, DataCollection::collect_data_thread, this) != 0) {
        std::cerr << "Error collect data thread" << std::endl;
        return 1;
    }

   

    return 0;
}

bool DataCollection :: stop(){

    char endDataCollectionCmd[] = "CLIENT: STOP_DATA_COLLECTION";

    if (!isDataCollectionRunning){
        cout << "[ERROR] Data Collection is not running" << endl;
        return false;
    }

    isDataCollectionRunning = false;
     
    stop_data_collection_flag = true;

    // send end data collection cmd
    if( !udp_transmit(sock_id, endDataCollectionCmd, sizeof(endDataCollectionCmd)) ){
        cout << "[ERROR]: UDP error. check connection with host!" << endl;
    }

    cout << "STOP DATA COLLECTION" << endl;

    pthread_join(collect_data_t, nullptr);
    return true;
}

static bool isInteger(const char* str) {
    // Check if the string is empty
    if (*str == '\0') {
        return false;
    }

    // Check if all characters in the string are digits
    return std::all_of(str, str + strlen(str), ::isdigit);
}

static bool isExitKeyPressed(){
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds); // Monitor stdin for input

    char buf[256];  
    // check if the file descriptor for stdin input has data
    if (FD_ISSET(STDIN_FILENO, &readfds)) {
        fgets(buf, sizeof(buf), stdin); // Read input from stdin
        if (buf[0] == 'e') {
            printf("Exit key pressed, stopping the program.\n");
            return true;
        }
    }
    return false;
}


using namespace std;

int main(int argc, char *argv[]) {

    int dataCollectionDuration = 0;
    bool startFlag = false;
    bool timedCaptureFlag = false;
    uint8_t boardID; 


    // start "time" "boardID"
    if (argc == 3){

        if (!isInteger(argv[1])){
            cout << "invalid dataCollectionDuration arg" << endl;
            return -1;
        }

        if (!isInteger(argv[2])){
            cout << "invalid boardID arg" << endl;
            return -1;
        }

        dataCollectionDuration = atoi(argv[1]);
        boardID = atoi(argv[2]);
        timedCaptureFlag = true;
    
    // start boardID
    } else if (argc == 2) {

        if (!isInteger(argv[1])){
            cout << "invalid boardID arg" << endl;
            return -1;
        }

        boardID = atoi(argv[1]);
    
    } else if (argc == 1) {

        cout << "Ethernet Client Program:" << endl;
        cout << "./ethernet_client <captureTime> <boardID>" << endl;
        cout << "where <captureTime> is an optional arg!" << endl;
        cout << "[NOTE] Make sure to start the server before you start the client" << endl;;
        return 0;

    // 
    } else {
        // invalid arg count
        return -1; 
    }

    int client_socket;

    // udp_init(&client_socket, boardID);

    DataCollection *DC = new DataCollection();

    DC->init(boardID);

    DC->start();

    sleep(3);

    DC->stop();

    // DataCollectionStateMachine(client_socket, readfds);

    return 0;
}


