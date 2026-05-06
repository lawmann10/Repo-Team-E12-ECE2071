import numpy as np
import wave
import serial
import matplotlib.pyplot as plt

# COM = "COM6"
COM = "/dev/tty.usbmodem103"
baudrate = 115200

ser = serial.Serial(COM, baudrate, timeout=1)

SAMPLE_RATE = 11520  # 115200 baud / 10 bits per byte = 11520 sps

def save_files(fileType, data): #function to determine output
    if fileType.lower() == "wav":
        with wave.open(f"E12_{SAMPLE_RATE}Hz_audio.wav", 'wb') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(1)  # 8-bit
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(data.tobytes())

    elif fileType.lower() == "png":
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

while True:
    triggerMode = input("Recording mode (Manual, Distance Trigger, Quit): ") # 2 input modes are manual and auto

    if triggerMode.lower() == "quit":
        break

    elif triggerMode.lower() == "manual":
        ser.write(b'M') #sends byte to processing STM to trigger "manual code"

        audio = bytearray()  # reset audio each recording
        recordingTime = float(input("Recording Length (s): "))

        for i in range(int(recordingTime * SAMPLE_RATE)):
            data = ser.read(1)
            if data:
                audio.append(data[0])

        # save files
        data = np.array(audio, dtype=np.uint8)
        outputType = input("Choose an Output Option (wav, png, csv): ")
        save_files(outputType, data)

    elif triggerMode.lower() == "distance trigger":
        ser.write(b'D')

        while True:
            audio = bytearray()
            print("Waiting for proximity trigger...")

            while ser.in_waiting == 0: #if no bytes are waiting, do nothing
                pass

            print("Recording...")

            while True:
                byte = ser.read(1)
                if byte == b'\xff':  # stop byte received
                    break
                if byte:
                    audio.append(byte[0])

            # save files
            data = np.array(audio, dtype=np.uint8)
            outputType = input("Choose an Output Option (wav, png, csv): ")
            save_files(outputType, data)

            print("DONE")

            again = input("Wait for next trigger? (y/n): ")
            if again.lower() != 'y':
                break