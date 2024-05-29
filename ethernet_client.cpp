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



#define TX_BYTECOUNT                    1024
const char *dataCollectionCMD;



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
    SM_EXIT
};

using namespace std;




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

// TODO: figure out valid return code for state machine success and failure
static int DataCollectionStateMachine(int client_socket, fd_set readfds){
    int handshake_state = SM_SEND_READY_STATE_TO_PS;
    char sendReadyStateCMD[] = "HOST: READY FOR DATA COLLECTION";
    char startDataCollectionCMD[] = "HOST: START DATA COLLECTION";
    char recvBuffer[100] = {0}; 
    while(handshake_state != SM_EXIT){
        switch(handshake_state){
            case SM_SEND_READY_STATE_TO_PS:{
                udp_transmit(client_socket, sendReadyStateCMD , strlen(sendReadyStateCMD));
                handshake_state = SM_WAIT_FOR_PS_HANDSHAKE;
                break;
            }
            case SM_WAIT_FOR_PS_HANDSHAKE:{
                int ret_code = udp_nonblocking_receive(client_socket, recvBuffer, sizeof(recvBuffer));
                if (ret_code == UDP_DATA_IS_AVAILBLE){
                    if (strcmp(recvBuffer, "ZYNQ: READY FOR DATA COLLECTION") == 0){
                        cout << "Received Message from Zynq: READY FOR DATA COLLECTION" << endl;
                        handshake_state = SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS;
                        break;
                    } else {
                        handshake_state = SM_SEND_READY_STATE_TO_PS;
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    handshake_state = SM_SEND_READY_STATE_TO_PS;
                    break;
                } else {
                    cout << "[UDP_ERROR] - return code: " << ret_code << endl;;
                    close(client_socket);
                    return ret_code;
                }
            }
            case SM_SEND_START_DATA_COLLECTIION_CMD_TO_PS:{
                cout << "Press [ENTER] to start data collection ..." << endl;
                getchar();
                udp_transmit(client_socket, startDataCollectionCMD, 28);
                handshake_state = SM_EXIT;
                break;
            }
        }
    }

    return 1;
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

    DataCollectionStateMachine(client_socket, readfds);


    // getchar();


    // struct timeval timeout;

    // // using chrono library to get relative time 
    // std::chrono::time_point<std::chrono::system_clock> start, end;
    // start = std::chrono::system_clock::now();

    // std::chrono::duration<double> elapsed_seconds;
    // double elapsed_time = 0; 


    // while(1){

    //     // TIMED CAPTURE
    //     // checks if timed capture based on args passed to ethernet client program
    //     if (timedCaptureFlag){
    //         end = std::chrono::system_clock::now();
    //         elapsed_seconds = end - start;
    //         elapsed_time = std::chrono::duration<double>(elapsed_seconds).count();

    //         if (elapsed_time > dataCollectionDuration){
    //             break;
    //         }
        
    //     }
    // }

    udp_close(&client_socket);
    return 0;
}


