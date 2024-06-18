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

struct DC_Time{
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
    std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
    float time_elapsed;
}dc_time;

// NOTE:
// HOST_IP_ADDRESS = "169.254.255.252"
// PS IP ADDRESS    (ETH1) = "169.254.10.0"  

// needs to be running continuously 
// could program to start on key press 
// could be in one big while loop -> NEED TO BE NON-BLOCKING 

// could use relative time 
    // call getitme function on start 
    // for each packet record relative to start

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



DataCollection :: DataCollection(){
    cout << "constructor" << endl;
}


static bool isInteger(const char* str) {
    // Check if the string is empty
    if (*str == '\0') {
        return false;
    }

    // Check if all characters in the string are digits
    return std::all_of(str, str + strlen(str), ::isdigit);
}

std::string uint32ToString(uint32_t value) {
    return std::to_string(value);
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

static float calculate_duration_as_float(chrono::high_resolution_clock::time_point start, chrono::high_resolution_clock::time_point end ){
    std::chrono::duration<float> duration = end - start;
    return duration.count();
}

static void load_meta_data(uint32_t *meta_data){
    // store meta data into meta data struct
    string hwVersString;

    switch(meta_data[1]){
        case 0x514C4131:{
            hwVersString = "QLA1";
        case 0x64524131:
            hwVersString = "dRA1";
        case 0x44514C41:
            hwVersString = "dQLA";
        }
    };

    dc_meta.HWVers = hwVersString;
    dc_meta.num_encoders = (uint8_t) meta_data[2];
    dc_meta.num_motors = (uint8_t) meta_data[3];
    dc_meta.data_buffer_size = meta_data[4];
    dc_meta.size_of_sample = meta_data[5];
    dc_meta.samples_per_packet = meta_data[6];
}

// TODO: figure out valid return code for state machine success and failure
// configure client as a library (start_colllect, stop_collect, init_collect) class
static int DataCollectionStateMachine(int client_socket, fd_set readfds){
    int state = SM_SEND_READY_STATE_TO_PS;
    int ret_code = 0;

    char sendReadyStateCMD[] = "HOST: READY FOR DATA COLLECTION";
    char startDataCollectionCMD[] = "HOST: START DATA COLLECTION";
    char sendMetaDataRecvd[] = "HOST: RECEIVED METADATA";
    char recvBuffer[100] = {0}; 
    uint32_t meta_data[7] = {0};

    while(state != SM_EXIT){
        switch(state){
            case SM_SEND_READY_STATE_TO_PS:{
                udp_transmit(client_socket, sendReadyStateCMD , strlen(sendReadyStateCMD));
                state = SM_RECV_DATA_COLLECTION_META_DATA;
                break;
            }

            // new pair 
            case SM_RECV_DATA_COLLECTION_META_DATA:{
                // recv meta data
                ret_code = udp_nonblocking_receive(client_socket, meta_data, sizeof(meta_data));
                if (ret_code == UDP_DATA_IS_AVAILBLE){
                    if (meta_data[0] == METADATA_MAGIC_NUMBER){
                        cout << "Received Message from Zynq: RECEIVED METADATA" << endl;

                        load_meta_data(meta_data);

                        cout << "-DATA COLLECTION METADATA -" << endl;
                        cout << "Hardware Version: " << dc_meta.HWVers << endl;
                        cout << "Num of Encoders:  " <<  +dc_meta.num_encoders << endl;
                        cout << "Num of Motors: " << +dc_meta.num_motors << endl;
                        cout << "DataBuffer Size: " << dc_meta.data_buffer_size << endl;
                        cout << "Samples per Packet: " << dc_meta.samples_per_packet << endl;
                        cout << "Sizoef Samples (in bytes): " << dc_meta.size_of_sample << endl;
                        
                        state = SM_SEND_METADATA_RECV;
                        break;
                    } else {
                        state = SM_SEND_READY_STATE_TO_PS;
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    state = SM_SEND_READY_STATE_TO_PS;
                    break;
                } else {
                    state = SM_CLOSE_SOCKET;
                    break;
                }
            }

            case SM_SEND_METADATA_RECV:{
                // transmit meta data recieved
                udp_transmit(client_socket, sendMetaDataRecvd, sizeof(sendMetaDataRecvd));
                state = SM_WAIT_FOR_PS_HANDSHAKE;
                break;
            }

            case SM_WAIT_FOR_PS_HANDSHAKE:{
                ret_code = udp_nonblocking_receive(client_socket, recvBuffer, sizeof(recvBuffer));
                if (ret_code == UDP_DATA_IS_AVAILBLE){
                    if (strcmp(recvBuffer, "ZYNQ: READY FOR DATA COLLECTION") == 0){
                        cout << "Received Message from Zynq: READY FOR DATA COLLECTION" << endl;
                        state = SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS;
                        break;
                    } else {
                        state = SM_SEND_METADATA_RECV;
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    state = SM_SEND_METADATA_RECV;
                    break;
                } else {
                    state = SM_CLOSE_SOCKET;
                    break;
                }
            }
            case SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS:{
                cout << "Press [ENTER] to start data collection ..." << endl;
                getchar();
                udp_transmit(client_socket, startDataCollectionCMD, 28);
                state = SM_START_DATA_COLLECTION;
                break;
            }

            case SM_START_DATA_COLLECTION:{
                start_time = std::chrono::high_resolution_clock::now();
                float time_elapsed = 0; 
                int count = 0;

                uint32_t data_buffer[350] = {0};
                char endDataCollectionCmd[] = "CLIENT: STOP_DATA_COLLECTION";
                uint32_t temp = 0;

                // ofstream myFile;
                // myFile.open("data.csv");


                while(time_elapsed < 3){

                    ret_code = udp_nonblocking_receive(client_socket, data_buffer, sizeof(data_buffer));

                    if (ret_code == UDP_DATA_IS_AVAILBLE){
                        
                        // might need to use timestamp to verify that this is truly new data
                        // index 0 is always the time stamp
                        // so im confirming that the timestamp from this packet and the previous 
                        // packet are not the same
                        if (temp != data_buffer[0]){
                            // cout << "count: " << count++ << endl;
                            // for (int i = 0; i < 1400; i++){
                            //     printf("databuffer[%d] =  0x%X\n", i, data_buffer[i]);
                            // }
                        }

                        // cout << "count: " << count++ << endl;

                        // for (int i = 0; i < 350; i+=15){
                        //     for (int j = i; j < i+15; j++){
                        //         myFile << data_buffer[j] << ",";
                        //     }

                        //     myFile << "\n";
                        // }

                        
                        
                    }
                    
                    temp = data_buffer[0];
                    end_time = std::chrono::high_resolution_clock::now();
                    time_elapsed = calculate_duration_as_float(start_time, end_time);
                }

                // myFile.close();


                if( !udp_transmit(client_socket, endDataCollectionCmd, sizeof(endDataCollectionCmd)) ){
                    cout << "broo its falso bro" << endl;
                }
            
                

                state = SM_CLOSE_SOCKET;
                break;
            }

            case SM_CLOSE_SOCKET:{

                if (ret_code > 1){
                    cout << "[UDP_ERROR] - return code: " << ret_code << " | Make sure that server application is executing on the processor! The udp connection may closed." << endl;;
                }
            
                close(client_socket);
                state = SM_EXIT;
                break;
            }
        }
    }

    return ret_code;
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

    int  client_socket;
    struct sockaddr_in server_address;

    fd_set readfds;

    udp_init(&client_socket, boardID);

    DataCollection *dc = new DataCollection();

    DataCollectionStateMachine(client_socket, readfds);

    return 0;
}


