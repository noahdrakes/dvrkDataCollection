#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstdlib>

using namespace std;

#define RX_BYTECOUNT    1024


static int initiate_socket_connection(int * sockfd){
    cout << "attempting to connect to port 12345" << endl;

    // Create a UDP socket
    *sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (*sockfd < 0) {
        cerr << "Failed to create socket [" << *sockfd << "]" << endl;
        return -1;
    }

    // Bind the socket to a specific address and port
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345); // Replace with your desired port number
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(*sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Failed to bind socket" << endl;
        close(*sockfd);
        return -1;
    }

    return 0;
}



int main(int argc, char *argv[]) {

    int sockfd;

    int ret_code = initiate_socket_connection(&sockfd);

    if (ret_code!= 0){
        return -1;
    }


    char buffer[RX_BYTECOUNT];
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    while (1) {
        ssize_t numBytes = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&clientAddr, &clientAddrLen);

        if (numBytes > 0) {
            cout << "Host Request Received" << endl;

            if (strcmp(buffer, "DVRK INITIATE UDP CONNECTION") == 0){
                cout << "ZYNQ - START HANDSHAKE" << endl;
                break;
            }
        }
    }

    char initiateDataCollection[] = "INITIATE DATA COLLECTION";
    char newBuffer[49];

    while (1) {
        ssize_t numBytes = sendto(sockfd, initiateDataCollection, sizeof(initiateDataCollection), 0, (struct sockaddr *)&clientAddr, clientAddrLen);

        if (numBytes <= 0) {
            cout << "socket error" << endl;
            return -1; 
        }

    
        numBytes = recvfrom(sockfd, newBuffer, sizeof(newBuffer), 0, (struct sockaddr *)&clientAddr, &clientAddrLen);

        cout << "after recv" << endl;


        if (numBytes > 0){

            cout << "receive" << endl;

            newBuffer[numBytes] = '\0';

            if (strcmp(newBuffer, "DVRK - HANDSHAKE COMPLETE, START DATA COLLECTION") == 0){
                cout << "ZYNQ - HANDSHAKE COMPLETE, STARTED DATA COLLECTION" << endl;
                break;
            }
        }
            
    }
    close(sockfd);
    return 0;
}


