#ifndef UDP_TX_H
#define UDP_TX_H

#include <stdint.h>

#define MTU_DEFAULT 1500

// correct this to value in EthUdpPort.cpp in unsigned int EthUdpPort::GetMaxReadDataSize(void) method
#define UDP_MAX_QUADLET_PER_PACKET 1500/32 

// upd init function
bool udp_init(int * client_socket, uint8_t encoder_number);

// udp close function
bool udp_close(int * client_socket);

// udp transmit function
bool udp_transmit(int client_socket, void *data, int len);

bool udp_receive(int client_socket, void *data, int len);
#endif
