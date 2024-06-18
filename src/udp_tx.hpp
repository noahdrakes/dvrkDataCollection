#ifndef UDP_TX_H
#define UDP_TX_H

#include <stdint.h>

#define MTU_DEFAULT 1500

// correct this to value in EthUdpPort.cpp in unsigned int EthUdpPort::GetMaxReadDataSize(void) method
#define UDP_REAL_MTU                1446
#define UDP_MAX_QUADLET_PER_PACKET  UDP_REAL_MTU/4

enum UDP_RETURN_CODES{
    UDP_DATA_IS_AVAILBLE = 0,
    UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT,
    UDP_SELECT_ERROR,
    UDP_CONNECTION_CLOSED_ERROR,
    UDP_SOCKET_ERROR
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
