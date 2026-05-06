import numpy as np
import wave
import serial
import matplotlib.pyplot as plt

COM = "COM6"
baudrate = 115200

ser = serial.Serial(COM, baudrate, timeout=1)

audio = bytearray()
 
SAMPLE_RATE = 11000

triggerMode = input("Recording mode (manual, auto): ")


def save_files(fileType, data):
    if fileType.lower() == "wav":
        with wave.open(f"E12_{SAMPLE_RATE}Hz_audio.wav", 'wb') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(1)  # 8-bit
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(data.tobytes())

    elif fileType.lower() == "png":
        np.linspace(0, 5)

        plt.plot(data, recordingTime)

        plt.xlabel("Time (s)")
        plt.ylabel("Amplitude")
        plt.title("Amplitude vs Time")
        plt.savefig(f"E12_{SAMPLE_RATE}Hz_image.png")

    elif fileType.lower() == "csv":

        with open(f"E12_{SAMPLE_RATE}Hz_data.csv", "w") as file:
            file.write(f"{SAMPLE_RATE}\n")
            file.write(data)


print("START")

if triggerMode.lower() == "auto":

    recordingTime = eval(input("Recording Length (s): "))

    for i in range(recordingTime * SAMPLE_RATE):
        data = ser.read(1)
        if data:
            audio.append(data[0])

    data = np.array(audio, dtype=np.uint8)

    outputType = input("Choose an Output Option (wav, png, csv): ")

    save_files(outputType, data)

elif triggerMode == "manual":

    recording = False
    noise_count = 0
    audio = bytearray()

    while distance_mode:
        
        dist = distance_Stm()

        if dist <= 10:
            recording = True
            noise_count = 0
        else:
            noise_count += 1

        if recording:
            data = ser.read(ser.in_waiting)
            audio.extend(data)

        if noise_count > 20:
            recording = False
            outputType = input("Choose an Output Option (wav, png, csv): ")
            save_files(outputType, audio)
            break

    print("DONE")


