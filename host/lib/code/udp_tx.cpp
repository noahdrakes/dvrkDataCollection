/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-    */
/* ex: set filetype=cpp softtabstop=4 shiftwidth=4 tabstop=4 cindent expandtab: */

/*
  Author(s):  Noah Drakes

  (C) Copyright 2024 Johns Hopkins University (JHU), All Rights Reserved.

--- begin cisst license - do not edit ---

This software is provided "as is" under an open source license, with
no warranty.  The complete license can be found in license.txt and
http://www.cisst.org/cisst/license.txt.

--- end cisst license ---
*/

#include <stdint.h>
#include <stdio.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "udp_tx.h"
#include "data_collection_shared.h"

bool udp_init(int *client_socket, uint8_t boardId)
{
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
        std::cout << "Failed to connect to server [" << ipAddress << "]" << std::endl;
        close(*client_socket);
        return false;
    }

    return true;
}

bool udp_transmit(int client_socket, void *data, int size)
{
    if (size > UDP_REAL_MTU) {
        return false;
    }

    return (send(client_socket, data, size, 0) >= 0);
}

bool udp_receive(int client_socket, void *data, int len)
{
    uint16_t received_bytes = recv(client_socket, data, len, 0);

    return (received_bytes > 0);
}

int udp_nonblocking_receive(int client_socket, void *data, int len)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client_socket, &readfds);

    int ret_code;

    struct timeval timeout;

    // Timeout values
    timeout.tv_sec = 0;          // 0 seconds
    timeout.tv_usec = 0;      // 1000 microseconds = 1 millisecond

    int max_fd = client_socket + 1;
    int activity = select(max_fd, &readfds, NULL, NULL, &timeout);

    if (activity < 0) {
        return UDP_SELECT_ERROR;
    } else if (activity == 0) {
        return UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT;
    } else {
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
        else {
            return UDP_NON_UDP_DATA_IS_AVAILABLE;
        }
    }
}

bool udp_close(int *client_socket)
{
    close(*client_socket);
    return true;
}
