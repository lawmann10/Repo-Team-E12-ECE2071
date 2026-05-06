import numpy as np
import wave
import serial
import matplotlib.pyplot as plt

COM = "COM6"
baudrate = 115200

ser = serial.Serial(COM, baudrate, timeout=1)

audio = bytearray()
 
SAMPLE_RATE = 11000

triggerMode = input("Recording mode (Manual, Distance Trigger): ") # 2 input modes are manual and auto


def save_files(fileType, data): #function to determine output
    if fileType.lower() == "wav":
        with wave.open(f"E12_{SAMPLE_RATE}Hz_audio.wav", 'wb') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(1)  # 8-bit
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(data.tobytes())

    elif fileType.lower() == "png":
        np.linspace(0, 5)

        time_axis = np.linspace(0, len(data) / SAMPLE_RATE, len(data))
        plt.plot(time_axis, data)

        plt.xlabel("Time (s)")
        plt.ylabel("Amplitude")
        plt.title("Amplitude vs Time")
        plt.savefig(f"E12_{SAMPLE_RATE}Hz_image.png")

    elif fileType.lower() == "csv":

        with open(f"E12_{SAMPLE_RATE}Hz_data.csv", "w") as file:
            file.write(f"{SAMPLE_RATE}\n")
            np.savetxt(file, data, delimiter=",", fmt="%d")


print("START")

if triggerMode.lower() == "manual":

    recordingTime = eval(input("Recording Length (s): "))

    for i in range(int(recordingTime * SAMPLE_RATE)):
        data = ser.read(1)
        if data:
            audio.append(data[0])

    //save_files
    data = np.array(audio, dtype=np.uint8)

    outputType = input("Choose an Output Option (wav, png, csv): ")

    save_files(outputType, data)

elif triggerMode.lower() == "distance trigger":

    recording = False
    noise_count = 0
    audio = bytearray()
    print("Waiting for proximity trigger...")
    
    while ser.in_waiting == 0: #if no bytes are waiting, do nothing
        pass

    print("recording...")

    while True:
        byte = ser.read(1)
        if byte == b'\xff':  # stop byte received
            break
        audio.append(byte[0])

    #save files
    data = np.array(audio, dtype=np.uint8)

    outputType = input("Choose an Output Option (wav, png, csv): ")

    save_files(outputType, data)
       
    print("DONE")


