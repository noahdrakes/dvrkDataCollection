#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <cstdlib>
#include <getopt.h>
#include "udp_tx.hpp"
#include <sys/select.h>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <string>
#include "data_collection.hpp"

using namespace std;

static bool isInteger(const char* str) {
    // Check if the string is empty
    if (*str == '\0') {
        return false;
    }

    // Check if all characters in the string are digits
    return std::all_of(str, str + strlen(str), ::isdigit);
}

static bool isExitKeyPressed(){
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds); // Monitor stdin for input

    struct timeval timeout = {0, 0}; // No wait time
    int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
    
    if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
        char buf[256];
        fgets(buf, sizeof(buf), stdin); // Consume input
        return true;
    }
    return false;

}




using namespace std;

int main(int argc, char *argv[]) {

    int dataCollectionDuration = 0;
    bool startFlag = false;
    bool timedCaptureFlag = false;
    uint8_t boardID; 


    // start "time" "boardID"
    if (argc == 3){

        if (!isInteger(argv[1])){
            cout << "invalid boardID arg" << endl;
            return -1;
        }


        if (!isInteger(argv[2])){
            cout << "invalid dataCollectionDuration arg" << endl;
            return -1;
        }

        dataCollectionDuration = atoi(argv[2]);
        boardID = atoi(argv[1]);
        timedCaptureFlag = true;
    
    // start boardID
    } else if (argc == 2) {

        if (!isInteger(argv[1])){
            cout << "invalid boardID arg" << endl;
            return -1;
        }

        boardID = atoi(argv[1]);
    
    } else if (argc == 1) {

        cout << "Ethernet Client Program!" << endl;
        cout << "------------------------------" << endl;
        cout << "Usage: ./ethernet_client <boardID> [captureTime] " << endl;
        cout << "where <captureTime> is an optional arg!" << endl;
        cout << "[NOTE] Make sure to start the server before you start the client" << endl;;
        return 0;

    // 
    } else {
        // invalid arg count
        return -1; 
    }

    int client_socket;
    bool ret;

    DataCollection *DC = new DataCollection();
    bool stop_data_collection = false;

    ret = DC->init(boardID);

    if (!ret){
        return -1;
    }

    int count = 1;

    while(!stop_data_collection){

        printf("Woud you like to start capture [%d]? (y/n) :", count);

        char yn;
        cin >> yn;


        if (yn == 'y'){
            stop_data_collection = false;
        } else if (yn == 'n'){
            stop_data_collection = true;
            continue;
        } else {
            cout << "[error] Invalid character. Type either 'y' or 'n' and press enter: " << endl;
            stop_data_collection = true;
            continue;         
        }

        cout << endl;
        
    
        ret = DC->start();
        printf("...Press [ENTER] to terminate capture\n");


        if (!ret){
            return -1;
        }

        if (timedCaptureFlag){
            sleep(dataCollectionDuration);
        } else {
            while(1){
                if (isExitKeyPressed()){
                    break;
                }
            }
        }
        
        ret = DC->stop();

        if (!ret){
            return -1;
        }

        count++;

        
    }
       
    
    ret = DC->terminate();

    return 0;
}

