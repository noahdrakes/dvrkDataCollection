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

// stdlibs
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <chrono>
#include <pthread.h>
#include <atomic>

// mmap mio pins
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>

// dvrk libs
#include "BasePort.h"
#include "PortFactory.h"
#include "ZynqEmioPort.h"
#include "AmpIO.h"

// shared header
#include "data_collection_shared.h"

using namespace std;

// UDP_MAX_PACKET_SIZE (in bytes) is calculated from GetMaxWriteDataSize (based
// on the MTU) in EthUdpPort.cpp
// PK: This is now defined in data_collection_shared.h
const int UDP_MAX_PACKET_SIZE = UDP_REAL_MTU;

// defines and variables for MIO Memory mapping for reading MIO pins
const uint32_t GPIO_BASE_ADDR = 0xE000A000;
const unsigned int GPIO_BANK1_OFFSET = 0x8;
const uint32_t SCLR_CLK_BASE_ADDR = 0xF8000000;
volatile uint32_t *GPIO_MEM_REGION;

// FLAG for including Processor IO in data packets
bool use_ps_io_flag = false;


// DEBUGGING VARIABLES 
int data_packet_count = 0;
int sample_count = 0;

// Motor Current/Status arrays to store data 
// for emio timeout error
int32_t emio_read_error_counter = 0; 

// start time for data collection timestamps
std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
std::chrono::time_point<std::chrono::high_resolution_clock> end_time;

// FLAG set when the host terminates data collection
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
    SM_TERMINATE,
    SM_EXIT
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
struct UDP_Info {
    int socket;
    struct sockaddr_in Addr;
    socklen_t AddrLen;
} udp_host; // this is global bc there will only be one


// Double Buffer Struct handling double buffer between collecting and transmitting data between 
// threads
struct Double_Buffer_Info {
    uint32_t double_buffer[2][UDP_MAX_PACKET_SIZE/4]; //note: changed from 1500 which makes sense
    uint16_t buffer_size; 
    uint8_t prod_buf;
    uint8_t cons_buf;
    atomic_uint8_t cons_busy; 
};


static int mio_mmap_init()
{
    int mem_fd;

    // Open /dev/mem for accessing physical memory
    if ((mem_fd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
        cout << "Failed to open /dev/mem" << endl;
        return -1;
    }

    void *gpio_mmap = mmap(
        NULL,
        0x1000,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        mem_fd,
        GPIO_BASE_ADDR
    );

    if (gpio_mmap == MAP_FAILED) {
        perror("Failed to mmap");
        close(mem_fd);  // Always close the file descriptor on failure
        return -1;
    }

    GPIO_MEM_REGION = (volatile uint32_t * ) gpio_mmap;

    // Following code ensures that APER_CLK is enabled; should not be necessary
    // since fpgav3_emio_mmap also does this.
    void *clk_map = mmap(
        NULL,
        0x00000130,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        mem_fd,
        SCLR_CLK_BASE_ADDR
    );

    volatile unsigned long *clock_map = (volatile unsigned long *)clk_map;

    uint32_t bitmsk = (1 << 22);
    uint32_t aper_clk_reg = clock_map[0x12C/4];

    if ((aper_clk_reg & bitmsk) == 0) {
        clock_map[0x12C/4] |= bitmsk;
    }

    munmap(clk_map, 0x00000130);
    // End of APER_CLK check

    close(mem_fd);

    return 0;
}

uint8_t returnMIOPins(){

    if (GPIO_MEM_REGION == NULL) {
        cout << "[ERROR] MIO mmap region initialized incorrectly!" << endl;
        return 0;
    }

    const uint16_t MIO_PINS_MSK = 0x3C;
    uint32_t gpio_bank1 = GPIO_MEM_REGION[GPIO_BANK1_OFFSET/4];

    return (gpio_bank1 & MIO_PINS_MSK) >> 2;
}

// checks if data is available from udp buffer (for noblocking udp recv)
int udp_nonblocking_receive(UDP_Info *udp_host, void *data, int size)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(udp_host->socket, &readfds);

    int ret_code;

    struct timeval timeout;

    // Timeout values
    timeout.tv_sec = 0;   
    timeout.tv_usec = 0;    

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
static int udp_transmit(UDP_Info *udp_host, void * data, int size)
{

    if (size > UDP_MAX_PACKET_SIZE) {
        return -1;
    }

    return sendto(udp_host->socket, data, size, 0, (struct sockaddr *)&udp_host->Addr, udp_host->AddrLen);
}


static bool initiate_socket_connection(int &host_socket)
{
    cout << endl << "Initiating Socket Connection with host..." << endl;

    udp_host.AddrLen = sizeof(udp_host.Addr);

    // Create a UDP socket
    host_socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (host_socket < 0) {
        cerr << "[UDP ERROR] Failed to create socket [" << host_socket << "]" << endl;
        return false;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345); 
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(host_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "[UDP ERROR] Failed to bind socket" << endl;
        close(host_socket);
        return false;
    }

    cout << "UDP Connection Success !" << endl << endl;
    return true;
}

// calculate the size of a sample in quadlets
static uint16_t calculate_quadlets_per_sample(uint8_t num_encoders, uint8_t num_motors)
{
    // SAMPLE STRUCTURE

    // 1 quadlet = 4 bytes

    // Timestamp (32 bit)                                                           [1 quadlet]
    // Encoder Position (32 * num of encoders)                                      [1 quadlet * num of encoders]
    // Encoder Velocity Predicted (64 * num of encoders -> truncated to 32bits)     [1 quadlet * num of encoders]
    // Motur Current and Motor Status (32 * num of Motors -> each are 16 bits)      [1 quadlet * num of motors]
    // Digtial IO Values  (optional, used if PS IO is enabled ) 32 bits             [1 quadlet * digital IO]
    // MIO Pins (optional, used if PS IO is enabled ) 4 bits -> pad 32 bits         [1 quadlet * MIO PINS]                  
    if (use_ps_io_flag){
        return (1 + 1 + 1 + (2*(num_encoders)) + (num_motors));
    } else {
        return (1 + (2*(num_encoders)) + (num_motors));
    }
    
}

// calculates the # of samples per packet in quadlets
static uint16_t calculate_samples_per_packet(uint8_t num_encoders, uint8_t num_motors)
{
    return ((UDP_MAX_PACKET_SIZE/4)/ calculate_quadlets_per_sample(num_encoders, num_motors) );
}

// calculate # of quadlets per packet
static uint16_t calculate_quadlets_per_packet(uint8_t num_encoders, uint8_t num_motors)
{
    return (calculate_samples_per_packet(num_encoders, num_motors) * calculate_quadlets_per_sample(num_encoders, num_motors));
}

// returns the duration between start and end using the chrono
static float convert_chrono_duration_to_float(chrono::high_resolution_clock::time_point start, chrono::high_resolution_clock::time_point end )
{
    std::chrono::duration<float> duration = end - start;
    return duration.count();
}

// loads data buffer for data collection
    // size of the data buffer is dependent on encoder count and motor count
    // see calculate_quadlets_per_sample method for data formatting
static bool load_data_packet(BasePort *Port, AmpIO *Board, uint32_t *data_packet, uint8_t num_encoders, uint8_t num_motors)
{
    if (data_packet == NULL) {
        cout << "[ERROR - load_data_packet] databuffer pointer is null" << endl;
        return false;
    }

    if (sizeof(data_packet) == 0) {
        cout << "[ERROR - load_data_packet] len of databuffer == 0" << endl;
        return false;
    }

    uint16_t samples_per_packet = calculate_samples_per_packet(num_encoders, num_motors);
    uint16_t count = 0;

    // CAPTURE DATA 
    for (int i = 0; i < samples_per_packet; i++) {

        while(!Port->ReadAllBoards()) {
            emio_read_error_counter++;
        }

        if (!Board->ValidRead()) {
            cout << "[ERROR in load_data_packet] invalid read for ReadAllBoards" << endl;
            return false;
        }

        // DATA 1: timestamp
        end_time = std::chrono::high_resolution_clock::now();
        float time_elapsed = convert_chrono_duration_to_float(start_time, end_time);

        data_packet[count++] = *reinterpret_cast<uint32_t *> (&time_elapsed);

        // DATA 2: encoder position
        for (int i = 0; i < num_encoders; i++) {
            int32_t encoder_pos = Board->GetEncoderPosition(i);
            data_packet[count++] = static_cast<uint32_t>(encoder_pos + Board->GetEncoderMidRange());
        }

        // DATA 3: encoder velocity
        for (int i = 0; i < num_encoders; i++) {
            float encoder_velocity_float= static_cast<float>(Board->GetEncoderVelocityPredicted(i));
            data_packet[count++] = *reinterpret_cast<uint32_t *>(&encoder_velocity_float);
        }

        // DATA 4 & 5: motor current and motor status (for num_motors)
        for (int i = 0; i < num_motors; i++) {
            uint32_t motor_curr = Board->GetMotorCurrent(i); 
            // uint32_t motor_status = Board->GetMotorStatus(i);
        
            uint32_t raw_cmd_current;
            Port->ReadQuadlet(Port->GetBoardId(0), ((i+1) << 4) | 1, raw_cmd_current);
            // int16_t raw_cmd_current_16_bit = static_cast<int16_t>(raw_cmd_current);
            // uint16_t cmd_current_casted = *reinterpret_cast<uint16_t *>(&raw_cmd_current_16_bit);

            data_packet[count++] = (uint32_t)(((raw_cmd_current & 0x0000FFFF) << 16) | (motor_curr & 0x0000FFFF));
        }

        if (use_ps_io_flag){
            data_packet[count++] = Board->ReadDigitalIO();
            data_packet[count++] = (uint32_t) returnMIOPins();
        }
        
        sample_count++;
    }

    return true;    
}

void package_meta_data(DataCollectionMeta *dc_meta, AmpIO *board)
{
    uint8_t num_encoders = (uint8_t) board->GetNumEncoders();
    uint8_t num_motors = (uint8_t) board->GetNumMotors();

    dc_meta->hwvers = board->GetHardwareVersion();
    dc_meta->num_encoders = (uint32_t) num_encoders;
    dc_meta->num_motors = (uint32_t) num_motors;

    dc_meta->data_packet_size = (uint32_t)calculate_quadlets_per_packet(num_encoders, num_motors) * 4;
    dc_meta->size_of_sample = (uint32_t) calculate_quadlets_per_sample(num_encoders, num_motors);
    dc_meta->samples_per_packet = (uint32_t) calculate_samples_per_packet(num_encoders, num_motors);
}

void reset_double_buffer_info(Double_Buffer_Info *db, AmpIO *board)
{
    db->cons_buf = 0;
    db->prod_buf = 0;
    db->cons_busy = 0;
    db->buffer_size = calculate_quadlets_per_packet(board->GetNumEncoders(), board->GetNumMotors()) * 4;

    memset(db->double_buffer, 0, sizeof(db->double_buffer));
}

void *consume_data(void *arg)
{
    Double_Buffer_Info* db = (Double_Buffer_Info*)arg;

    while (!stop_data_collection_flag) {

        if (db->prod_buf != db->cons_buf) {
            
            db->cons_busy = 1; 
            udp_transmit(&udp_host, db->double_buffer[db->cons_buf], db->buffer_size);
            data_packet_count++;
            db->cons_busy = 0; 

            db->cons_buf = (db->cons_buf + 1) % 2;
        }   
    }

    return nullptr;
}

static int dataCollectionStateMachine(BasePort *port, AmpIO *board)
{
    cout << "Starting Handshake Routine..." << endl << endl;
    cout << "Start Data Collection Client on HOST to complete handshake..." << endl;

    // RETURN CODES
    // state machine return code
    int sm_ret = 0;
    // udp return code
    int udp_ret;

    // state status 
    int state = 0;
    int last_state = 0;

    Double_Buffer_Info db;
    reset_double_buffer_info(&db, board);
    
    if (mio_mmap_init() != 0) {
        state = SM_TERMINATE;
        sm_ret = SM_PS_IO_FAIL;
    }

    char recvdCMD[CMD_MAX_STRING_SIZE] = {0};

    struct DataCollectionMeta data_collection_meta;

    uint8_t num_encoders = board->GetNumEncoders();
    uint8_t num_motors = board->GetNumMotors();

    pthread_t consumer_t;

    state = SM_WAIT_FOR_HOST_HANDSHAKE;

    while (state != SM_EXIT) {

        switch (state) {

            case SM_WAIT_FOR_HOST_HANDSHAKE:

                memset(recvdCMD, 0, CMD_MAX_STRING_SIZE);
                udp_ret = udp_nonblocking_receive(&udp_host, recvdCMD, CMD_MAX_STRING_SIZE);

                if (udp_ret > 0) {
                    if (strcmp(recvdCMD,  HOST_READY_CMD) == 0) {
                        cout << "Received Message - " <<  HOST_READY_CMD << endl;
                        state = SM_SEND_DATA_COLLECTION_METADATA;
                    }                    
                    
                    else if (strcmp(recvdCMD, HOST_READY_CMD_W_PS_IO) == 0){
                        cout << "Received Message - " <<  HOST_READY_CMD_W_PS_IO << endl;
                        use_ps_io_flag = true;

                        // special case: need to resize double_buffer size to account
                        // for extra ps io data
                        reset_double_buffer_info(&db, board); 
                        state = SM_SEND_DATA_COLLECTION_METADATA;
                    }

                    else {
                        sm_ret = SM_OUT_OF_SYNC;
                        last_state = state;
                        state = SM_TERMINATE;
                    }
                }
                else if (udp_ret == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT || udp_ret == UDP_NON_UDP_DATA_IS_AVAILABLE) {
                    state = SM_WAIT_FOR_HOST_HANDSHAKE;
                }
                else {
                    sm_ret = SM_UDP_ERROR;
                    last_state = state;
                    state = SM_TERMINATE;
                }
                break;

            case SM_SEND_DATA_COLLECTION_METADATA:

                package_meta_data(&data_collection_meta, board);

                if (udp_transmit(&udp_host,  &data_collection_meta, sizeof(struct DataCollectionMeta )) < 1 ) {
                    sm_ret = SM_UDP_INVALID_HOST_ADDR;
                    state = SM_TERMINATE;
                }
                else {
                    state = SM_WAIT_FOR_HOST_RECV_METADATA;
                }
                break;

            case SM_WAIT_FOR_HOST_RECV_METADATA:

                memset(recvdCMD, 0, CMD_MAX_STRING_SIZE);
                udp_ret = udp_nonblocking_receive(&udp_host, recvdCMD, CMD_MAX_STRING_SIZE);

                if (udp_ret > 0) {
                    if (strcmp(recvdCMD, HOST_RECVD_METADATA) == 0) {
                        cout << "Received Message: " << HOST_RECVD_METADATA << endl;
                        cout << "Handshake Complete!" << endl;

                        state = SM_SEND_READY_STATE_TO_HOST;
                    } else {
                        sm_ret = SM_OUT_OF_SYNC;
                        last_state = state;
                        state = SM_TERMINATE;
                    }
                }
                else if (udp_ret == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT || udp_ret == UDP_NON_UDP_DATA_IS_AVAILABLE) {
                    // Stay in same state
                    state = SM_WAIT_FOR_HOST_RECV_METADATA;
                } else {
                    sm_ret = SM_UDP_ERROR;
                    last_state = state;
                    state = SM_TERMINATE;
                }
                break;

            case SM_SEND_READY_STATE_TO_HOST:

                if (udp_transmit(&udp_host,  (char *) ZYNQ_READY_CMD, sizeof(ZYNQ_READY_CMD)) < 1 ) {
                    sm_ret = SM_UDP_INVALID_HOST_ADDR;
                    state = SM_TERMINATE;
                }
                else {
                    state = SM_WAIT_FOR_HOST_START_CMD;
                    cout << endl << "Waiting for Host to start data collection..." << endl << endl;
                }
                break;

            case SM_WAIT_FOR_HOST_START_CMD:

                memset(recvdCMD, 0, CMD_MAX_STRING_SIZE);
                udp_ret = udp_nonblocking_receive(&udp_host, recvdCMD, CMD_MAX_STRING_SIZE);

                if (udp_ret > 0) {
                    if (strcmp(recvdCMD, HOST_START_DATA_COLLECTION) == 0) {
                        cout << "Received Message: " <<  recvdCMD << endl;
                        state = SM_START_DATA_COLLECTION;
                    }
                    else if (strcmp(recvdCMD, HOST_TERMINATE_SERVER) == 0) {
                        cout << "Received Message: " <<  recvdCMD << endl;
                        sm_ret = SM_SUCCESS;
                        state = SM_TERMINATE;
                    }
                    else {
                        sm_ret = SM_OUT_OF_SYNC;
                        last_state = state;
                        state = SM_TERMINATE;
                    }
                }
                else if (udp_ret == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT || udp_ret == UDP_NON_UDP_DATA_IS_AVAILABLE) {
                    state = SM_WAIT_FOR_HOST_START_CMD;
                }
                else {
                    sm_ret = SM_UDP_ERROR;
                    last_state = state;
                    state = SM_TERMINATE;
                }
                break;

            case SM_START_DATA_COLLECTION:

                stop_data_collection_flag = false;
                start_time = std::chrono::high_resolution_clock::now();
                state = SM_START_CONSUMER_THREAD;
                break;

            case SM_START_CONSUMER_THREAD:

                // Starting Consumer Thread: sends packets to host
                if (pthread_create(&consumer_t, nullptr, consume_data, &db) != 0) {
                    std::cerr << "Error creating consumer thread" << std::endl;
                    return 1;
                }

                pthread_detach(consumer_t);

                state = SM_PRODUCE_DATA;
                break;

            case SM_PRODUCE_DATA:

                if ( !load_data_packet(port, board, db.double_buffer[db.prod_buf], num_encoders, num_motors)) {
                    cout << "[ERROR]load data buffer fail" << endl;
                    return false;
                }

                while (db.cons_busy) {}

                // Switch to the next buffer
                db.prod_buf = (db.prod_buf + 1) % 2;

                state = SM_CHECK_FOR_STOP_DATA_COLLECTION_CMD;
                break;

            case SM_CHECK_FOR_STOP_DATA_COLLECTION_CMD:

                udp_ret = udp_nonblocking_receive(&udp_host, recvdCMD, CMD_MAX_STRING_SIZE);

                if (udp_ret > 0) {
                    if (strcmp(recvdCMD, HOST_STOP_DATA_COLLECTION) == 0) {
                        cout << "Message from Host: STOP DATA COLLECTION" << endl;

                        stop_data_collection_flag = true;

                        pthread_join(consumer_t, nullptr);

                        cout << "------------------------------------------------" << endl;
                        cout << "UDP DATA PACKETS SENT TO HOST: " << data_packet_count << endl;
                        cout << "SAMPLES SENT TO HOST: " << sample_count << endl;
                        cout << "EMIO ERROR COUNT: " << emio_read_error_counter << endl;
                        cout << "------------------------------------------------" << endl << endl;

                        emio_read_error_counter = 0; 
                        data_packet_count = 0;
                        sample_count = 0;

                        state = SM_WAIT_FOR_HOST_START_CMD;
                        cout << "Waiting for command from host..." << endl;
                        
                    } else {
                        sm_ret = SM_OUT_OF_SYNC;
                        last_state = state;
                        state = SM_TERMINATE;
                    }
                } else if (udp_ret == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT || udp_ret == UDP_NON_UDP_DATA_IS_AVAILABLE){
                    state = SM_PRODUCE_DATA;
                } else {
                    sm_ret = SM_UDP_ERROR;
                    last_state = state;
                    state = SM_TERMINATE;
                }
                break;

            case SM_TERMINATE:
                if (sm_ret != SM_SUCCESS) {
                    // Print out error messages
                    cout << "[ERROR] STATEMACHINE TERMINATING" << endl;

                    cout << "At STATE " << last_state << " ";

                    switch (sm_ret){
                        
                        case SM_OUT_OF_SYNC:
                            cout << "Zynq of sync with Host. Received unexpected command: " << recvdCMD << endl;
                            break;
                        case SM_UDP_ERROR:
                            cout << "Udp ERROR. Make sure host program is running." << endl;
                            break;
                        case SM_UDP_INVALID_HOST_ADDR:
                            cout << "Udp ERROR. Invalid Host Address format" << endl;
                            break;
                    }

                } else {
                    cout << "STATE MACHINE SUCCESS !" << endl;
                }

                cout << endl << ZYNQ_TERMINATATION_SUCCESSFUL << endl;

                udp_transmit(&udp_host,  (void*) ZYNQ_TERMINATATION_SUCCESSFUL, sizeof(ZYNQ_TERMINATATION_SUCCESSFUL));

                close(udp_host.socket);
                state = SM_EXIT;
                break;
        }
    }
    return sm_ret;
}



int main()
{
    string portDescription = BasePort::DefaultPort();
    cout << "PORT DESCRIPTION: " << portDescription << endl;

    // std::ostringstream nullStream;
    // std::ostream debugStream;
    // std::ostream & debugStream = std::cerr;
    // Custom stream buffer that discards all output
    class NullBuffer : public std::streambuf {
    protected:
        int overflow(int c) override {
            return c; // Discard all characters
        }
    };

    // Null output stream
    // NullBuffer nullBuffer;
    // std::ostream nullStream(&nullBuffer);
    // BasePort *Port = PortFactory(portDescription.c_str(), nullStream);

    BasePort *Port = PortFactory(portDescription.c_str());

    if (!Port->IsOK()) {
        std::cerr << "Failed to initialize " << Port->GetPortTypeString() << std::endl;
        return -1;
    }

    if (Port->GetNumOfNodes() == 0) {
        std::cerr << "Failed to find any boards" << std::endl;
        return -1;
    }

    ZynqEmioPort *EmioPort = dynamic_cast<ZynqEmioPort *>(Port);
    if (EmioPort) {
        cout << "Verbose: " << EmioPort->GetVerbose() << std::endl;
        // EmioPort->SetVerbose(true);
        EmioPort->SetTimeout_us(80);
    }
    else {
      cout << "[warning] failed to dynamic cast to ZynqEmioPort" << endl;
    }

    AmpIO *Board = new AmpIO(Port->GetBoardId(0));

    Port->AddBoard(Board);

    bool isOK = initiate_socket_connection(udp_host.socket);

    if (!isOK) {
        cout << "[error] failed to establish socket connection !!" << endl;
        return -1;
    }

    dataCollectionStateMachine(Port, Board);

    return 0;
}
