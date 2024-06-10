// stdlib
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

#define RX_BYTECOUNT    1024
#define ZYNQ_CONSOLE    "[ZYNQ]"

// MAX_PACKET_SIZE (in bytes) is calculated from GetMaxWriteDataSize in EthUdpPort.cpp
#define MAX_PACKET_SIZE     1446

uint32_t buffer1[MAX_PACKET_SIZE];
uint32_t buffer2[MAX_PACKET_SIZE];

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

enum DataCollectionStateMachine {
    SM_READY = 0,
    SM_SEND_READY_STATE_TO_HOST,
    SM_WAIT_FOR_HOST_HANDSHAKE,
    SM_WAIT_FOR_HOST_START_CMD,
    SM_START_DATA_COLLECTION,
    SM_CLOSE_SOCKET,
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

typedef struct {
    uint32_t *currentBuffer;
    uint32_t *nextBuffer;
    uint16_t dataBufferSize;
    pthread_mutex_t mutex;
    pthread_cond_t dataReadyCond;
    pthread_cond_t bufferProcessedCond;
    int dataReady;
    client_udp *client;
    BasePort *port;
    AmpIO *board;
} DoubleBuffer;



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

static uint16_t calculate_samples_per_packet(uint8_t num_encoders, uint8_t num_motors){
    return ((MAX_PACKET_SIZE/4)/ calculate_sizeof_sample(num_encoders, num_motors) );
}

static uint16_t calculate_quadlets_per_packet(uint8_t num_encoders, uint8_t num_motors){
    return (calculate_samples_per_packet(num_encoders, num_motors) * calculate_sizeof_sample(num_encoders, num_motors));
}

static float calculate_duration_as_float(chrono::high_resolution_clock::time_point start, chrono::high_resolution_clock::time_point end ){
    std::chrono::duration<float> duration = end - start;
    return duration.count();
}

static bool load_data_buffer(BasePort *Port, AmpIO *Board, uint32_t *data_buffer){

    cout << "inside of data buffer" << endl;

    if(data_buffer == NULL){
        cout << "databuffer pointer is null" << endl;
        return false;
    }

    if (sizeof(data_buffer) == 0){
        cout << "len of databuffer == 0" << endl;
        return false;
    }

    uint8_t num_encoders = Board->GetNumEncoders();
    uint8_t num_motors = Board->GetNumMotors();
    
    uint16_t samples_per_packet = calculate_samples_per_packet(num_encoders, num_motors);
    cout << "samples per packet: " << samples_per_packet << endl;
    cout << "size of sample: " << calculate_sizeof_sample(num_encoders, num_motors) << endl;
    cout << "sizeof databuffer: " << sizeof(data_buffer) << endl;
    
    uint16_t count = 0;

    // CAPTURE DATA 

    for (int i = 0; i < samples_per_packet; i++){

        Port->ReadAllBoards();

        if (!Board->ValidRead()){
            cout << "invalid read" << endl;
            return false;
        }

        cout << "count: "<< count << endl;

        // DATA 1: timestamp
        end_time = std::chrono::high_resolution_clock::now();
        float time_elapsed = calculate_duration_as_float(start_time,end_time);
        data_buffer[count++] = *reinterpret_cast<uint32_t *> (&time_elapsed);

        cout << "after timestamp " << endl;

        // DATA 2: num of encoders and num of motors
        data_buffer[count++] = (uint32_t)(num_encoders << 16) | (num_motors);

        // DATA 3: encoder position (for num_encoders)
        for (int i = 0; i < num_encoders; i++){
            data_buffer[count++] = static_cast<uint32_t>(Board->GetEncoderPosition(i));

            float encoder_velocity_float= static_cast<float>(Board->GetEncoderVelocityPredicted(i));
            data_buffer[count++] = *reinterpret_cast<uint32_t *>(&encoder_velocity_float);
        }

        cout << "after get encoder data" << endl;

        // DATA 4: motor current and motor status (for num_motors)
        for (int i = 0; i < num_motors; i++){
            uint32_t motor_curr = Board->GetMotorCurrent(i); 
            uint32_t motor_status = (Board->GetMotorStatus(i));
            data_buffer[count++] = (uint32_t)(((motor_status & 0x0000FFFF) << 16) | (motor_curr & 0x0000FFFF));
        }

        cout << "get motor data " << endl;
    
    }
    return true;
}


void *producer(void *arg) {
    DoubleBuffer *double_buffer = (DoubleBuffer *)arg;
    uint32_t data_buffer[MAX_PACKET_SIZE/4] = {0};

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
                return NULL;
            }
        }

        if(!load_data_buffer(double_buffer->port, double_buffer->board, data_buffer)){
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
    char recv_buffer[100];

    while(!stop_data_collection_flag){
        pthread_mutex_lock(&double_buffer->mutex);

        while(!double_buffer->dataReady && !stop_data_collection_flag){
            pthread_cond_wait(&double_buffer->dataReadyCond, &double_buffer->mutex);
        }

        if (stop_data_collection_flag) {
            pthread_mutex_unlock(&double_buffer->mutex);
            return NULL;
        }

        uint32_t *temp = double_buffer->currentBuffer;
        double_buffer->currentBuffer = double_buffer->nextBuffer;
        double_buffer->nextBuffer = temp;

        double_buffer->dataReady = 0;
        pthread_cond_signal(&double_buffer->bufferProcessedCond);
        pthread_mutex_unlock(&double_buffer->mutex);

        for (int i = 0; i < MAX_PACKET_SIZE/4; i++){
            printf("current buffer[%d] =  0x%X\n", i, double_buffer->currentBuffer[i]);
        }

        sendto(double_buffer->client->socket, double_buffer->currentBuffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&double_buffer->client->Addr, double_buffer->client->AddrLen);
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

    // set the start time for the timestamp data
    

    pthread_create(producer_t,NULL,producer, double_buffer);
    pthread_create(consumer_t,NULL,consumer, double_buffer);

    pthread_join(*producer_t, NULL);
    pthread_join(*consumer_t, NULL);

    pthread_mutex_destroy(&double_buffer->mutex);
    pthread_cond_destroy(&double_buffer->dataReadyCond);
    pthread_cond_destroy(&double_buffer->bufferProcessedCond);
}



// TODO: Data collection should be in State Machine in here
// Beter Return Code Handling 
    // create seperate enum and boil all udp errors down to client not up
static int dataCollectionStateMachine(client_udp *client, BasePort *port, AmpIO *board, DoubleBuffer *double_buffer) {
    cout << "Start Handshake Routine..." << endl;
    cout << "are we updating" << endl;

    int state = SM_WAIT_FOR_HOST_HANDSHAKE;
    char recvBuffer[100] = {0};
    
    char initiateDataCollection[] = "ZYNQ: READY FOR DATA COLLECTION";

    while(state != SM_EXIT){
        int ret_code = 0;

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
                    cout << "i'm guessing we are here "<< endl;
                    state = SM_CLOSE_SOCKET;
                    break;
                }
            }

            case SM_SEND_READY_STATE_TO_HOST:{
                if(sendto(client->socket, initiateDataCollection, strlen(initiateDataCollection), 0, (struct sockaddr *)&client->Addr, client->AddrLen) < 0 ){
                    perror("sendto failed");
                    cout << "client addr is invalid." << endl;
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
                if (ret_code > 1){
                    cout << "[UDP_ERROR] - return code: " << ret_code << " | Make sure that server application is executing on the processor! The udp connection may closed." << endl;;
                }

                cout << "ret code: " << ret_code << endl;
            
                close(client->socket);
                state = SM_EXIT;
                break;
            }
        }
    }
    return true;
}





int main(int argc, char *argv[]) {

    // might want to pass in cout or default to cerr

    // Initializing BasePort stuff
    // stringstream debugStream;

    BasePort::ProtocolType protocol = BasePort::PROTOCOL_SEQ_RW;


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


    // uint32_t data_buffer[MAX_PACKET_SIZE] = {0};
    // load_data_buffer(Port, Board, data_buffer);

    

    // create client object and set the addLen to the sizeof of the sockaddr_in struct
    client_udp client;
    client.AddrLen = sizeof(client.Addr);

    // initiate socket connection to port 12345
    int ret_code = initiate_socket_connection(&client.socket);

    if (ret_code!= 0){
        cout << "we stopped right here ?" << endl;
        return -1;
    }

    // start handshake routine between host and client 
    DoubleBuffer double_buffer;

    dataCollectionStateMachine(&client, Port, Board, &double_buffer);

    uint32_t data_buffer[350];

    // load_data_buffer(Port, Board, data_buffer);

    // for (int i = 0; i < 350 ; i++){
    //     printf("databuffer[%d] =  0x%X\n", i, data_buffer[i]);
    // }

    // start_time = std::chrono::high_resolution_clock::now();

    // sleep(5);

    // end_time = std::chrono::high_resolution_clock::now();

    // printf("time elapsed: %f\n", calculate_duration_as_float(start_time, end_time));

    cout << "DATA COLLECTION FINISHED!" << endl;

    // pthread_t producer_t;
    // pthread_t consumer_t;
    // DoubleBuffer double_buffer;

    // init_double_buffer(producer_t, consumer_t, &double_buffer, Port, Board, &client);

    // close(client.socket);
    return 0;
}


