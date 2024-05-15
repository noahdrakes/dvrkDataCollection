#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <cstdlib>
#include <getopt.h>
#include <udp_tx.h>

using namespace std;

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

int main(int argc, char *argv[]) {

    int dataCollectionDuration = 0;
    bool startFlag = false;
    bool timedCaptureFlag = false;
    uint8_t encoderNumber; 


    // start "time" "boardID"
    if (argc == 4){

        if (argv[1] != "start"){
            return false;
        }

        dataCollectionDuration = atoi(argv[2]);
        encoderNumber = atoi(argv[3]);
        timedCaptureFlag = true;
    
    // start boardID
    } else if (argc == 3) {

        if (argv[1] != "start"){
            return false;
        }

        encoderNumber = atoi(argv[2]);

    // 
    } else {
        // invalid arg count
        return false; 

    }

    int  client_socket;

    udp_init(&client_socket, encoderNumber);
    uint8_t initiate_connection_cmd[] = "start udp connection";

    while(1){
        if (udp_transmit(client_socket, initiate_connection_cmd, sizeof(initiate_connection_cmd))){
            break;
        }  
    }

    while(1){
        if (udp_receive(client_socket, initiate_connection_cmd, sizeof(initiate_connection_cmd))){
            break;
        }  
    }

    ////// NOW WE CAN RECIEVE DATA //////


    return 0;
}

