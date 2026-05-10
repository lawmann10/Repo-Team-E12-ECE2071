import numpy as np
import wave
import serial
import matplotlib.pyplot as plt
import time
from pathlib import Path

# COM = "COM6"
COM = "/dev/tty.usbmodem103"
baudrate = 921600               # Highest allowed rate
SAMPLE_RATE = 44100             # Changed to hold the higher sampling rate of to hold 44.1kspsp 
Team_ID = "E12"

Path("./outputs").mkdir(parents=True, exist_ok=True)

ser = serial.Serial(COM, baudrate, timeout=1)

def save_wav(data, filename): #function to save wav  
        # Data needs to be centered and bit shifted to 16-bit      
        with wave.open(f"./outputs/{filename}", 'wb') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)  # 16-bit 
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(data.tobytes())
        print(f"Saved: {filename}")

def save_png(data, filename, length): #function to save png
    time_axis = np.linspace(0, length, len(data))
    plt.figure()
    plt.plot(time_axis, data)
    plt.xlabel("Time (s)")
    plt.ylabel("Amplitude Unscaled") 
    plt.title(f"Audio Amplitude vs Time | {Team_ID} | {SAMPLE_RATE}Hz | 12-bit")    # Updated to represent bit amount
    plt.tight_layout()
    plt.savefig(f"./outputs/{filename}")
    plt.close()
    print(f"Saved: {filename}")

def save_csv(data, filename): #function to save csv
    output_path = f"./outputs/{filename}"
    np.savetxt(output_path, data, delimiter=",", header =f"Sample Rate: {SAMPLE_RATE}", fmt="%d")
    print(f"Saved: {filename}")

def audio_fix(raw_bytes):
    """
    Converts bytes, centres around mean, and provides scaling for wav file    
    """

    # Convert to 16-bit using Big Endian
    raw_audio = np.frombuffer(raw_bytes, dtype='>u2').astype(np.uint16)

    # Centre the audio
    mean_val = np.mean(raw_audio)
    centered_audio = raw_audio.astype(np.uint32) - int(mean_val)

    # Scale to 16-bit for wav file by shifting left by 4
    scaled_audio = centered_audio << 4

    return raw_audio, scaled_audio

def save_files(output_types, raw_data, scaled_data):
    timestamp = time.strftime("%d_%H%M%S")              # So output files dont overwrite each other
    base = f"{Team_ID}_{SAMPLE_RATE}Hz_{timestamp}"
    actual_length = len(raw_data) / SAMPLE_RATE

    for fmt in output_types:
        if 'wav' in fmt:
            save_wav(scaled_data, f"{base}.wav")
        elif 'png' in fmt:
            save_png(raw_data, f"{base}.png", actual_length)
        elif 'csv' in fmt:
            save_csv(raw_data, f"{base}.csv")
        else:
            print(f"Unknown format - {fmt}")

def get_output_types():
    print("\n Available output types: wav, png, csv")
    chosen = input("Select format(s) (e.g. wav,png,csv): ")
    return [x.strip().lower() for x in chosen.split(",")]

def print_help():
    print("""
            ECE2071 Audio Recorder - CLI Help
            Modes:
            manual      -   Record for a user-specified time (Sends mode 0)
            distance    -   Trigger-based recording (Sends mode 1)
            help        -   Print this message
            quit        -   Exits the program
          """)
    
def manual_mode():
    try:
        recording_time = int(input(" Recording Length (seconds): "))
    except ValueError:
        print("Invalid input. Enter a number")
        return
    
    output_types = get_output_types()
    target_samples = SAMPLE_RATE * recording_time

    print(f"Signaling STM for Manual Mode - {recording_time}s")
    # Tell STM it is in Manual and give time for switch
    
    # Loop to ensure STM picks up the bytes
    for _ in range(10):
        ser.write(bytes([0, recording_time]))

    raw_buffer = bytearray()
    print("Collecting Data")

    while True:
        chunk = ser.read(256)
        if not chunk:
            break

        raw_buffer.extend(chunk)
    
    if not raw_buffer:
        print("No data received")
        return

    # Trim to target length
    raw_data, scaled_data = audio_fix(raw_buffer)
    # Remove any extra bytes from the recording cutting off late
    if len(raw_data) > target_samples:
        raw_data = raw_data[:target_samples]
        scaled_data = scaled_data[:target_samples]

    save_files(output_types, raw_data, scaled_data)

def distance_mode():
    try:
        dist = int(input(" Enter distance to stop (cm) [Default 10]: ") or "10")
    except ValueError:
        print("Invalid input.")
        return
        
    output_types = get_output_types()
    print("\n  Distance Trigger Mode active.")
    print(f"The system will record automatically when an object is detected within {dist}cm.")

    for _ in range(10):
        ser.write(bytes([1, dist]))

    raw_buffer = bytearray()
    print("Collecting Data (Press Ctrl+C to stop)")

    try:
        while True:
            chunk = ser.read(256)
            if chunk:
                raw_buffer.extend(chunk)
    
    except KeyboardInterrupt:
        print("\nStopping distance mode. Telling STM to stop recording")
        for _ in range(10):
            ser.write(bytes([2, 0])) # Stop command

    if raw_buffer:
        raw_data, scaled_data = audio_fix(raw_buffer)
        save_files(output_types, raw_data, scaled_data)
    else:
        print("No audio captured")

print_help()

try:
    while True:
        mode = input("Select Mode (manual / distance / help / quit)").strip().lower()

        if mode == "quit":
            print("Goodbye")
            break
        elif mode == 'help':
            print_help()
        elif mode == 'distance':
            distance_mode()
        elif mode == 'manual':
            manual_mode()
        else:
            print("Unrecognised mode, try 'help' for options")

finally:
    ser.close() 
    print("Serial Port Closed")