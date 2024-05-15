#include <stdint.h>
#include <stdio.h>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <udp_tx.h>
#include <cstdio>
#include <cstring> 

// might need to pass in integer for boardID

bool udp_tx_init(int * client_socket, int boardId){

    int ret;
    char ipAddress[14] = "169.254.10.";

    if (boardId < 0 || boardId > 15){
        return false; 
    }
    
    // **change sprint statemnt to use %s , no need for conditionals

    sprintf(ipAddress + strlen(ipAddress) , "%d", boardId);

    *client_socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (*client_socket < 0) {
        return false;
    }

    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(12345);
    server_address.sin_addr.s_addr = inet_addr(ipAddress);

    ret = connect(*client_socket, (struct sockaddr*)&server_address, sizeof(server_address));

    if (ret != 0) {
        return false;
    }

    return true;
}

bool udp_transmit(int client_socket, void * data, size_t size){
    
    // change UDP_MAX_QUADLET to 
    if (size > UDP_MAX_QUADLET_PER_PACKET){
        return false;
    }

    if (send(client_socket, data, size, 0) > 0){
        return true;
    } else {
        return false;
    } 
}

// might want to do nonblocking to program a stop key 
bool udp_receive(int client_socket, void *data, int len)
{
    while(1){
        uint16_t recieved_bytes = recv(client_socket, data, len, 0);

        if (recieved_bytes > 0){
            break;
        } else if (recieved_bytes == 0){
            return false; // connection closed
        } else {
            // EAGAIN just means no data is available or some other trivial error 
            // EWOULDBLOCK just means 
            if(errno == EAGAIN){
                continue;
            } else {
                return false;
            }
        }
    }
    
}

bool udp_close(int * client_socket){
    close(*client_socket);
    return true;
}