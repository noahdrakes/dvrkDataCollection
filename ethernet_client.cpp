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
#include <Kernel/sys/_types/_fd_set.h>
#include <sys/select.h>

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


    // transmit until packet is received 
    while(1){
        if (udp_transmit(client_socket, initiate_connection_cmd, sizeof(initiate_connection_cmd))){
            break;
        }  
    }

    // read file descripter:
    // just a bit array that contains a bit for each fd that sets the bit if data is ready
    // and clears if data is not ready
    fd_set readfds;

    struct timeval timeout;

    // timeout valus
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    // Buffer to read input
    char buf[256];  


    while(1){

        while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds); // Monitor stdin for input
        FD_SET(client_socket, &readfds);// Monitor socket for data

        int max_fd = client_socket + 1;
        int activity = select(max_fd, &readfds, NULL, NULL, &timeout);

        if (activity < 0) {
            perror("select error");
        } else if (activity == 0) {
            printf("select timeout occurred, no data available.\n");
        } else {
            if (FD_ISSET(STDIN_FILENO, &readfds)) {
                fgets(buf, sizeof(buf), stdin); // Read input from stdin
                if (buf[0] == 'e') {
                    printf("Exit key pressed, stopping the program.\n");
                    break;
                }
            }
            if (FD_ISSET(client_socket, &readfds)) {
                printf("Data available to read on socket1\n");
                if (udp_receive(client_socket, initiate_connection_cmd, sizeof(initiate_connection_cmd))){
                    break;
                }  
            }
        }
    }


        
    }

    udp_close(&client_socket);


    return 0;
}

