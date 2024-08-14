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

// buffer for new db setup 
    // 2d static array that we can switch between 
uint32_t buf[2][1500];

float prev_time = 0;
float curr_time = 0;

// DEBUGGING VARIABLES 
int producer_counter = 0;
int consumer_counter = 0;
int sample_count = 0;
vector<float> udp_max_load_databuffer_times;


// start time for data collection timestamps
std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
std::chrono::time_point<std::chrono::high_resolution_clock> overhead;

std::chrono::time_point<std::chrono::high_resolution_clock> start;
std::chrono::time_point<std::chrono::high_resolution_clock> end_t;

std::chrono::time_point<std::chrono::high_resolution_clock> start_overhead;
std::chrono::time_point<std::chrono::high_resolution_clock> end_overhead;
float overhead_time = 0;

float average_transmit_time = 0;
float average_consumer_wait_time = 0;
double average_produce_time = 0;
double average_producer_wait_time = 0;
int total_transmits = 0;
vector<float> udp_max_transmit_wait_times;
int packet[20];


bool stop_data_collection_flag = false;


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
struct UDP_Info{
    int socket;
    struct sockaddr_in Addr;
    socklen_t AddrLen;
} udp_host ; // this is global bc there will only be one


struct Double_Buffer_Info{
    uint32_t double_buffer[2][UDP_MAX_PACKET_SIZE/4]; //note: changed from 1500 which makes sense
    uint16_t buffer_size; 
    uint8_t prod_buf;
    uint8_t cons_buf;
    atomic_uint8_t cons_busy; // TODO: change name 
};


// checks if data is available from udp buffer (for noblocking udp recv)
int udp_nonblocking_receive(UDP_Info *udp_host, void *data, int size) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(udp_host->socket, &readfds);

    int ret_code;

    struct timeval timeout;

    // Timeout values
    timeout.tv_sec = 0;          // 0 seconds
    timeout.tv_usec = 0;      // 1000 microseconds = 1 millisecond

    int max_fd = udp_host->socket + 1;
    int activity = select(max_fd, &readfds, NULL, NULL, &timeout);

    if (activity < 0) {
        return UDP_SELECT_ERROR;
    } else if (activity == 0) {
        return UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT;
    } else {
        if (FD_ISSET(udp_host->socket, &readfds)) {
            ret_code = recvfrom(udp_host->socket, data, size, 0, (struct sockaddr *)&udp_host->Addr, &udp_host->AddrLen);

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

// udp transmit function. wrapper for sendo that abstracts the UDP_Info_struct
static int udp_transmit(UDP_Info *udp_host, void * data, int size){
    
    // change UDP_MAX_QUADLET to 
    if (size > UDP_MAX_PACKET_SIZE){
        return -1;
    }

    return sendto(udp_host->socket, data, size, 0, (struct sockaddr *)&udp_host->Addr, udp_host->AddrLen);
}


static bool initiate_socket_connection(int *host_socket){

    cout << endl << "Initiating Socket Connection with DVRK board..." << endl;

    udp_host.AddrLen = sizeof(udp_host.Addr);

    // Create a UDP socket
    *host_socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (*host_socket < 0) {
        cerr << "[UDP ERROR] Failed to create socket [" << *host_socket << "]" << endl;
        return false;
    }

    // Bind the socket to a specific address and port
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345); // Replace with your desired port number
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(*host_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "[UDP ERROR] Failed to bind socket" << endl;
        close(*host_socket);
        return false;
    }

    cout << "UDP Connection Success !" << endl << endl;
    return true;
}

// calculate the size of a sample in quadlets
static uint16_t calculate_quadlets_per_sample(uint8_t num_encoders, uint8_t num_motors){
    // SAMPLE STRUCTURE

    // 1 quadlet = 4 bytes

    // Timestamp (32 bit)                                                       [1 quadlet]
    // Encoder Position (32 * num of encoders)                                  [1 quadlet * num of encoders]
    // Encoder Velocity Predicted (64 * num of encoders -> truncated to 32bits) [1 quadlet * num of encoders]
    // Motur Current and Motor Status (32 * num of Motors -> each are 16 bits)  [1 quadlet * num of motors]

    return (1 + (2*(num_encoders)) + (num_motors));

}

// calculates the # of samples per packet in quadlets
static uint16_t calculate_samples_per_packet(uint8_t num_encoders, uint8_t num_motors){
    return ((UDP_MAX_PACKET_SIZE/4)/ calculate_quadlets_per_sample(num_encoders, num_motors) );
}

// calculate # of quadlets per packet
static uint16_t calculate_quadlets_per_packet(uint8_t num_encoders, uint8_t num_motors){
    return (calculate_samples_per_packet(num_encoders, num_motors) * calculate_quadlets_per_sample(num_encoders, num_motors));
}

// returns the duration between start and end using the chrono
static float convert_chrono_duration_to_float(chrono::high_resolution_clock::time_point start, chrono::high_resolution_clock::time_point end ){
    std::chrono::duration<float> duration = end - start;
    return duration.count();
}

// loads data buffer for data collection
    // size of the data buffer is dependent on encoder count and motor count
    // see calculate_quadlets_per_sample method for data formatting
static bool load_data_buffer(BasePort *Port, AmpIO *Board, uint32_t *data_buffer, uint8_t num_encoders, uint8_t num_motors){

    if(data_buffer == NULL){
        cout << "[ERROR - load_data_buffer] databuffer pointer is null" << endl;
        return false;
    }

    if (sizeof(data_buffer) == 0){
        cout << "[ERROR - load_data_buffer] len of databuffer == 0" << endl;
        return false;
    }

    uint16_t samples_per_packet = calculate_samples_per_packet(num_encoders, num_motors);
    uint16_t count = 0;

    // CAPTURE DATA 
    for (int i = 0; i < samples_per_packet; i++){

        if (!Port->ReadAllBoards()){
            cout << "[ERROR in load_data_buffer] Read All Board Fail" << endl;
            return false;
        }

        if (!Board->ValidRead()){
            cout << "[ERROR in load_data_buffer] invalid read for ReadAllBoards" << endl;
            return false;
        }

        // DATA 1: timestamp
        end_time = std::chrono::high_resolution_clock::now();

        float time_elapsed = convert_chrono_duration_to_float(start_time, end_time);

        if ((time_elapsed - prev_time) > 0.000700){
            printf("[ERROR in load_data_buffer] time glitch. sample: %d\n", sample_count);
        }

        prev_time = time_elapsed;

        data_buffer[count++] = *reinterpret_cast<uint32_t *> (&time_elapsed);

        // DATA 2: encoder position
        for (int i = 0; i < num_encoders; i++){
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
            uint32_t motor_status = (Board->GetMotorStatus(i));

            data_buffer[count++] = (uint32_t)(((motor_status & 0x0000FFFF) << 16) | (motor_curr & 0x0000FFFF));
        }

        sample_count++;
    }
    
    return true;

    
}

void package_meta_data(uint32_t *meta_data_buffer, Double_Buffer_Info *db, AmpIO *board){
    uint8_t num_encoders = (uint8_t) board->GetNumEncoders();
    uint8_t num_motors = (uint8_t) board->GetNumMotors();

    meta_data_buffer[0] = 0xABCDEF12; // metadata magic word
    meta_data_buffer[1] = board->GetHardwareVersion();
    meta_data_buffer[2] = (uint32_t) num_encoders;
    meta_data_buffer[3] = (uint32_t) num_motors;

    meta_data_buffer[4] = (uint32_t) db->buffer_size; 
    meta_data_buffer[5] = (uint32_t) calculate_quadlets_per_sample(num_encoders, num_motors);
    meta_data_buffer[6] = (uint32_t) calculate_samples_per_packet(num_encoders, num_motors);
}

void reset_double_buffer_info(Double_Buffer_Info *db, AmpIO *board){
    db->cons_buf = 0;
    db->prod_buf = 0;
    db->cons_busy = 0;
    db->buffer_size = calculate_quadlets_per_packet(board->GetNumEncoders(), board->GetNumMotors()) * 4;

    memset(db->double_buffer, 0, sizeof(db->double_buffer));
}

void *consume_data(void *arg){
    Double_Buffer_Info* db = (Double_Buffer_Info*)arg;

    while (!stop_data_collection_flag) {

        if (db->prod_buf != db->cons_buf) {
            
            db->cons_busy = 1; 
            udp_transmit(&udp_host, db->double_buffer[db->cons_buf], db->buffer_size);
            consumer_counter++;
            db->cons_busy = 0; 
            
            db->cons_buf = (db->cons_buf + 1) % 2;
        }   
    }

    return nullptr;
}

static int dataCollectionStateMachine(BasePort *port, AmpIO *board) {

    cout << "Starting Handshake Routine..." << endl << endl;
    cout << "Start Data Collection Client on HOST to complete handshake..." << endl;

    Double_Buffer_Info db;
    reset_double_buffer_info(&db, board);

    
    char recvBuffer[100] = {0};

    uint32_t data_collection_meta[7];

    uint8_t num_encoders = board->GetNumEncoders();
    uint8_t num_motors = board->GetNumMotors();
    
    int ret_code = 0;

    pthread_t consumer_t;

    int state = SM_WAIT_FOR_HOST_HANDSHAKE;

    while(state != SM_EXIT){     

        switch(state){
            case SM_WAIT_FOR_HOST_HANDSHAKE:{

                memset(recvBuffer, 0, 100);

                ret_code = udp_nonblocking_receive(&udp_host, recvBuffer, 100);

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

            case SM_SEND_DATA_COLLECTION_METADATA:{
                package_meta_data(data_collection_meta, &db, board);

                if(udp_transmit(&udp_host, data_collection_meta, sizeof(data_collection_meta)) < 1 ){
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

                memset(recvBuffer, 0, 100);
                ret_code = udp_nonblocking_receive(&udp_host, recvBuffer, 100);

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
                    state = SM_WAIT_FOR_HOST_RECV_METADATA;
                    break;
                } else {
                    ret_code = SM_UDP_ERROR;
                    state = SM_CLOSE_SOCKET;
                }
            }


            case SM_SEND_READY_STATE_TO_HOST:{
                
                char initiateDataCollection[] = "ZYNQ: READY FOR DATA COLLECTION";

                if(udp_transmit(&udp_host, initiateDataCollection, strlen(initiateDataCollection)) < 1 ){
                    perror("sendto failed");
                    cout << "[ERROR UDP] client addr is invalid." << endl;
                    ret_code = SM_UDP_ERROR;
                    state = SM_CLOSE_SOCKET;
                    break;
                }

                cout << endl << "Waiting for Host to start data collection..." << endl << endl;
                state = SM_WAIT_FOR_HOST_START_CMD;
                break;
            }

            case SM_WAIT_FOR_HOST_START_CMD: {
                
                memset(recvBuffer, 0, 100);
                ret_code = udp_nonblocking_receive(&udp_host, recvBuffer, 100);

                if (ret_code > 0){
                    if(strcmp(recvBuffer, "HOST: START DATA COLLECTION") == 0){
                        cout << "Received Message from Host: START DATA COLLECTION" << endl;
                        state = SM_START_DATA_COLLECTION;
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

                stop_data_collection_flag = false;

                // set start time for data collection
                start_time = std::chrono::high_resolution_clock::now();

                state = SM_START_CONSUMER_THREAD;
                break;
            }

            case SM_START_CONSUMER_THREAD:{

                if (pthread_create(&consumer_t, nullptr, consume_data, &db) != 0) {
                    std::cerr << "Error creating consumer thread" << std::endl;
                    return 1;
                }

                pthread_detach(consumer_t);

                state = SM_PRODUCE_DATA;
                break;
            }

            case SM_PRODUCE_DATA:{
                
                if ( !load_data_buffer(port, board, db.double_buffer[db.prod_buf], num_encoders, num_motors)){
                    cout << "[ERROR]load data buffer fail" << endl;
                    return false;
                }
                
                producer_counter++;

                while (db.cons_busy) {}

                // Switch to the next buffer
                db.prod_buf = (db.prod_buf + 1) % 2;

                state = SM_CHECK_FOR_STOP_DATA_COLLECTION_CMD;
                break;

            }

            case SM_CHECK_FOR_STOP_DATA_COLLECTION_CMD:{

                start_overhead = std::chrono::high_resolution_clock::now();
                char recv_buffer[29];
                int ret = udp_nonblocking_receive(&udp_host, recv_buffer, 29);
                
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
                    udp_transmit(&udp_host, terminationSuccessfulCmd, 31);
                // }
                
                close(udp_host.socket);
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
    vector<AmpIO *> allBoards;

    // for (unsigned int i = 0; i < Port->GetNumOfNodes(); i++){
    //     allBoards.push_back(new AmpIO(Port->GetBoardId(i)));
    // }

    // for (unsigned int i = 0; i < Port->GetNumOfNodes(); i++){
    //     Port->AddBoard(allBoards[i]);
    // }

    Port->AddBoard(Board);

    // create client object and set the addLen to the sizeof of the sockaddr_in struct
    // rename maybe UDP_Info

    

    // initiate socket connection to port 12345
    bool isOK = initiate_socket_connection(&udp_host.socket);

    cout << "here" << endl;
    if (!isOK){
        cout << "[error] failed to establish socket connection !!" << endl;
        return -1;
    }

    


    // cout << "successful recvfrom" << endl;



    // char word[] = "packet";
    // udp_transmit(&client, word, sizeof(word));


    dataCollectionStateMachine(Port, Board);
    cout << "exiting data collection " << endl;

    // Port->ReadAllBoards(); 

    // uint32_t data_buffer[361];
    // // load_data_buffer(Port, Board, data_buffer);
    // // load_data_buffer(Port, Board, data_buffer);
    // load_data_buffer(Port, Board, data_buffer);

    // for (int i = 0; i < 361; i++){
    //     printf("data_buffer[%d]  = %d\n", i, data_buffer[i]);
    // }

    // Port->ReadAllBoards(); 

    // for (int i = 0; i<10; i++){
    //     printf("motor_status[%d]: %d\n", i, Board->GetMotorStatus(i));
    // }

    // for (int i = 0; i<10; i++){
    //     printf("motor_curr[%d]: %d\n", i, Board->GetMotorStatus(i));
    // }
    

    return 0;
}


