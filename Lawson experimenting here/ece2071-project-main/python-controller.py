import numpy as np
import wave
import os
import questionary
import matplotlib.pyplot as plt


import serial
import serial.tools.list_ports
import time

import keyboard

# UI
os.system('cls' if os.name == 'nt' else 'clear') # clear console

valid_input = False

target_recording_length = 0
trigger_distance = 0

while valid_input == False:

    recording_name = questionary.text("Please enter a name for your recording: ").ask()

    if(recording_name) == "":
        print("Invalid input detected!\n")
        continue

    mode = questionary.select(
        "What operating mode would you like to use?",
        choices=["Manual Recording Mode", "Distance Trigger Mode"],
    ).ask()

    if mode == "Manual Recording Mode":
        target_recording_length_str = questionary.text("Please enter your desired recording length (s): ").ask()

        try: # try casting input to int
            target_recording_length = int(target_recording_length_str)

            if target_recording_length <= 0:
                print("Invalid input detected!\n")
                continue

        except ValueError:
            print("Invalid input detected!\n")
            continue

    elif mode == "Distance Trigger Mode":
        trigger_distance_str = questionary.text("Please enter the distance to stop recording at (cm): ", default="10").ask()

        try: # try casting input to int
            trigger_distance = int(trigger_distance_str)

            if trigger_distance <= 0:
                print("Invalid input detected!\n")
                continue

        except ValueError:
            print("Invalid input detected!\n")
            continue

    formats = questionary.checkbox(
    "Please select your desired output formats", choices=[".wav", ".png (Amplitude vs Time)", ".csv (Raw Data)"]).ask()

    if formats == []:
        print("Invalid input detected! \n")
        continue

        

            
    valid_input = True



# Backend

#poll serial devices
devices = serial.tools.list_ports.comports()

for ports in devices:
    #print (ports)
    continue

ser = serial.Serial(
    "COM6",
    baudrate=921600
)

sample_rate = 44100

if mode == "Manual Recording Mode":
    target_samples = sample_rate * target_recording_length #recording len
    bytes_to_read = target_samples * 2
    for i in range(10): #it sometimes misses transmissions, so sending multiple to be safe.
        ser.write(bytes([0, target_recording_length])) #send mode (0 for manual, 1 for distance), and recording length

elif mode == "Distance Trigger Mode":
    for i in range(10):
        ser.write(bytes([1, trigger_distance]))


print("Collecting data...")
# Read in chunks until timeout or all bytes received
ser.timeout = 0.25  # 250 ms timeout
buffer = bytearray()

if mode == "Distance Trigger Mode":
    print("Press 'q' to quit!")

while True:

    if mode == "Distance Trigger Mode":
        if keyboard.is_pressed('q'):
            print("User aborted")
            for i in range(5):
                ser.write(bytes([2, 0])) # tell STM to stop recording
            break

    chunk = ser.read(256) #read in 256 byte chunks - more efficent for python, but won't drop a whole packet if we lose 1 byte
    
    if not chunk:
        break  # timeout → no more data
    
    buffer.extend(chunk)

print("Processing data...")

# Parse bytes into numpy array

raw_audio_data = np.frombuffer(buffer, dtype='>u2').astype(np.int16) #convert raw bytes to 16 bit datapoints

raw_audio_data_mean = np.mean(raw_audio_data)

# Convert unsigned to signed & centered
audio_data = raw_audio_data - int(raw_audio_data_mean)

# Scale to full 16-bit range for .wav
audio_data = audio_data << 4

raw_audio_data_trimmed = raw_audio_data #initalise trimmed var so it still works in ultrasonic sensor mode

actual_recording_length = audio_data.size / sample_rate

if mode == "Manual Recording Mode":

    # trim extra samples if we received too much data
    if audio_data.size > target_samples:
        extra_samples = audio_data.size - target_samples
        print(f"Received {extra_samples} extra samples ({extra_samples*0.02268:.2f}ms), trimming...")
        audio_data = audio_data[:target_samples]

    # warn if we received too little
    elif audio_data.size < target_samples:
        print(f"Warning: Missing {target_samples - audio_data.size} samples")

    # mask out undesired datapoints
    raw_audio_data_trimmed = raw_audio_data[:target_samples] 

    # recalculate length after trimming
    actual_recording_length = audio_data.size / sample_rate



    



output_dir = os.path.join(".", "outputs")
os.makedirs(output_dir, exist_ok=True)

file_name = recording_name + " - " + str(sample_rate) + "sps" + " - H04"

if ".wav" in formats: # if wav selected

    wav_output_path = os.path.join(output_dir, file_name + ".wav") # format output path


    with wave.open(wav_output_path, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(audio_data.tobytes())

        print("Saving:   ", os.path.abspath(wav_output_path))

if ".png (Amplitude vs Time)" in formats: # if making a graph
    time_datapoints = np.linspace(0, actual_recording_length, audio_data.size)

    plt.plot(time_datapoints, raw_audio_data_trimmed)
    plt.xlabel("Time (s)")
    plt.ylabel("Unscaled Amplitude")
    plt.title(file_name + " - Amplitude vs Time")

    png_output_path = os.path.join(output_dir, file_name + ".png")
    plt.savefig(png_output_path)
    

if ".csv (Raw Data)" in formats: # if making a csv
    csv_output_path = os.path.join(output_dir, file_name + ".csv")

    np.savetxt(csv_output_path, raw_audio_data_trimmed, delimiter=",", header="Sample Rate" + str(sample_rate))



print("Complete!")