#include <stdint.h>
#include <stdio.h>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <sys/select.h>
#include <cstdlib>
#include <atomic>

using namespace std; 

#define UDP_REAL_MTU 1446 // Define the maximum UDP packet size

// Return codes for non-blocking UDP receive
enum UDP_RETURN_CODES {
    UDP_DATA_IS_AVAILABLE = 0,
    UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT,
    UDP_SELECT_ERROR,
    UDP_CONNECTION_CLOSED_ERROR,
    UDP_SOCKET_ERROR
};

bool udp_init(int *client_socket, uint8_t boardId) {
    int ret;
    char ipAddress[14] = "169.254.10.";

    if (boardId > 15) {
        return false; 
    }

    snprintf(ipAddress + strlen(ipAddress), sizeof(ipAddress) - strlen(ipAddress), "%d", boardId);

    *client_socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (*client_socket < 0) {
        return false;
    }

    sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(12345);
    inet_pton(AF_INET, ipAddress, &server_address.sin_addr);

    ret = connect(*client_socket, (struct sockaddr*)&server_address, sizeof(server_address));

    if (ret != 0) {
        cout << "Failed to connect to server [" << ipAddress << "]" << endl;
        close(*client_socket);
        return false;
    }

    return true;
}

bool udp_transmit(int client_socket, void *data, int size) {
    if (size > UDP_REAL_MTU) {
        return false;
    }

    if (send(client_socket, data, size, 0) >= 0) {
        return true;
    } else {
        return false;
    }
}

bool udp_receive(int client_socket, void *data, int len) {
    uint16_t received_bytes = recv(client_socket, data, len, 0);

    if (received_bytes > 0) {
        return true;
    } else {
        return false;
    }
}

int isDataAvailable(fd_set *readfds, int client_socket) {
    struct timeval timeout;

    // Timeout values
    timeout.tv_sec = 0;          // 0 seconds
    timeout.tv_usec = 1000;      // 1000 microseconds = 1 millisecond

    int max_fd = client_socket + 1;
    int activity = select(max_fd, readfds, NULL, NULL, &timeout);

    if (activity < 0) {
        return UDP_SELECT_ERROR;
    } else if (activity == 0) {
        return UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT;
    } else {
        return UDP_DATA_IS_AVAILABLE;
    }
}

int udp_nonblocking_receive(int client_socket, void *data, int len) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client_socket, &readfds);

    int ret_code = isDataAvailable(&readfds, client_socket);

    if (ret_code == UDP_DATA_IS_AVAILABLE) {
        if (FD_ISSET(client_socket, &readfds)) {
            ret_code = recv(client_socket, data, len, 0);
            if (ret_code == 0) {
                return UDP_CONNECTION_CLOSED_ERROR;
            } else if (ret_code < 0) {
                return UDP_SOCKET_ERROR;
            } else {
                return ret_code; // Return the number of bytes received
            }
        }
    }

    return ret_code;
}

bool udp_close(int *client_socket) {
    close(*client_socket);
    return true;
}
