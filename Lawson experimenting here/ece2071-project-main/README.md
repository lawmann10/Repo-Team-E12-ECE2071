# ECE2071 project

## Overview

Utilises two STM MCUs to receive, process, and output an audio signal. Features an easy-to-use Python CLI with configurable recording options & modes. 

## Setup

Set up physical hardware as seen in the block flow diagram below:

<img width="1499" height="638" alt="image" src="https://github.com/user-attachments/assets/d7869153-6959-4135-891f-124040d418c2" />

<br>

Check which COM port the processing STM is connected to. You can find this in `Control Panel -> Device Manager (Windows)`. Once you've found the COM port, hardcode this into the python file.

Ensure the required Python packages are installed. The required packages are visible at the top of `python-controller.py`


## Usage

Run the Python script:
```bash
python3 .\python-controller.py
```

Step through the interactive CLI:

<img width="745" height="211" alt="image" src="https://github.com/user-attachments/assets/11f8e3bd-71cd-481c-bd17-d2c31edd6336" />

<br>

Once the recording has completed, the program should pronounce the location of the saved file(s). By default, this is in the directory `outputs` one level down from the Python script.

To take another recording, run the python file again!

## Parameters

| Parameter    | Description  | 
| ------------ | ---------  | 
| File Name &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp;| The desired name of the output file. Note, this will not be the full name as file details will be appended to the end. | 
| Manual Recording Mode | Will record for a set duration. | 
| Distance Trigger Mode | Will continiously record. If there is an object within the user-specified range of the ultrasonic sensor, it will record the input, otherwise, it will record silence. Only exits this mode when the user presses 'q'. | 
| Desired Recording Length | The desired recording length in manual mode to the nearest second. | 
| Distance to Stop Recording | The distance required between the ultrasonic sensor and an object to record silence. |
| .wav Output | Will produce a 16 bit .wav file at 44.1ksps with the collected data. |
| .png Output | Will produce an amplitude vs time graph of the recorded data. |
| .csv Output | Will produce a .csv file with the recorded data. The header column is populated with the sample rate. |
