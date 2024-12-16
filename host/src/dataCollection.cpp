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

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <cstdlib>
#include <getopt.h>
#include <sys/select.h>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <string>
#include <ctype.h>

#include "data_collection.h"

using namespace std;

static bool isInteger(const char* str)
{
    if (str == NULL || (strcmp(str, "") == 0)) {
        return false;
    }

    int strLength = strlen(str);

    for (int i = 0; i < strLength; i++) {
        if (!isdigit(str[i])) {
            return false;
        }
    }
    return true;
}

static bool isFloat(const char* str)
{
    if (str == NULL || (strcmp(str, "") == 0)) {
        return false;
    }

    int strLength = strlen(str);
    bool hasDecimalPoint = false;

    for (int i = 0; i < strLength; i++) {
        if (str[i] == '.') {

            if (hasDecimalPoint) {
                return false;
            }
            hasDecimalPoint = true;
        } else if (!isdigit(str[i])) {
            return false;
        }
    }

    return hasDecimalPoint && strLength > 1;
}

static bool isExitKeyPressed()
{
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


int main(int argc, char *argv[])
{
    float data_collection_duration_s= 0;
    bool startFlag = false;
    bool timedCaptureFlag = false;
    bool use_ps_io_flag = false;
    uint8_t boardID; 
    

    // cmd line variables
    uint8_t min_args = 1;

    if (argc == 1) {  
        cout << endl;
        cout << "                 dVRK Data Collection Program" << endl;
        cout << "|-----------------------------------------------------------------------" << endl;
        cout << "|Usage: " << argv[0] << " <boardID> [-t <seconds>] [-i]" << endl;
        cout << "|" <<endl;
        cout << "|Arguments:" << endl;
        cout << "|  <boardID>          Required. ID of the board to connect to." << endl;
        cout << "|" << endl;
        cout << "|Options:" << endl;
        cout << "|  -t <seconds>       Optional. Duration for data capture in seconds (float)." << endl;
        cout << "|  -i                 Optional. Add PS IO to packet for data collection." << endl;
        cout << "|" << endl;
        cout << "|[NOTE] Ensure the server is started before running the client." << endl;
        cout << "__________________________________________________________________________" << endl;
        return 0;
    } 

    if (!isInteger(argv[1])) {
        cout << "[ERROR] Invalid boardID arg: " << argv[1] << endl;
        return -1;
    } else {
        boardID = atoi(argv[1]);
    }

    if (argc >= 6){
        cout << "[ERROR] Too many cmd line args" << endl;
        return -1;
    }

    for (int i = min_args + 1; i < argc; i++ ){

        if (argv[i][0] == '-') {

            if (argv[i][1] == 't') {

                if (!isFloat(argv[i+1])) {
                    cout << "[ERROR] invalid time value " << argv[i+1] << " for timed capture. Pass in float" << endl;
                    return -1;
                } else {
                    data_collection_duration_s = atof(argv[i+1]);
                    timedCaptureFlag = true;
                    cout << "Timed Capture Enabled!" << endl;
                    i+=1;
                }
            }

            else if (argv[i][1] == 'i' ) {
                use_ps_io_flag = true;
                cout << "PS IO pins will be included in data packet!" << endl;
            } else {
                cout << "[ERROR] Invalid arg: " << argv[i] << endl;
            }
        } else {
            cout << "[ERROR] invalid arg: " << argv[i] << endl;
            return -1;
        }
    }

    bool ret;

    DataCollection *DC = new DataCollection();
    bool stop_data_collection = false;

    if (!DC->init(boardID, use_ps_io_flag)) {
        return -1;
    }

    int count = 1;

    while (!stop_data_collection) {

        cout << "Woud you like to start capture [" << count << "]? (y/n): ";

        char yn;
        cin >> yn;

        if (yn == 'y') {
            stop_data_collection = false;
        } else if (yn == 'n') {
            stop_data_collection = true;
            continue;
        } else {
            cout << "[error] Invalid character. Type either 'y' or 'n' and press enter: " << endl;
            stop_data_collection = false;
            continue;         
        }

        cout << endl;
    
        if (!DC->start()) {
            return -1;
        }
        cout << "...Press [ENTER] to terminate capture" << endl;

        if (timedCaptureFlag) {
            int data_collection_duration_us = data_collection_duration_s * 1000000;
            usleep(data_collection_duration_us);
        } else {
            while(1) {
                if (isExitKeyPressed()) {
                    break;
                } 
            }
        }

        if (!DC->stop()) {
            return -1;
        }

        count++;
    }

    ret = DC->terminate();

    return ret;
}
