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

#include "udp_tx.h"
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
    int dataCollectionDuration = 0;
    bool startFlag = false;
    bool timedCaptureFlag = false;
    uint8_t boardID; 

    if (argc == 3) {

        if (!isInteger(argv[1])) {
            cout << "invalid boardID arg" << endl;
            return -1;
        }

        if (!isInteger(argv[2])) {
            cout << "invalid dataCollectionDuration arg" << endl;
            return -1;
        }

        dataCollectionDuration = atoi(argv[2]);
        boardID = atoi(argv[1]);
        timedCaptureFlag = true;

    // start boardID
    } else if (argc == 2) {

        if (!isInteger(argv[1])) {
            cout << "invalid boardID arg" << endl;
            return -1;
        }

        boardID = atoi(argv[1]);

    } else if (argc == 1) {

        cout << "Ethernet Client Program!" << endl;
        cout << "------------------------------" << endl;
        cout << "Usage: " << argv[0] << " <boardID> [captureTime] " << endl;
        cout << "where <captureTime> is an optional arg!" << endl;
        cout << "[NOTE] Make sure to start the server before you start the client" << endl;
        return 0;

    } else {
        // invalid arg count
        return -1; 
    }

    int client_socket;
    bool ret;

    DataCollection *DC = new DataCollection();
    bool stop_data_collection = false;

    ret = DC->init(boardID);

    if (!ret) {
        return -1;
    }

    int count = 1;

    while (!stop_data_collection) {

        printf("Woud you like to start capture [%d]? (y/n): ", count);

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
    
        ret = DC->start();
        printf("...Press [ENTER] to terminate capture\n");

        if (!ret) {
            return -1;
        }

        if (timedCaptureFlag) {
            sleep(dataCollectionDuration);
        } else {
            while(1) {
                if (isExitKeyPressed()) {
                    break;
                }
            }
        }

        ret = DC->stop();

        if (!ret) {
            return -1;
        }

        count++;
    }

    ret = DC->terminate();

    return 0;
}
