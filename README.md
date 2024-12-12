# Embedded Data Collection Tool

This repository contains the source code for embedded data collection for FPGA V3, using the Zynq 7000 SoC ARM processor to collect high frequency data.

## Overview

The data collection tool consists of two programs.

The first program runs on the Zynq processor which polls data using the EMIO pins which route directly to FPGA registers. Then the data is sent via ethernet (udp packets) to the host program.

The second program, host program, dumps the timestamped data into a csv file on the host.

## Dependencies

Make sure to have the most current firmware running on the FPGA found [here](https://github.com/jhu-cisst/mechatronics-firmware/releases/).

## Compilation

There are two executables used for embedded data collection.

The executable running on the host is compiled using cmake in the `host` directory which will output an executable in a `bin` folder called **`dvrkDataCollection-HOST`**.

The second executable is meant to run on the Zynq processor. There is a precompiled program stored in releases on github called **`dvrkDataCollection-ZYNQ`**. This program is good to run as is. The source code for this tool is also provided in the `zynq` directory in case someone wants to edit the original program and compile the program for the Zynq using a toolchain file that can be generated using [Mechatronics Embedded](https://github.com/jhu-cisst/mechatronics-embedded.git) repo and generating the build tree by setting the toolchain file for cross compilation using:
`cmake -DCMAKE_TOOLCHAIN_FILE=path/to/toolchain_clang_fpgav3.cmake <path/to/src>`.

## Running

- Connect an ethernet cable to either ethernet port on the FPGA.

- Download the **`dvrkDataCollection-ZYNQ`** executable from [Releases](https://github.com/noahdrakes/dvrkDataCollection/releases/) (or compile the source code by running cmake in the `zynq` directory).

- Load the **`dvrkDataCollection-ZYNQ`** executable on the Zynq processor.

There are two ways to do this. The most efficient way is to use a scp command to transfer the data over ethernet using TCP:
```
scp dvrkDataCollection-ZYNQ root@169.254.10.N:~/media/targetDir
```
where N is the Board ID and the exectubale will appear in ~/media/targetDir/.

- Compile the host binary by running cmake on the `host` directory to generate the **`dvrkDataCollection-HOST`** exectuable.

- SSH into the zynq processor: `ssh root@169.254.10.N`, where N is the boardID of the controller.
- Start the Zynq program first by running:

```
        cd /media/bin
        ./dvrkDataCollection-ZYNQ
```

- Start the Host program by cd'ing into the `bin` folder inside the build tree and run:

```
        ./dvrkDataCollection-HOST
```

The host program output will guide you on how to collect data.

## Output

The program will output a csv file for each capture containing the following data for each axis:

*Timestamp,* *EncoderPos1*,..,*EncoderPosN*, *EncoderVel1*, *EncoderVelN*, *MotorCurrent1*, *MotorCurrentN*, *CommandedCur1*, *CommandedCurrN*, *BoardIO*, in that order.

The filename for each capture is capture_[date and time].csv


###### Contact Info
Send me an email if you have any questions.
Noah Drakes
email: ndrakes1@jh.edu

