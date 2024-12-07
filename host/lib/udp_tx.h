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

#ifndef UDP_TX_H
#define UDP_TX_H

#include <stdint.h>

#define MTU_DEFAULT 1500

// correct this to value in EthUdpPort.cpp in unsigned int EthUdpPort::GetMaxReadDataSize(void) method
#define UDP_REAL_MTU                1446
#define UDP_MAX_QUADLET_PER_PACKET  UDP_REAL_MTU/4

enum UDP_RETURN_CODES {
    UDP_DATA_IS_AVAILABLE = 0,
    UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT = -1,
    UDP_SELECT_ERROR = -2,
    UDP_CONNECTION_CLOSED_ERROR = -3,
    UDP_SOCKET_ERROR = -4,
    UDP_NON_UDP_DATA_IS_AVAILABLE = -5
};


// upd init function
bool udp_init(int * client_socket, uint8_t encoder_number);

// udp close function
bool udp_close(int * client_socket);

// udp transmit function
bool udp_transmit(int client_socket, void *data, int len);

bool udp_receive(int client_socket, void *data, int len);

// this is what ya gotta do fr make a nonblocking receive function
int udp_nonblocking_receive(int client_socket, void *data, int len);

// check fd to check if data is available for udp port (and also console input)
int isDataAvailable(fd_set *readfds, int client_socket);

#endif
