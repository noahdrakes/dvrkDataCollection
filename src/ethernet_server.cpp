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
#include <atomic>

// dvrk libs
#include "BasePort.h"
#include "PortFactory.h"
#include "EthBasePort.h"


using namespace std;

// UDP_MAX_PACKET_SIZE (in bytes) is calculated from GetMaxWriteDataSize (based
// on the MTU) in EthUdpPort.cpp
#define UDP_MAX_PACKET_SIZE     1446

// bffers for previous double buffer setup
uint32_t buffer1[UDP_MAX_PACKET_SIZE];
uint32_t buffer2[UDP_MAX_PACKET_SIZE];

// buffer for new db setup 
    // 2d static array that we can switch between 
uint32_t buf[2][1500];

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
    SM_CHECK_FOR_STOP_DATA_COLLECTION_CMD,
    SM_START_CONSUMER_THREAD,
    SM_PACKAGE_DATA_COLLECTION_METADATA,
    SM_SEND_DATA_COLLECTION_METADATA,
    SM_WAIT_FOR_HOST_RECV_METADATA,
    SM_PRODUCE_DATA,
    SM_CONSUME_DATA,
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
struct udp_info{
    int socket;
    struct sockaddr_in Addr;
    socklen_t AddrLen;
} ; // this is global bc there will only be one


struct DB{
    uint32_t double_buffer[2][UDP_MAX_PACKET_SIZE/4]; //note: changed from 1500 which makes sense
    uint8_t prod_buf;
    uint8_t cons_buf;
    atomic_uint8_t busy; // TODO: change name 
    uint16_t dataBufferSize; 

    // bad design since client doesnt belong in db struct
    // probably should make this object global
    udp_info *client;
};


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
static int udp_recvfrom_nonblocking(udp_info *client, void *buffer, size_t len){

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

// udp transmit function. wrapper for sendo that abstracts the udp_info_struct
static int udp_transmit(udp_info *client, void * data, int size){
    
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

// might need some renaming 

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
            // is ths right ????? adding the encoderMidiRange
            data_buffer[count++] = static_cast<uint32_t>(Board->GetEncoderPosition(i) + Board->GetEncoderMidRange());

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

void package_meta_data(uint32_t *meta_data_buffer, DB *db, AmpIO *board){
    uint8_t num_encoders = (uint8_t) board->GetNumEncoders();
    uint8_t num_motors = (uint8_t) board->GetNumMotors();

    meta_data_buffer[0] = 0xABCDEF12; // metadata magic word
    meta_data_buffer[1] = board->GetHardwareVersion();
    meta_data_buffer[2] = (uint32_t) num_encoders;
    meta_data_buffer[3] = (uint32_t) num_motors;

    meta_data_buffer[4] = (uint32_t) db->dataBufferSize; 
    meta_data_buffer[5] = (uint32_t) calculate_sizeof_sample(num_encoders, num_motors);
    meta_data_buffer[6] = (uint32_t) calculate_samples_per_packet(num_encoders, num_motors);
}

void init_db(DB *db, AmpIO *board, udp_info * client){
    db->cons_buf = 0;
    db->prod_buf = 0;
    db->busy = 0;
    db->dataBufferSize = calculate_quadlets_per_packet(board->GetNumEncoders(), board->GetNumMotors()) * 4;
    db->client = client;
}

void * consume_data(void *arg){
    DB* db = (DB *) arg;
    
    while(!stop_data_collection_flag){
        
        if (!db->busy){
            continue;
        }

        // need to fix order 
       

        udp_transmit(db->client, db->double_buffer[db->cons_buf], db->dataBufferSize);

        db->cons_buf = (db->cons_buf + 1) % 2;

        db->busy = 0;
    }

    return NULL;

}

static int dataCollectionStateMachine(udp_info *client, BasePort *port, AmpIO *board, DB *db) {
    cout << "Start Handshake Routine..." << endl;

    int state = SM_WAIT_FOR_HOST_HANDSHAKE;
    char recvBuffer[100] = {0};

    uint32_t data_collection_meta[7];
    
    // condition variable 
    int ret_code = 0;

    while(state != SM_EXIT){     

        switch(state){
            case SM_WAIT_FOR_HOST_HANDSHAKE:{
                ret_code = udp_recvfrom_nonblocking(client, recvBuffer, 100);
                if (ret_code == UDP_DATA_IS_AVAILBLE){
                    if(strcmp(recvBuffer, "HOST: READY FOR DATA COLLECTION") == 0){
                        cout << "Received Message from Host: READY FOR DATA COLLECTION" << endl;
                        state = SM_SEND_DATA_COLLECTION_METADATA;
                        break;
                    } 
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    break;
                } else {
                    ret_code = SM_UDP_ERROR;
                    state = SM_CLOSE_SOCKET;
                    break;
                }
            }

            // new pair ////////////////

            case SM_SEND_DATA_COLLECTION_METADATA:{
                package_meta_data(data_collection_meta, db, board);

                if(udp_transmit(client, data_collection_meta, sizeof(data_collection_meta)) < 1 ){
                    perror("sendto failed");
                    cout << "client addr is invalid." << endl;
                    ret_code = SM_UDP_ERROR;
                    state = SM_CLOSE_SOCKET;
                    break;
                }
                state = SM_WAIT_FOR_HOST_RECV_METADATA;
                break;
            }

            case SM_WAIT_FOR_HOST_RECV_METADATA:{
                ret_code = udp_recvfrom_nonblocking(client, recvBuffer, 100);
                if (ret_code == UDP_DATA_IS_AVAILBLE){
                    if(strcmp(recvBuffer, "HOST: RECEIVED METADATA") == 0){
                        cout << "Received Message from Host: METADATA RECEIVED" << endl;
                        state = SM_SEND_READY_STATE_TO_HOST;
                        break;
                    } 
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    state = SM_SEND_DATA_COLLECTION_METADATA;
                    break;
                } else {
                    ret_code = SM_UDP_ERROR;
                    state = SM_CLOSE_SOCKET;
                    break;
                }
            }


            case SM_SEND_READY_STATE_TO_HOST:{
                char initiateDataCollection[] = "ZYNQ: READY FOR DATA COLLECTION";

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

                // set start time for data collection
                start_time = std::chrono::high_resolution_clock::now();
                state = SM_START_CONSUMER_THREAD;
                break;
            }

            case SM_START_CONSUMER_THREAD:{

                pthread_t consumer_t;

                if (pthread_create(&consumer_t, nullptr, consume_data, db) != 0) {
                    std::cerr << "Error creating consumer thread" << std::endl;
                    return 1;
                }

                pthread_detach(consumer_t);

                state = SM_PRODUCE_DATA;
                break;
            }

            case SM_PRODUCE_DATA:{
                
                // if busy then just repeat
                if(db->busy){
                    state = SM_PRODUCE_DATA;
                    break;
                }

                load_data_buffer(port, board, db->double_buffer[db->prod_buf]);

                // need check for if producer overruns consumer 
                // wait for consumer to finish

                db->busy = 1;

                db->prod_buf = (db->prod_buf + 1) % 2;

                state = SM_CHECK_FOR_STOP_DATA_COLLECTION_CMD;
                break;

            }

            case SM_CHECK_FOR_STOP_DATA_COLLECTION_CMD:{
                char recv_buffer[29];
                
                if (udp_recvfrom_nonblocking(client, recv_buffer, 29) == UDP_DATA_IS_AVAILBLE){

                    if(strcmp(recv_buffer, "CLIENT: STOP_DATA_COLLECTION") == 0){
                        stop_data_collection_flag = true;
                        db->cons_buf = 1;
                        state = SM_CLOSE_SOCKET;
                        break;
                    } else {
                        // something went terribly wrong
                        cout << "[error] unexpected UDP message. Host and Processor are out of sync" << endl;
                        ret_code = SM_UDP_ERROR;
                        state = SM_CLOSE_SOCKET;
                        break;
                    }
                }
                
                state = SM_PRODUCE_DATA;
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
    return SM_SUCCESS;
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
    // rename maybe udp_info
    udp_info client;
    client.AddrLen = sizeof(client.Addr);

    // initiate socket connection to port 12345
    int ret_code = initiate_socket_connection(&client.socket);

    if (ret_code!= 0){
        cout << "[error] failed to establish socket connection !!" << endl;
        return -1;
    }

    DB db;
    init_db(&db, Board, &client);


    dataCollectionStateMachine(&client, Port, Board, &db);

    // uint32_t data_buffer[350];
    // load_data_buffer(Port, Board, data_buffer);

    // for (int i = 0; i < 350; i++){
    //     printf("data_buffer[%d]  = 0x%X\n", i, data_buffer[i]);
    // }

    return 0;
}


