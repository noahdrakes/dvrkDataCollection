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

    char buf[256];  
    // check if the file descriptor for stdin input has data
    if (FD_ISSET(STDIN_FILENO, &readfds)) {
        fgets(buf, sizeof(buf), stdin); // Read input from stdin
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

    ret = DC->init(boardID);

    if (!ret){
        return -1;
    }

    int count = 0;
    while(count != 3){
        ret = DC->start();

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
    
    ret = DC->stop();

    return 0;
}


