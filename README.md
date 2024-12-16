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

The executable running on the host is compiled using cmake in the `host` directory which will output an executable in a `bin` folder called **`dvrk-data-collection-host`**.

The second executable must be cross-compiled to run on the Zynq processor. Due to the greater effort required, a precompiled executable, called **`dvrk-data-collection-zynq`**, will be stored on GitHub releases for each [mechatronics-embedded release](https://github.com/jhu-cisst/mechatronics-embedded/releases). Eventually, `dvrk-data-collection-zynq` may be added to the mechatronics-embedded release.

If it is necessary to build `dvrk-data-collection-zynq`, the source code is provided in the `zynq` directory and must be cross-compiled using a toolchain file (`toolchain_clang_fpgav3.cmake` or `toolchain_vitis_fpgav3.cmake`) that can be generated using the [mechatronics-embedded](https://github.com/jhu-cisst/mechatronics-embedded.git) repository:
```
cmake -DCMAKE_TOOLCHAIN_FILE=<path/to/toolchain_file> <path/to/src>
```
There is also a dependency on the Amp1394 library in the [mechatronics-software](https://github.com/jhu-cisst/mechatronics-software.git) repository, which must also have been cross-compiled for the Zynq using the toolchain file. The path to this dependency (`Amp1394Config.cmake`) is specified via the `Amp1394_DIR` variable in CMake.

## Running

- Connect an ethernet cable to either ethernet port on the FPGA.

- Download the **`dvrk-data-collection-zynq`** executable from [Releases](https://github.com/jhu-dvrk/fpgav3-data-collection/releases/) (or cross-compile the source code by running cmake in the `zynq` directory, as described above).

- Load the **`dvrk-data-collection-zynq`** executable on the Zynq processor.

There are two ways to do this. The most efficient way is to use a scp command to transfer the data over ethernet using TCP:
```
scp dvrk-data-collection-zynq root@169.254.10.N:~/media
```
where N is the Board ID. The executable will appear in ~/media.

- Compile the host binary by running cmake on the `host` directory to generate the **`dvrk-data-collection-host`** exectuable.

- SSH into the zynq processor: `ssh root@169.254.10.N`, where N is the boardID of the controller.
- Start the Zynq program first by running:

```
        cd /media
        ./dvrk-data-collection-zynq
```

- Start the Host program by cd'ing into the `bin` folder inside the build tree and run:

```
        ./dvrk-data-collection-host <boardID> [-t <seconds>] [-i]
```

Where:
-    -t enables timed capture mode 

-    -i enables PS IO and FPGA digital I/O to be included in data collection CSV

The host program output will guide you on how to collect data.

## Output

The program will output a csv file for each capture containing the following data for each axis:

*Timestamp,* *EncoderPos1*,..,*EncoderPosN*, *EncoderVel1*, *EncoderVelN*, *MotorCurrent1*, *MotorCurrentN*, *CommandedCur1*, *CommandedCurrN*, *BoardIO* (optional), *MIOPins* (optional) in that order.

The filename for each capture is capture_[date and time].csv


###### Contact Info
Send me an email if you have any questions.
Noah Drakes
email: ndrakes1@jh.edu
