#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstdlib>
#include "BasePort.h"
#include "PortFactory.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include "AmpIO.h"
#include <vector>
#include <string>


using namespace std;

#define RX_BYTECOUNT    1024
#define ZYNQ_CONSOLE    "[ZYNQ]"

enum DataCollectionStateMachine {
    SM_READY = 0,
    SM_SEND_READY_STATE_TO_HOST,
    SM_WAIT_FOR_HOST_HANDSHAKE,
    SM_WAIT_FOR_HOST_START_CMD,
    SM_EXIT
};

enum UDP_RETURN_CODES{
    UDP_DATA_IS_AVAILBLE = 0,
    UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT,
    UDP_SELECT_ERROR,
    UDP_CONNECTION_CLOSED_ERROR,
    UDP_SOCKET_ERROR
};



struct client_udp{
    int socket;
    struct sockaddr_in Addr;
    socklen_t AddrLen;
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
        return UDP_SELECT_ERROR;
    } else if (activity == 0){
        return UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT;
    } else {
        return UDP_DATA_IS_AVAILBLE;
    }
}


static int udp_recvfrom_nonblocking(client_udp *client, void *buffer, size_t len){

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client->socket, &readfds);

    int ret_code = isDataAvailable(&readfds, client->socket) ;

    if(ret_code == UDP_DATA_IS_AVAILBLE ){
        if (FD_ISSET(client->socket, &readfds)) {
            ret_code = recvfrom(client->socket, buffer, len, 0, (struct sockaddr *)&client->Addr, &client->AddrLen);
            if (ret_code == 0){
                return UDP_CONNECTION_CLOSED_ERROR;
            } else if (ret_code < 0){
                return UDP_SOCKET_ERROR;
            } else {
                return UDP_DATA_IS_AVAILBLE;
            }
        }
    }

    return ret_code;
}

// static bool wait_for_start(client_udp *client){
//     cout << "Wait for User to start data collection ... " << endl;

//     char recvBuffer[100] = {0};
//     while(1){
//         bool ret_code = udp_recvfrom_nonblocking(client, recvBuffer, 100);
//         if (ret_code){
//             if(strcmp(recvBuffer, "HOST: START DATA COLLECTION") == 0){
//                     cout << "Received Message from Host: START DATA COLLECTION" << endl;
//                 break;
//             } else {
//                 close(client->socket);
//                 return false;
//             }
//         }
//     }
//     return true;
// }

static int initiate_socket_connection(int *client_socket){
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

// TODO: Data collection should be in State Machine in here
static int dataCollectionStateMachine(client_udp *client) {
    cout << "Start Handshake Routine..." << endl;

    int handshake_state = SM_WAIT_FOR_HOST_HANDSHAKE;
    char recvBuffer[100] = {0};
    
    char initiateDataCollection[] = "ZYNQ: READY FOR DATA COLLECTION";

    while(handshake_state != SM_EXIT){

        switch(handshake_state){
            case SM_WAIT_FOR_HOST_HANDSHAKE:{
                int ret_code = udp_recvfrom_nonblocking(client, recvBuffer, 100);
                if (ret_code == UDP_DATA_IS_AVAILBLE){
                    if(strcmp(recvBuffer, "HOST: READY FOR DATA COLLECTION") == 0){
                        cout << "Received Message from Host: READY FOR DATA COLLECTION" << endl;
                        handshake_state = SM_SEND_READY_STATE_TO_HOST;
                        break;
                    } 
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    break;
                } else {
                    cout << "[UDP_ERROR] - return code: " << ret_code << endl;
                    close(client->socket);
                    return ret_code;
                }
            }
            case SM_SEND_READY_STATE_TO_HOST:{
                if(sendto(client->socket, initiateDataCollection, strlen(initiateDataCollection), 0, (struct sockaddr *)&client->Addr, client->AddrLen) < 0 ){
                    perror("sendto failed");
                }
                handshake_state = SM_WAIT_FOR_HOST_START_CMD;
                break;
            }
            case SM_WAIT_FOR_HOST_START_CMD: {
                memset(recvBuffer, 0, 100);
                int ret_code = udp_recvfrom_nonblocking(client, recvBuffer, 100);
                if (ret_code == UDP_DATA_IS_AVAILBLE){
                    if(strcmp(recvBuffer, "HOST: START DATA COLLECTION") == 0){
                        cout << "Received Message from Host: START DATA COLLECTION" << endl;
                        handshake_state = SM_EXIT;
                        break;
                    } else {
                        handshake_state = SM_SEND_READY_STATE_TO_HOST;
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    handshake_state = SM_SEND_READY_STATE_TO_HOST;
                    break;
                } else {
                    cout << "[UDP_ERROR] - return code: " << ret_code << endl;;
                    close(client->socket);
                    return ret_code;
                }
            } 
        }
    }
    return true;
}



int main(int argc, char *argv[]) {

    // might want to pass in cout or default to cerr

    // Initializing BasePort stuff
    // stringstream debugStream;

    std::stringstream debugStream(std::stringstream::out|std::stringstream::in);

    string portDescription = BasePort::DefaultPort();
    BasePort *Port = PortFactory(portDescription.c_str(), debugStream);

    cout << "Board ID " << Port->GetBoardId(0) << endl;

    if(!Port->IsOK()){
        std::cerr << "Failed to initialize " << Port->GetPortTypeString() << std::endl;
        return -1;
    }


    if (Port->GetNumOfNodes() == 0) {
        std::cerr << "Failed to find any boards" << std::endl;
        return -1;
    }

    std::vector<AmpIO*> BoardList;
    BoardList.push_back(new AmpIO(Port->GetBoardId(Port->GetBoardId(0))));

    cout << "Board List size: " << BoardList.size() ;

    // create client object and set the addLen to the sizeof of the sockaddr_in struct
    client_udp client;
    client.AddrLen = sizeof(client.Addr);

    // initiate socket connection to port 12345
    int ret_code = initiate_socket_connection(&client.socket);

    if (ret_code!= 0){
        return -1;
    }

    // start handshake routine between host and client 

    dataCollectionStateMachine(&client);

    cout << "STARTING DATA COLLECTION!" << endl;

    close(client.socket);
    return 0;
}


