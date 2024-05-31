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
#include "AmpIO.h"
#include <vector>
#include <string>
#include <chrono>
#include <ctime>

#include "BasePort.h"
#include "PortFactory.h"
#include "EthBasePort.h"


using namespace std;

#define RX_BYTECOUNT    1024
#define ZYNQ_CONSOLE    "[ZYNQ]"

// MAX_PACKET_SIZE is calculated from GetMaxWriteDataSize in EthUdpPort.cpp
#define MAX_PACKET_SIZE     1446

uint32_t buffer1[MAX_PACKET_SIZE];
uint32_t buffer2[MAX_PACKET_SIZE];

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

// calculate the size of a sample in bytes
static uint16_t calculate_sizeof_sample(uint8_t num_encoders, uint8_t num_motors){

    // Timestamp (32 bit)                                                       [4 byte]
    // EncoderNum and MotorNum (32 bit -> each are 16 bit)                      [4 byte]
    // Encoder Position (32 * num of encoders)                                  [4 byte * num of encoders]
    // Encoder Velocity Predicted (64 * num of encoders -> truncated to 32bits) [4 bytes * num of encoders]
    // Motur Current and Motor Status (32 * num of Motors -> each are 16 bits)  [4 byte * num of motors]

    return (2 + (1 * num_encoders) + (1 * num_encoders) + (1 * num_motors)) * 4;

}

static uint16_t calculate_samples_per_packet(uint8_t num_encoders, uint8_t num_motors){

    

    return (MAX_PACKET_SIZE/ calculate_sizeof_sample(num_encoders, num_motors) );
}

static float calculate_duration_as_float(chrono::high_resolution_clock::time_point start, chrono::high_resolution_clock::time_point end ){
    std::chrono::duration<float> duration = end - start;
    return duration.count();
}

static bool load_data_buffer(BasePort *Port, AmpIO *Board, uint32_t *data_buffer, int len){

    if(data_buffer == NULL){
        cout << "databuffer pointer is null" << endl;
        return false;
    }

    if (len == 0){
        cout << "len of databuffer == 0" << endl;
        return false;
    }

    Port->ReadAllBoards();

    if (!Board->ValidRead()){
        cout << "invalid read" << endl;
        return false;
    }

    // start time
    chrono::time_point<std::chrono::high_resolution_clock> start, end;
    start = std::chrono::high_resolution_clock::now();

    uint8_t num_encoders = Board->GetNumEncoders();
    uint8_t num_motors = Board->GetNumMotors();
    

    uint16_t samples_per_packet = calculate_samples_per_packet(num_encoders, num_motors);

    uint16_t count = 0;

    // CAPTURE DATA 

    for (int i = 0; i < samples_per_packet; i++){

        // DATA 1: timestamp
        end = std::chrono::high_resolution_clock::now();
        float time_elapsed = calculate_duration_as_float(start,end);
        data_buffer[count++] = static_cast<uint32_t> (time_elapsed);

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
            uint32_t motor_status = static_cast<uint32_t> (Board->GetMotorStatus(i));
            data_buffer[count++] = (uint32_t)(((motor_status & 0x0000FFFF) << 16) | (motor_curr & 0x0000FFFF));
        }
    }
    return true;
}




// TODO: Data collection should be in State Machine in here
static int dataCollectionStateMachine(client_udp *client) {
    cout << "Start Handshake Routine..." << endl;

    int handshake_state = SM_WAIT_FOR_HOST_HANDSHAKE;
    char recvBuffer[100] = {0};
    
    char initiateDataCollection[] = "ZYNQ: READY FOR DATA COLLECTION";

    chrono::time_point<std::chrono::system_clock> start, end;
    start = chrono::system_clock::now();

    std::chrono::duration<double> elapsed_seconds;
    double elapsed_time = 0; 

    while(handshake_state != SM_EXIT){

        switch(handshake_state){
            case SM_WAIT_FOR_HOST_HANDSHAKE:{
                int ret_code = udp_recvfrom_nonblocking(client, recvBuffer, 100);
                if (ret_code == UDP_DATA_IS_AVAILBLE){
                    if(strcmp(recvBuffer, "HOST: READY FOR DATA COLLECTION") == 0){
                        cout << "Received Message from Host: READY FOR DATA COLLECTION" << endl;
                        handshake_state = SM_SEND_READY_STATE_TO_HOST;
                        break;
                    } 
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    break;
                } else {
                    cout << "[UDP_ERROR] - return code: " << ret_code << endl;
                    close(client->socket);
                    return ret_code;
                }
            }
            case SM_SEND_READY_STATE_TO_HOST:{
                if(sendto(client->socket, initiateDataCollection, strlen(initiateDataCollection), 0, (struct sockaddr *)&client->Addr, client->AddrLen) < 0 ){
                    perror("sendto failed");
                }
                handshake_state = SM_WAIT_FOR_HOST_START_CMD;
                break;
            }
            case SM_WAIT_FOR_HOST_START_CMD: {
                memset(recvBuffer, 0, 100);
                int ret_code = udp_recvfrom_nonblocking(client, recvBuffer, 100);
                if (ret_code == UDP_DATA_IS_AVAILBLE){
                    if(strcmp(recvBuffer, "HOST: START DATA COLLECTION") == 0){
                        cout << "Received Message from Host: START DATA COLLECTION" << endl;
                        handshake_state = SM_EXIT;
                        break;
                    } else {
                        handshake_state = SM_SEND_READY_STATE_TO_HOST;
                        break;
                    }
                } else if (ret_code == UDP_DATA_IS_NOT_AVAILABLE_WITHIN_TIMEOUT){
                    handshake_state = SM_SEND_READY_STATE_TO_HOST;
                    break;
                } else {
                    cout << "[UDP_ERROR] - return code: " << ret_code << endl;;
                    close(client->socket);
                    return ret_code;
                }
            } 
        }
    }
    end = std::chrono::system_clock::now();
    elapsed_seconds = end - start;
    elapsed_time = std::chrono::duration<double>(elapsed_seconds).count();
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


    // uint32_t data_buffer[80] = {0};
    // uint32_t len = 19;
    // float timestamp = 123;
    // load_data_buffer(Port, Board, data_buffer, len);

    

    // // create client object and set the addLen to the sizeof of the sockaddr_in struct
    // client_udp client;
    // client.AddrLen = sizeof(client.Addr);

    // // initiate socket connection to port 12345
    // int ret_code = initiate_socket_connection(&client.socket);

    // if (ret_code!= 0){
    //     return -1;
    // }

    // // start handshake routine between host and client 

    // dataCollectionStateMachine(&client);

    // cout << "STARTING DATA COLLECTION!" << endl;

    // close(client.socket);
    return 0;
}


