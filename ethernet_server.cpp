// stdlibs
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <ctime>
#include "AmpIO.h"
#include "pthread.h"

// dvrk libs
#include "BasePort.h"
#include "PortFactory.h"
#include "EthBasePort.h"

using namespace std;

// UDP_MAX_PACKET_SIZE (in bytes) is calculated from GetMaxWriteDataSize (based
// on the MTU) in EthUdpPort.cpp
#define UDP_MAX_PACKET_SIZE     1446

uint32_t buffer1[UDP_MAX_PACKET_SIZE];
uint32_t buffer2[UDP_MAX_PACKET_SIZE];

// start time for data collection timestamps
std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
std::chrono::time_point<std::chrono::high_resolution_clock> end_time;

bool stop_data_collection_flag = false;

// #define QLA_NUM_MOTORS      4
// #define QLA_NUM_ENCODERS    4
// #define DQLA_NUM_MOTORS     8
// #define QLA_NUM_ENCODERS    8
// #define DRAC_NUM_MOTORS     10
// #define DRAC_NUM_ENCODERS   7

// State Machine states
enum DataCollectionStateMachine {
    SM_READY = 0,
    SM_SEND_READY_STATE_TO_HOST,
    SM_WAIT_FOR_HOST_HANDSHAKE,
    SM_WAIT_FOR_HOST_START_CMD,
    SM_START_DATA_COLLECTION,
    SM_CLOSE_SOCKET,
    SM_EXIT
};

// State Machine Return Codes
enum StateMachineReturnCodes {
    SM_SUCCESS, 
    SM_UDP_ERROR, 
    SM_BOARD_ERROR,
    SM_FAIL
};

// UDP Return Codes
enum UDP_RETURN_CODES{
    UDP_DATA_IS_AVAILBLE = 0,
    UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT,
    UDP_SELECT_ERROR,
    UDP_CONNECTION_CLOSED_ERROR,
    UDP_SOCKET_ERROR
};

// Socket Data struct
struct client_udp{
    int socket;
    struct sockaddr_in Addr;
    socklen_t AddrLen;
};

// Double Buffer struct to hold all parameters
// relating to the double buffer 
typedef struct {
    uint32_t *currentBuffer;            // buffer1
    uint32_t *nextBuffer;               // buffer2
    uint16_t dataBufferSize;            // hold the size of the data buffers in bytes
    pthread_mutex_t mutex;              // mutex to sync the threads for double buffer
    pthread_cond_t dataReadyCond;       // cond var used to signal that the producer thread finished retrieving data
    pthread_cond_t bufferProcessedCond; // cond var used to signal that the consumer thread finished sending data to client
    int dataReady;                      // data ready conditional

    // relevent members for sending and processing data
    client_udp *client;
    BasePort *port;
    AmpIO *board;
} DoubleBuffer;



// checks if data is available from udp buffer (for noblocking udp recv)
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

// nonblocking udp recv function. 
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

// udp transmit function. wrapper for sendo that abstracts the client_udp_struct
static int udp_transmit(client_udp *client, void * data, int size){
    
    // change UDP_MAX_QUADLET to 
    if (size > UDP_MAX_PACKET_SIZE){
        return -1;
    }

    return sendto(client->socket, data, size, 0, (struct sockaddr *)&client->Addr, client->AddrLen);
}


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

// calculate the size of a sample in quadlets
static uint16_t calculate_sizeof_sample(uint8_t num_encoders, uint8_t num_motors){
    // SAMPLE STRUCTURE

    // Timestamp (32 bit)                                                       [4 byte]
    // EncoderNum and MotorNum (32 bit -> each are 16 bit)                      [4 byte]
    // Encoder Position (32 * num of encoders)                                  [4 byte * num of encoders]
    // Encoder Velocity Predicted (64 * num of encoders -> truncated to 32bits) [4 bytes * num of encoders]
    // Motur Current and Motor Status (32 * num of Motors -> each are 16 bits)  [4 byte * num of motors]

    return (2 + (1 * num_encoders) + (1 * num_encoders) + (1 * num_motors));

}

// calculates the # of samples per packet in quadlets
static uint16_t calculate_samples_per_packet(uint8_t num_encoders, uint8_t num_motors){
    return ((UDP_MAX_PACKET_SIZE/4)/ calculate_sizeof_sample(num_encoders, num_motors) );
}

// calculate # of quadlets per packet
static uint16_t calculate_quadlets_per_packet(uint8_t num_encoders, uint8_t num_motors){
    return (calculate_samples_per_packet(num_encoders, num_motors) * calculate_sizeof_sample(num_encoders, num_motors));
}

// returns the duration between start and end using the chrono
static float calculate_duration_as_float(chrono::high_resolution_clock::time_point start, chrono::high_resolution_clock::time_point end ){
    std::chrono::duration<float> duration = end - start;
    return duration.count();
}

// loads data buffer for data collection
    // size of the data buffer is dependent on encoder count and motor count
    // see calculate_sizeof_sample method for data formatting
static bool load_data_buffer(BasePort *Port, AmpIO *Board, uint32_t *data_buffer){

    if(data_buffer == NULL){
        cout << "[error] databuffer pointer is null" << endl;
        return false;
    }

    if (sizeof(data_buffer) == 0){
        cout << "[error] len of databuffer == 0" << endl;
        return false;
    }

    uint8_t num_encoders = Board->GetNumEncoders();
    uint8_t num_motors = Board->GetNumMotors();
    
    uint16_t samples_per_packet = calculate_samples_per_packet(num_encoders, num_motors);
    
    uint16_t count = 0;

    // CAPTURE DATA 
    for (int i = 0; i < samples_per_packet; i++){
        
        Port->ReadAllBoards();

        if (!Board->ValidRead()){
            cout << "invalid read" << endl;
            return false;
        }

        // DATA 1: timestamp
        end_time = std::chrono::high_resolution_clock::now();
        float time_elapsed = calculate_duration_as_float(start_time,end_time);
        data_buffer[count++] = *reinterpret_cast<uint32_t *> (&time_elapsed);

        // DATA 2: num of encoders and num of motors
        data_buffer[count++] = (uint32_t)(num_encoders << 16) | (num_motors);

        // DATA 3: encoder position (for num_encoders)
        for (int i = 0; i < num_encoders; i++){
            data_buffer[count++] = static_cast<uint32_t>(Board->GetEncoderPosition(i));

            float encoder_velocity_float= static_cast<float>(Board->GetEncoderVelocityPredicted(i));
            data_buffer[count++] = *reinterpret_cast<uint32_t *>(&encoder_velocity_float);
        }

        // DATA 4: motor current and motor status (for num_motors)
        for (int i = 0; i < num_motors; i++){
            uint32_t motor_curr = Board->GetMotorCurrent(i); 
            uint32_t motor_status = (Board->GetMotorStatus(i));
            data_buffer[count++] = (uint32_t)(((motor_status & 0x0000FFFF) << 16) | (motor_curr & 0x0000FFFF));
        }

    
    }
    return true;
}


void *producer(void *arg) {
    DoubleBuffer *double_buffer = (DoubleBuffer *)arg;
    uint32_t data_buffer[UDP_MAX_PACKET_SIZE/4] = {0};

    start_time = std::chrono::high_resolution_clock::now();

    while(!stop_data_collection_flag){
        char recv_buffer[100] = {0};
        if (udp_recvfrom_nonblocking(double_buffer->client, recv_buffer, sizeof(recv_buffer)) == UDP_DATA_IS_AVAILBLE){
            if(strcmp(recv_buffer, "CLIENT: STOP_DATA_COLLECTION") == 0){
                stop_data_collection_flag = true;
                pthread_cond_signal(&double_buffer->dataReadyCond); // Signal to ensure consumer can exit
                pthread_mutex_unlock(&double_buffer->mutex);
                continue;
            } else {
                // something went terribly wrong
                cout << "[error] unexpected UDP message. Host and Processor are out of sync" << endl;
                stop_data_collection_flag = true;
                continue;
            }
        }

        if(!load_data_buffer(double_buffer->port, double_buffer->board, data_buffer)){
            stop_data_collection_flag = true;
            break;
        }

        pthread_mutex_lock(&double_buffer->mutex);

        while(double_buffer->dataReady){
            pthread_cond_wait(&double_buffer->bufferProcessedCond, &double_buffer->mutex);
        }

        //TODO: MAKE SURE DOUBLE BUFFER DATASIZE IS 1400
        memcpy(double_buffer->nextBuffer, data_buffer, double_buffer->dataBufferSize);
        double_buffer->dataReady = 1;
        pthread_cond_signal(&double_buffer->dataReadyCond);
        pthread_mutex_unlock(&double_buffer->mutex);
    }    

    return NULL;
}


//TODO: NEED TO FIGURE OUT WHERE CONSUMER STOPS
void *consumer(void *arg) {
    DoubleBuffer *double_buffer = (DoubleBuffer *)arg;
    int count = 0;

    while(!stop_data_collection_flag){
        pthread_mutex_lock(&double_buffer->mutex);

        while(!double_buffer->dataReady && !stop_data_collection_flag){
            pthread_cond_wait(&double_buffer->dataReadyCond, &double_buffer->mutex);
        }

        if (stop_data_collection_flag) {
            pthread_mutex_unlock(&double_buffer->mutex);
            continue;
        }

        uint32_t *temp = double_buffer->currentBuffer;
        double_buffer->currentBuffer = double_buffer->nextBuffer;
        double_buffer->nextBuffer = temp;

        double_buffer->dataReady = 0;
        pthread_cond_signal(&double_buffer->bufferProcessedCond);
        pthread_mutex_unlock(&double_buffer->mutex);

        udp_transmit(double_buffer->client, double_buffer->currentBuffer, calculate_quadlets_per_packet(double_buffer->board->GetNumEncoders(), double_buffer->board->GetNumMotors()) * 4);
        cout << "count: " << count++ << endl;
    }

    return NULL;
}


static void init_double_buffer(pthread_t *producer_t, pthread_t *consumer_t, DoubleBuffer *double_buffer, BasePort *port, AmpIO *board, client_udp *client){

    double_buffer->currentBuffer = buffer1;
    double_buffer->nextBuffer = buffer2;
    double_buffer->dataReady = 0;
    double_buffer->client = client;
    double_buffer->port = port;
    double_buffer->board = board;
    double_buffer->dataBufferSize = calculate_quadlets_per_packet(board->GetNumEncoders(), board->GetNumMotors()) * 4;
    
    pthread_mutex_init(&double_buffer->mutex, NULL);
    pthread_cond_init(&double_buffer->dataReadyCond, NULL);
    pthread_cond_init(&double_buffer->bufferProcessedCond, NULL);

    pthread_create(producer_t,NULL,producer, double_buffer);
    pthread_create(consumer_t,NULL,consumer, double_buffer);

    pthread_join(*producer_t, NULL);
    pthread_join(*consumer_t, NULL);

    pthread_mutex_destroy(&double_buffer->mutex);
    pthread_cond_destroy(&double_buffer->dataReadyCond);
    pthread_cond_destroy(&double_buffer->bufferProcessedCond);
}


static int dataCollectionStateMachine(client_udp *client, BasePort *port, AmpIO *board, DoubleBuffer *double_buffer) {
    cout << "Start Handshake Routine..." << endl;

    int state = SM_WAIT_FOR_HOST_HANDSHAKE;
    char recvBuffer[100] = {0};
    
    char initiateDataCollection[] = "ZYNQ: READY FOR DATA COLLECTION";

    int ret_code = 0;
    while(state != SM_EXIT){     

        switch(state){
            case SM_WAIT_FOR_HOST_HANDSHAKE:{
                ret_code = udp_recvfrom_nonblocking(client, recvBuffer, 100);
                if (ret_code == UDP_DATA_IS_AVAILBLE){
                    if(strcmp(recvBuffer, "HOST: READY FOR DATA COLLECTION") == 0){
                        cout << "Received Message from Host: READY FOR DATA COLLECTION" << endl;
                        state = SM_SEND_READY_STATE_TO_HOST;
                        break;
                    } 
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    // cout << "what about here " << endl;
                    // state = SM_WAIT_FOR_HOST_HANDSHAKE;
                    break;
                } else {
                    ret_code = SM_UDP_ERROR;
                    state = SM_CLOSE_SOCKET;
                    break;
                }
            }

            case SM_SEND_READY_STATE_TO_HOST:{
                if(udp_transmit(client, initiateDataCollection, strlen(initiateDataCollection)) < 1 ){
                    perror("sendto failed");
                    cout << "client addr is invalid." << endl;
                    ret_code = SM_UDP_ERROR;
                    state = SM_CLOSE_SOCKET;
                    break;
                }
                state = SM_WAIT_FOR_HOST_START_CMD;
                break;
            }

            case SM_WAIT_FOR_HOST_START_CMD: {
                memset(recvBuffer, 0, 100);
                ret_code = udp_recvfrom_nonblocking(client, recvBuffer, 100);
                if (ret_code == UDP_DATA_IS_AVAILBLE){
                    if(strcmp(recvBuffer, "HOST: START DATA COLLECTION") == 0){
                        cout << "Received Message from Host: START DATA COLLECTION" << endl;
                        state = SM_START_DATA_COLLECTION;
                        break;
                    } else {
                        state = SM_SEND_READY_STATE_TO_HOST;
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    state = SM_SEND_READY_STATE_TO_HOST;
                    break;
                } else {
                    // UDP error
                    ret_code = SM_UDP_ERROR;
                    state = SM_CLOSE_SOCKET;
                    break;
                }
            } 

            case SM_START_DATA_COLLECTION:{

                pthread_t producer_t;
                pthread_t consumer_t;

                init_double_buffer(&producer_t, &consumer_t, double_buffer, port, board, client);

                state = SM_CLOSE_SOCKET;
                break;
            }

            case SM_CLOSE_SOCKET:{
                if (ret_code != SM_SUCCESS){
                    cout << "[UDP_ERROR] - return code: " << ret_code << " | Make sure that server application is executing on the processor! The udp connection may closed." << endl;;
                } else{
                    cout << "DATA COLLECTION FINISHED! Success !" << endl;
                }
            
                close(client->socket);
                state = SM_EXIT;
                break;
            }
        }
    }
    return true;
}




int main(int argc, char *argv[]) {

    string portDescription = BasePort::DefaultPort();
    BasePort *Port = PortFactory(portDescription.c_str());

    cout << "Board ID " << Port->GetBoardId(0) << endl;

    if(!Port->IsOK()){
        std::cerr << "Failed to initialize " << Port->GetPortTypeString() << std::endl;
        return -1;
    }

    if (Port->GetNumOfNodes() == 0) {
        std::cerr << "Failed to find any boards" << std::endl;
        return -1;
    }

    AmpIO *Board = new AmpIO(Port->GetBoardId(0));

    Port->AddBoard(Board);
    
    cout << "GET NUM OF ENCODERS: " << Board->GetNumEncoders() << endl;
    cout << "GET NUM OF Motors: " << Board->GetNumMotors() << endl;
    cout << "GET BOARD ID: " << (unsigned int) Board->GetBoardId() << endl;
    cout << "timestamp: " << Board->GetTimestamp() << endl;
    
    cout << "hardwareVer: " << Board->GetHardwareVersionString() << endl;
    cout << "EncoderPos: " << Board->GetEncoderPosition(0) << endl;
    cout << "MotorCurr: " << Board->GetMotorCurrent(0) << endl;

    // create client object and set the addLen to the sizeof of the sockaddr_in struct
    client_udp client;
    client.AddrLen = sizeof(client.Addr);

    // initiate socket connection to port 12345
    int ret_code = initiate_socket_connection(&client.socket);

    if (ret_code!= 0){
        cout << "[error] failed to establish socket connection !!" << endl;
        return -1;
    }

    DoubleBuffer double_buffer;

    dataCollectionStateMachine(&client, Port, Board, &double_buffer);

    return 0;
}


