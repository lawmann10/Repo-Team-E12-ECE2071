import numpy as np
import wave
import serial
import matplotlib.pyplot as plt
import time

# COM = "COM6"
COM = "/dev/tty.usbmodem103"
baudrate = 115200
SAMPLE_RATE = 10000  
Team_ID = "E12"

ser = serial.Serial(COM, baudrate, timeout=1)


def save_wav(data, filename): #function to save wav
        with wave.open(filename, 'wb') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(1)  # 8-bit
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(data.tobytes())
        print(f"Saved: {filename}")

def save_png(data, filename): #function to save png
    time_axis = np.linspace(0, len(data) / SAMPLE_RATE, len(data))
    plt.figure()
    plt.plot(time_axis, data)

    plt.xlabel("Time (s)")
    plt.ylabel("Amplitude (8-bit)")
    plt.title(f"Audio Amplitude vs Time | {Team_ID} | {SAMPLE_RATE}Hz")
    plt.tight_layout()
    plt.savefig(filename)
    plt.close()
    print(f"Saved: {filename}")

def save_csv(data, filename): #function to save csv
    with open(filename, "w") as file:
        file.write(f"{SAMPLE_RATE}\n")
        np.savetxt(file, data, delimiter=",", fmt="%d")
    print(f"Saved: {filename}")

def save_files(output_types, data):
    base = f"{Team_ID}_{SAMPLE_RATE}Hz"

    for fmt in output_types:
        fmt = fmt.strip().lower()
        if fmt == 'wav':
            save_wav(data, f"{base}.wav")
        elif fmt == 'png':
            save_png(data, f"{base}.png")
        elif fmt == 'csv':
            save_csv(data, f"{base}.csv")
        else:
            print(f"Unknown format - {fmt}")

def get_output_types():
    print("\n Available output types: wav, png, csv")
    chosen = input("Select format(s) (e.g. wav,png,csv): ")
    return [x.strip().lower() for x in chosen.split(",")]

def print_help():
    print("""
    ============================================================
     ECE2071 Audio Recorder  |  Team E12  |  CLI Help
    ============================================================
     Modes:
       manual    - Manually record for a user-specified duration.
                   You will be prompted for:
                     > Recording length (seconds)
                     > Output format(s): wav, png, csv

       distance  - Proximity-triggered recording.
                   Recording starts automatically when an object
                   is detected within your chosen distance, and
                   stops once the object moves away.
                   You will be prompted for:
                     > Detection distance threshold (cm)
                     > Output format(s): wav, png, csv
                   Press Ctrl+C to return to the main menu.

       help      - Show this help message.
       quit      - Close the program and release the serial port.
    ============================================================
     Output formats:
       wav  - Playable audio file
       png  - Amplitude vs Time waveform plot
       csv  - Raw sample data (first row = sample rate)
    ============================================================
    """)
    
def manual_mode():
    try:
        recording_time = float(input(" Recording Length (seconds): "))
    except ValueError:
        print("Invalid input. Enter a number")
        return
    
    output_types = get_output_types()

    # Tell STM it is in Manual and give time for switch
    ser.write(b'M')
    time.sleep(0.1)

    print(f"\n Recording for {recording_time}s")
    audio = bytearray()
    start = time.time()

    while time.time() - start < recording_time:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            audio.extend(chunk)

    """ Uncomment if we want to add stop byte stuff to stop the STM from clogging up otherwise we have to reset the STM for each recording"""
    ser.write(b'S')
    time.sleep(0.1)                   # Wait for STM to stop
    ser.reset_input_buffer()          # Discards old/garbage bits to allow for clean back to back recordings

    print(f"Captured {len(audio)} Samples")

    if len(audio) == 0:
        print("No data received")
        return
    
    data = np.array(audio, dtype= np.uint8)
    save_files(output_types, data)

def distance_mode():
    try:
        threshold = int(input("  Detection distance threshold (cm, default 10): ").strip() or "10")
        threshold = max(1, min(threshold, 255))
    except ValueError:
        print("  Invalid input, using default of 10cm.")
        threshold = 10

    output_types = get_output_types()
    print(f"\n  Distance Trigger Mode active (threshold: {threshold}cm).")
    print("  Recording starts automatically when an object is detected.")
    print("  Press Ctrl+C to return to the main menu.\n")

    try:
        while True:
            audio = bytearray()
            ser.write(b'D')                 # Set STM to distance mode
            ser.write(bytes([threshold]))   # Send threshold (1 byte, 1-255 cm)
            print("Waiting for trigger")

            # When something is detected
            while ser.in_waiting == 0:
                time.sleep(0.01)
            
            print("Object Deteceted")

            # Read audio until stop byte
            while True:
                byte = ser.read(1)
                if not byte:
                    continue

                # Received stop byte (SHOULD CHANGE STOP BYTE TO BE A LETTER, once done change this to the letter)
                if byte == b'\xff':
                    break

                audio.append(byte[0])

            print(f"Recording Stopped. Captured {len(audio)} Samples")

            if len(audio) == 0:
                print("No audio Data")
                continue

            data = np.array(audio, dtype = np.uint8)
            save_files(output_types, data)

    except KeyboardInterrupt:
        """ Uncomment if we want to add stop byte stuff to stop the STM from clogging up otherwise we have to reset the STM for each recording"""
        ser.write(b'S')
        time.sleep(0.1)                   # Wait for STM to stop
        ser.reset_input_buffer()          # Discards old/garbage bits to allow for clean back to back recordings
        print('\n Ending Distance Trigger Mode')

print_help()

try:
    while True:
        mode = input("Select Mode (manual / distance / help / quit): ").strip().lower()

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



        



