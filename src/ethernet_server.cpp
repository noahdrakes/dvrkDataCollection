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
#include <algorithm>

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


float prev_time = 0;
float curr_time = 0;
int producer_counter = 0;
int consumer_counter = 0;
int sample_count = 0;


// start time for data collection timestamps
std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
std::chrono::time_point<std::chrono::high_resolution_clock> overhead;

std::chrono::time_point<std::chrono::high_resolution_clock> start_overhead;
std::chrono::time_point<std::chrono::high_resolution_clock> end_overhead;
float overhead_time = 0;

float average_transmit_time = 0;
float average_consumer_wait_time = 0;
double average_produce_time = 0;
double average_producer_wait_time = 0;
int total_transmits = 0;
vector<float> udp_max_transmit_wait_times;


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
enum UDP_RETURN_CODES {
    UDP_DATA_IS_AVAILABLE = 0,
    UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT = -1,
    UDP_SELECT_ERROR = -2,
    UDP_CONNECTION_CLOSED_ERROR = -3,
    UDP_SOCKET_ERROR = -4,
    UDP_NON_UDP_DATA_IS_AVAILABLE = -5
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
    atomic_uint8_t cons_busy; // TODO: change name 
    uint16_t dataBufferSize; 

    // bad design since client doesnt belong in db struct
    // probably should make this object global
    udp_info *client;
};


// checks if data is available from udp buffer (for noblocking udp recv)
int udp_nonblocking_receive(udp_info *client, void *data, int len) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client->socket, &readfds);

    int ret_code;

    struct timeval timeout;

    // Timeout values
    timeout.tv_sec = 0;          // 0 seconds
    timeout.tv_usec = 0;      // 1000 microseconds = 1 millisecond

    int max_fd = client->socket + 1;
    int activity = select(max_fd, &readfds, NULL, NULL, &timeout);

    if (activity < 0) {
        return UDP_SELECT_ERROR;
    } else if (activity == 0) {
        return UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT;
    } else {
        if (FD_ISSET(client->socket, &readfds)) {
            ret_code = recvfrom(client->socket, data, len, 0, (struct sockaddr *)&client->Addr, &client->AddrLen);

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

// udp transmit function. wrapper for sendo that abstracts the udp_info_struct
static int udp_transmit(udp_info *client, void * data, int size){
    
    // change UDP_MAX_QUADLET to 
    if (size > UDP_MAX_PACKET_SIZE){
        return -1;
    }

    return sendto(client->socket, data, size, 0, (struct sockaddr *)&client->Addr, client->AddrLen);
}


static int initiate_socket_connection(int *client_socket){
    cout << endl << "Initiating Socket Connection with DVRK board..." << endl;

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

    cout << "Connection Success !" << endl << endl;
    return 0;
}

// calculate the size of a sample in quadlets
static uint16_t calculate_sizeof_sample(uint8_t num_encoders, uint8_t num_motors){
    // SAMPLE STRUCTURE

    // Timestamp (32 bit)                                                       [4 byte]
    // EncoderNum and MotorNum (32 bit -> each are 16 bit)                      [4 byte] - NOT USED ANYMORE
    // Encoder Position (32 * num of encoders)                                  [4 byte * num of encoders]
    // Encoder Velocity Predicted (64 * num of encoders -> truncated to 32bits) [4 bytes * num of encoders]
    // Motur Current and Motor Status (32 * num of Motors -> each are 16 bits)  [4 byte * num of motors]

    return (1 + (1 * num_encoders) + (1 * num_encoders) + (1 * num_motors));

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
        
        if (!Port->ReadAllBoards()){
            cout << "Read All Board Fail" << endl;
            return false;
        }

        if (!Board->ValidRead()){
            cout << "invalid read" << endl;
            return false;
        }

        // DATA 1: timestamp
        end_time = std::chrono::high_resolution_clock::now();

        float time_elapsed = calculate_duration_as_float(start_time, end_time);

        if ((time_elapsed - prev_time) > 0.000700){
            printf("time glitch. sample: %d\n", producer_counter);
        }

        prev_time = time_elapsed;

        
        
        data_buffer[count++] = *reinterpret_cast<uint32_t *> (&time_elapsed);

        // DATA 2: encoder position
        for (int i = 0; i < num_encoders; i++){
            // is ths right ????? adding the encoderMidiRange
            data_buffer[count++] = static_cast<uint32_t>(Board->GetEncoderPosition(i) + Board->GetEncoderMidRange());
        }

        // DATA 3: encoder velocity
        for (int i = 0; i < num_encoders; i++){
            float encoder_velocity_float= static_cast<float>(Board->GetEncoderVelocityPredicted(i));
            data_buffer[count++] = *reinterpret_cast<uint32_t *>(&encoder_velocity_float);
        }

        // DATA 4 & 5: motor current and motor status (for num_motors)
        for (int i = 0; i < num_motors; i++){
            uint32_t motor_curr = Board->GetMotorCurrent(i); 

            // if (motor_curr == (uint32_t) 16320){
            //     cout << "DATA CORRUPTED "<< endl;
            // }

            uint32_t motor_status = (Board->GetMotorStatus(i));
            data_buffer[count++] = (uint32_t)(((motor_status & 0x0000FFFF) << 16) | (motor_curr & 0x0000FFFF));
        }

        sample_count++;
    }

    // producer_counter++;
    
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
    db->cons_busy = 0;
    db->dataBufferSize = calculate_quadlets_per_packet(board->GetNumEncoders(), board->GetNumMotors()) * 4;
    db->client = client;

    memset(db->double_buffer, 0, sizeof(db->double_buffer));
}

void * consume_data(void *arg){
    DB* db = (DB*)arg;

    while (!stop_data_collection_flag) {

        if (db->prod_buf != db->cons_buf) {
            
            // if (stop_data_collection_flag){
            //     return nullptr;
            // }
            db->cons_busy = 1; 
            udp_transmit(db->client, db->double_buffer[db->cons_buf], db->dataBufferSize);
            consumer_counter++;
            db->cons_busy = 0; // Mark consumer as not busy
            
            
            db->cons_buf = (db->cons_buf + 1) % 2;
        }   
    }

    return nullptr;
}

static int dataCollectionStateMachine(udp_info *client, BasePort *port, AmpIO *board, DB *db) {
    cout << "Starting Handshake Routine..." << endl << endl;
    cout << "Start Data Collection Client on HOST to complete handshake..." << endl;

    int state = SM_WAIT_FOR_HOST_HANDSHAKE;
    char recvBuffer[100] = {0};

    uint32_t data_collection_meta[7];
   
    
    // condition variable 
    int ret_code = 0;

    pthread_t consumer_t;

    while(state != SM_EXIT){     

        switch(state){
            case SM_WAIT_FOR_HOST_HANDSHAKE:{
                ret_code = udp_nonblocking_receive(client, recvBuffer, 100);
                if (ret_code > 0){
                    if(strcmp(recvBuffer, "HOST: READY FOR DATA COLLECTION") == 0){
                        cout << "Received Message from Host: READY FOR DATA COLLECTION" << endl;
                        state = SM_SEND_DATA_COLLECTION_METADATA;
                        break;
                    } 
                    
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT || ret_code == UDP_NON_UDP_DATA_IS_AVAILABLE){
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
                // cout << "package meta" << endl;

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

                ret_code = udp_nonblocking_receive(client, recvBuffer, 100);
                // cout << "checking if meta is received " << endl;

                if (ret_code > 0){
                    if(strcmp(recvBuffer, "HOST: RECEIVED METADATA") == 0){
                        cout << "Received Message from Host: METADATA RECEIVED" << endl;
                        cout << "Handshake Complete!" << endl;

                        state = SM_SEND_READY_STATE_TO_HOST;
                        break; 
                    } else {
                        state = SM_WAIT_FOR_HOST_RECV_METADATA;
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT || ret_code == UDP_NON_UDP_DATA_IS_AVAILABLE){
                    // cout << "here i guess" << endl;
                    state = SM_WAIT_FOR_HOST_RECV_METADATA;
                    break;
                } else {
                    // cout << "maybe here" << endl;
                    ret_code = SM_UDP_ERROR;
                    state = SM_CLOSE_SOCKET;
                    // state = SM_WAIT_FOR_HOST_RECV_METADATA;
                    // break;
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

                cout << endl << "Waiting for Host to start data collection..." << endl << endl;
                state = SM_WAIT_FOR_HOST_START_CMD;
                break;
            }

            case SM_WAIT_FOR_HOST_START_CMD: {
                
                // cout << "WAIT FOR START CMDDDDD " << endl;
                memset(recvBuffer, 0, 100);
                // cout << "host start cmd" << endl;
                ret_code = udp_nonblocking_receive(client, recvBuffer, 100);
                // cout << "wait for start cmd" << endl;
                if (ret_code > 0){
                    // cout << "recvd cmd " << recvBuffer << endl; 
                    if(strcmp(recvBuffer, "HOST: START DATA COLLECTION") == 0){
                        cout << "Received Message from Host: START DATA COLLECTION" << endl;
                        state = SM_START_DATA_COLLECTION;
                        break;
                        break;
                    } else if (strcmp(recvBuffer, "CLIENT: Terminate Server") == 0){
                        
                        printf("-- TIMING ANALYSIS --\n");
                        printf("Average Consume-Data Time: %f\n", average_transmit_time);
                        printf("Average Consumer-Wait Time (waiting on producer to switch buffers: ): %f\n", average_consumer_wait_time);
                        printf("Average Produce-Data Time: %.9f\n", average_produce_time);
                        printf("Average Producer Wait Time (waiting on consumer to finish): %.9f\n", average_producer_wait_time);
                        printf("Last 20 Transmit Times: ");

                        for (float times : udp_max_transmit_wait_times){
                            printf("%fs\n", times);
                        }

                        printf("Total Transmits: %d\n", total_transmits);

                        printf("\n-------------------------------------------------------------------\n");

                        printf("Average Read Quadlet Time: %f\n", average_produce_time/ ((float) calculate_quadlets_per_packet(board->GetNumEncoders(), board->GetNumMotors())));
                        ret_code = SM_SUCCESS;
                        state = SM_CLOSE_SOCKET;
                        break;
                    } else {
                        // TODO: need to add this invalid conidtion thing
                        // state = SM_SEND_READY_STATE_TO_HOST;
                        // while (1){
                            cout << "recv Buffer: " << recvBuffer << endl;
                            cout << "INVALID ENTRY" << endl;
                            state = SM_CLOSE_SOCKET;
                        // }
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT || ret_code == UDP_NON_UDP_DATA_IS_AVAILABLE){

                    // if (capture_count == 1){
                    //     state = SM_SEND_READY_STATE_TO_HOST;
                    // } else if (capture_count > 1){
                    //     state = SM_WAIT_FOR_HOST_START_CMD;
                    // }

                    state = SM_WAIT_FOR_HOST_START_CMD;
                    
                    break;
                } else {
                    // UDP error
                    ret_code = SM_UDP_ERROR;
                    state = SM_CLOSE_SOCKET;
                    break;
                }
            } 

            case SM_START_DATA_COLLECTION:{

                init_db(db, board, client);

                stop_data_collection_flag = false;

                // set start time for data collection
                start_time = std::chrono::high_resolution_clock::now();

                state = SM_START_CONSUMER_THREAD;
                break;
            }

            case SM_START_CONSUMER_THREAD:{

                // pthread_t new_cons_t;

                
                

                if (pthread_create(&consumer_t, nullptr, consume_data, db) != 0) {
                    std::cerr << "Error creating consumer thread" << std::endl;
                    return 1;
                }

                pthread_detach(consumer_t);

                


                state = SM_PRODUCE_DATA;
                break;
            }

            case SM_PRODUCE_DATA:{
                
                if ( !load_data_buffer(port, board, db->double_buffer[db->prod_buf])){
                    cout << "load data buffer fail" << endl;
                    while(1){}
                }
               
                producer_counter++;

                while (db->cons_busy) {}

                // Switch to the next buffer
                db->prod_buf = (db->prod_buf + 1) % 2;

                state = SM_CHECK_FOR_STOP_DATA_COLLECTION_CMD;
                break;

            }

            case SM_CHECK_FOR_STOP_DATA_COLLECTION_CMD:{

                start_overhead = std::chrono::high_resolution_clock::now();
                char recv_buffer[29];
                int ret = udp_nonblocking_receive(client, recv_buffer, 29);
                
                if (ret > 0){
                    if(strcmp(recv_buffer, "CLIENT: STOP_DATA_COLLECTION") == 0){
                        cout << "Message from Host: STOP DATA COLLECTION" << endl;

                        stop_data_collection_flag = true;
                                                
                        // using these conditionals just to break out of the 
                        // while loops in the producer and consumer

                        // while (db->cons_busy) {}
                        // db->cons_busy = 0;

                        pthread_join(consumer_t, nullptr);
                        

                        printf("TOTAL SAMPLE COLLECTED: %d\n", producer_counter);
                        printf("PRODUCER COUNTER: %d\n", producer_counter);
                        printf("CONSUMER_COUNTER: %d\n", consumer_counter);
                        printf("SAMPLE_COUNT: %d\n", sample_count);

                        producer_counter = 0; 
                        consumer_counter = 0;
                        sample_count = 0;

                        // state = SM_SEND_READY_STATE_TO_HOST;
                        state = SM_WAIT_FOR_HOST_START_CMD;
                        break;
                    } else {
                        // something went terribly wrong
                        cout << "[error] unexpected UDP message. Host and Processor are out of sync" << endl;
                        ret_code = SM_UDP_ERROR;
                        state = SM_CLOSE_SOCKET;
                        break;
                    }
                } else if (ret == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT || ret_code == UDP_NON_UDP_DATA_IS_AVAILABLE){
                    end_overhead = std::chrono::high_resolution_clock::now();
                    
                    state = SM_PRODUCE_DATA;
                    break;
                } else {
                    cout << "else" << endl;
                }
                
                
            }

            case SM_CLOSE_SOCKET:{
                if (ret_code != SM_SUCCESS){
                    cout << "[UDP_ERROR] - return code: " << ret_code << " | Make sure that server application is executing on the processor! The udp connection may closed." << endl;;
                } 

                cout << endl << "Terminating Server.." << endl;
                char terminationSuccessfulCmd[] = "Server: Termination Successful";
                // for (int i = 0; i < 100; i++){
                    udp_transmit(client, terminationSuccessfulCmd, 31);
                // }
                
                close(client->socket);
                state = SM_EXIT;
                break;
            }
        }
    }
    return SM_SUCCESS;
}




int main() {

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

    // char data[100] = {0};

    // while(udp_nonblocking_receive(&client,data,sizeof(data)) != UDP_DATA_IS_AVAILBLE){
        
    // }

    // cout << "successful recvfrom" << endl;



    // char word[] = "packet";
    // udp_transmit(&client, word, sizeof(word));


    dataCollectionStateMachine(&client, Port, Board, &db);

    // Port->ReadAllBoards();

    // uint32_t data_buffer[361];
    // // load_data_buffer(Port, Board, data_buffer);
    // // load_data_buffer(Port, Board, data_buffer);
    // load_data_buffer(Port, Board, data_buffer);

    // for (int i = 0; i < 361; i++){
    //     printf("data_buffer[%d]  = %d\n", i, data_buffer[i]);
    // }

    return 0;
}


