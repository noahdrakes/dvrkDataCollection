#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstdlib>

using namespace std;

#define RX_BYTECOUNT    1024

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

static bool recvfrom_nonblocking(int client_socket, void * buffer, size_t len, struct sockaddr_in *src_addr, socklen_t *addrlen){

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client_socket, &readfds);

    if(isDataAvailable(&readfds, client_socket) == DATA_IS_AVAILBLE){
        cout << "data is available" << endl;
        if (FD_ISSET(client_socket, &readfds)) {
            printf("Data available to read on socket1\n");
            if (recvfrom(client_socket, buffer, len, 0, (struct sockaddr *)src_addr, addrlen)){
                return true;
            }  
        }
    }
    return false;
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

static bool handshakeRoutineHostPS(int client_socket) {
    int handshake_state = handshakeReady;
    char recvBuffer[100];
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    char initiateDataCollection[] = "ZYNQ: READY FOR DATA COLLECTION";

    while(handshake_state != handShakeCplt){
        switch(handshake_state){
            case handshakeReady:{
                handshake_state = handShakeWaitforHost;
                cout << "handshake_state: " << handshake_state << endl;
                break;
            }
            case handShakeWaitforHost:{
                bool is_data_available = recvfrom_nonblocking(client_socket, recvBuffer, 100, &clientAddr, &clientAddrLen);
                cout << "data available: %d " << is_data_available << endl;
                if (is_data_available){
                    cout << "data that is available: " << recvBuffer << endl; 
                    if(strcmp(recvBuffer, "HOST: READY FOR DATA COLLECTION") == 0){
                        handshake_state = handshakeSendReadyStateToHost;
                        break;
                    } 
                }
                break;
            }
            case handshakeSendReadyStateToHost:{
                if(sendto(client_socket, initiateDataCollection, strlen(initiateDataCollection), 0, (struct sockaddr *)&clientAddr, clientAddrLen) < 0 ){
                    perror("sendto failed");
                }
                handshake_state = handShakeIsHostReady;
                cout << "handshakeSendReadyStateToHost" << endl;
                break;
            }
            case handShakeIsHostReady:{
                cout << "handShakeIsHostReady" << endl;
                bool is_data_available = recvfrom_nonblocking(client_socket, recvBuffer, 100, &clientAddr, &clientAddrLen);
                if (is_data_available){
                    cout << "BIG DATA AVAILABLE" << endl;
                    cout << "data that is available: " << recvBuffer << endl; 
                    if(strcmp(recvBuffer, "HOST: START DATA COLLECTION") == 0){
                        handshake_state = handShakeCplt;
                        break;
                    } 
                }
                handshake_state = handshakeSendReadyStateToHost;
                break;
            }
        } 
    }

    return true;
}





int main(int argc, char *argv[]) {

    int client_socket;

    int ret_code = initiate_socket_connection(&client_socket);

    if (ret_code!= 0){
        return -1;
    }

    cout << "before handshake routine" << endl;

    if(handshakeRoutineHostPS(client_socket)){
        cout << "ZYNQ: HANDSHAKE SUCCESS" << endl;
    }


    close(client_socket);
    return 0;
}


