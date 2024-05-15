#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstdlib>

using namespace std;

#define RX_BYTECOUNT    1024


int main(int argc, char *argv[]) {
    daemon(0, 0);

    // Create a UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "Failed to create socket" << endl;
        return 1;
    }

    // Bind the socket to a specific address and port
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345); // Replace with your desired port number
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Failed to bind socket" << endl;
        close(sockfd);
        return 1;
    }

    char buffer[RX_BYTECOUNT];
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    while (1) {
        // Receive data from the client
        
        ssize_t numBytes = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&clientAddr, &clientAddrLen);

        if (numBytes > 0) {
            
        }

        // Process the received data
        // ...

        // Print the received data
        cout << "Received data: " << buffer << endl;
    }

    close(sockfd);
    return 0;
}
