#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstdlib>

using namespace std;

#define RX_BYTECOUNT    1024
#define ZYNQ_CONSOLE    "[ZYNQ]"

enum handshakeSTATE {
    handshakeReady = 0,
    handshakeSendReadyStateToHost,
    handShakeWaitforHost,
    handShakeIsHostReady,
    handShakePsIsReady,
    handShakeCplt
};

enum dataBufferState{
    DATA_IS_AVAILBLE = 1,
    DATA_IS_NOT_AVAILABLE,
    SELECT_ERROR
};


struct client_udp{
    int socket;
    struct sockaddr_in Addr;
    socklen_t AddrLen;
};

//


// checks if data is available from console in or udp buffer
static int isDataAvailable(fd_set *readfds, int client_socket){
    struct timeval timeout;

    // timeout valus
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000;

    int max_fd = client_socket + 1;
    int activity = select(max_fd, readfds, NULL, NULL, &timeout);

    if (activity < 0){
        return SELECT_ERROR;
    } else if (activity == 0){
        return DATA_IS_NOT_AVAILABLE;
    } else {
        return DATA_IS_AVAILBLE;
    }
}

static bool recvfrom_nonblocking(client_udp *client, void * buffer, size_t len){

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client->socket, &readfds);

    if(isDataAvailable(&readfds, client->socket) == DATA_IS_AVAILBLE){
        // cout << "data is available" << endl;
        if (FD_ISSET(client->socket, &readfds)) {
            // printf("Data available to read on socket1\n");
            if (recvfrom(client->socket, buffer, len, 0, (struct sockaddr *)&client->Addr, &client->AddrLen)){
                return true;
            }  
        }
    }
    return false;
}

static bool wait_for_start(client_udp *client){
    char recvBuffer[100] = {0};
    while(1){
        bool is_data_available = recvfrom_nonblocking(client, recvBuffer, 100);
        if (is_data_available){
            if(strcmp(recvBuffer, "HOST: START DATA COLLECTION") == 0){
                    cout << "Received Message from Host: START DATA COLLECTION" << endl;
                break;
            } else {
                return false;
            }
        }
    }
    return true;
}

static int initiate_socket_connection(int * client_socket){
    cout << "attempting to connect to port 12345" << endl;

    // Create a UDP socket
    *client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (*client_socket < 0) {
        cerr << "Failed to create socket [" << *client_socket << "]" << endl;
        return -1;
    }

    // Bind the socket to a specific address and port
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345); // Replace with your desired port number
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(*client_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Failed to bind socket" << endl;
        close(*client_socket);
        return -1;
    }

    return 0;
}

static bool handshakeRoutineHostPS(client_udp *client) {
    int handshake_state = handshakeReady;
    char recvBuffer[100] = {0};
    
    char initiateDataCollection[] = "ZYNQ: READY FOR DATA COLLECTION";

    while(handshake_state != handShakeCplt){
        switch(handshake_state){
            case handshakeReady:{
                handshake_state = handShakeWaitforHost;
                break;
            }
            case handShakeWaitforHost:{
                bool is_data_available = recvfrom_nonblocking(client, recvBuffer, 100);
                if (is_data_available){
                    if(strcmp(recvBuffer, "HOST: READY FOR DATA COLLECTION") == 0){
                        cout << "Received Message from Host: READY FOR DATA COLLECTION" << endl;
                        handshake_state = handshakeSendReadyStateToHost;
                        break;
                    } 
                }
                break;
            }
            case handshakeSendReadyStateToHost:{
                if(sendto(client->socket, initiateDataCollection, strlen(initiateDataCollection), 0, (struct sockaddr *)&client->Addr, client->AddrLen) < 0 ){
                    perror("sendto failed");
                }
                handshake_state = handShakeCplt;
                break;
            }
        } 
    }
    return true;
}



int main(int argc, char *argv[]) {

    client_udp *client;
    client->AddrLen = sizeof(client->Addr);

    int ret_code = initiate_socket_connection(&client->socket);

    if (ret_code!= 0){
        return -1;
    }

    cout << "Start Handshake Routine..." << endl;

    if(handshakeRoutineHostPS(client)){
        cout << "ZYNQ: HANDSHAKE SUCCESS" << endl;
    }

    cout << "Wait for User to start data collection ... " << endl;

    bool retValue = wait_for_start(client);

    if (!retValue){
        cout << "ZYNQ recieved invalid cmd from host" << endl;
        close(client->socket);
        return -1;
    }

    cout << "STARTING DATA COLLECTION!" << endl;

    close(client->socket);
    return 0;
}


