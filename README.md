# Embedded Data Collection Tool

This is repository contains the source code and executables for embedded data collection for FPGAv3 by using the Zynq 7000 SoC ARM processor to collect high frequency kinematics data on the Davinci Si.
### Overview

The data collection tool obtains kinematics data using two programs.

The first program runs on the Zynq processor which polls data using the EMIO pins which route directly to FPGA registers. Then the data is sent via ethernet (udp packets) to the host program 

The second program, host program, dumps the timestamped kinematics data data into a csv file to the host. 
### Compilation

There are two executables used for embedded data collection. 

The executable running on the host is compiled using cmake in the root directory which will output an executable in a `bin` folder called **`dvrkDataCollection-HOST`**. 

The second executable is meant to run on the zynq processor. There is a precompiled program stored in the `src` directory as **`dvrkDataCollection-ZYNQ`**. This program is good to run as is. The source code for this tool is also provided in case someone wants to edit the original program and compile the program for the Zynq using a toolchain file that can be generated using [Mechatronics Embedded](https://github.com/jhu-cisst/mechatronics-embedded.git) repo. 

### Running

- Connect an ethernet cable to either ethernet port on the FPGA.

- Load the **`dvrkDataCollection-ZYNQ`** executable on the Zynq Processor. 

    
    There are two ways to do this. The most efficient way is to use an scp command to transfer the data over ethernet using tcp. To do as such run:

            scp dvrkDataCollection-ZYNQ root@169.254.10.2:~/media/targetDir


