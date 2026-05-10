import numpy as np
import wave
import serial
import matplotlib.pyplot as plt
import time

# COM = "COM6"
COM = "/dev/tty.usbmodem103"
baudrate = 921600               # Highest allowed rate
SAMPLE_RATE = 44100             # Changed to hold the higher sampling rate of to hold 44.1kspsp 
Team_ID = "E12"

ser = serial.Serial(COM, baudrate, timeout=1)


def save_wav(data, filename): #function to save wav
        # Change to 32-bit to stop overflow, then convert data from a max of 4095 to a max of 65535
        data_16bit = (data.astype(np.uint32) * 65535 // 4095).astype(np.uint16)     # Scales the audio data to be 16-bit without the extra 4 bits
        
        with wave.open(filename, 'wb') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)  # 16-bit doesnt need to change as STM should scale from 10 -> 8
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(data_16bit.tobytes())
        print(f"Saved: {filename}")

def save_png(data, filename): #function to save png
    time_axis = np.linspace(0, len(data) / SAMPLE_RATE, len(data))
    plt.figure()
    plt.plot(time_axis, data)

    plt.xlabel("Time (s)")
    plt.ylabel("Amplitude (12-bit)")    # Updated to represent bit amount
    plt.title(f"Audio Amplitude vs Time | {Team_ID} | {SAMPLE_RATE}Hz | 12-bit")    # Updated to represent bit amount
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
    timestamp = time.strftime("%d_%H%M%S")              # So output files dont overwrite each other
    base = f"{Team_ID}_{SAMPLE_RATE}Hz_{timestamp}"

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

def audio_fix(raw_bytes):
    # Make sure that all byts have their pair to make 12-bits
    if len(raw_bytes) % 2 != 0:
        raw_bytes = raw_bytes[:-1]
    
    # Sort the bytes into pairs
    """
    NEED TO TEST
    dtype >u2 means that the first byte sent is the larger number
    dtype <u2 means that the first byte sent is the smaller number
    """
    data = np.frombuffer(raw_bytes, dtype='>u2')
    data = data.astype(np.uint16)
    return data

def get_output_types():
    print("\n Available output types: wav, png, csv")
    chosen = input("Select format(s) (e.g. wav,png,csv): ")
    return [x.strip().lower() for x in chosen.split(",")]

def print_help():
    print("""
            ECE2071 Audio Recorder - CLI Help
            Modes:
            manual      -   Record for a user-specified time

            distance    -   Proximity triggered recording (~10cm)
                            Runs continuosly until exited
            
            help        -   Print this message
            quit        - Exits the program
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
    raw = bytearray()
    start = time.time()

    while time.time() - start < recording_time:
        chunk = ser.read(ser.in_waiting or 2)
        if chunk:
            raw.extend(chunk)

    # Trim to even number of bytes
    if len(raw) % 2 != 0:
        raw = raw[:-1]

    """ Uncomment if we want to add stop byte stuff to stop the STM from clogging up otherwise we have to reset the STM for each recording"""
    # ser.write(b'S')
    # time.sleep(0.1)                   # Wait for STM to stop
    # ser.reset_input_buffer()          # Discards old/garbage bits to allow for clean back to back recordings

    print(f"Captured {len(raw) // 2} Samples")

    if len(raw) == 0:
        print("No data received")
        return
    
    data = audio_fix(raw)
    save_files(output_types, data)

def distance_mode():
    output_types = get_output_types()
    print("\n  Distance Trigger Mode active.")
    print("  The system will record automatically when an object is detected within 10cm.")
    print("  Press Ctrl+C to return to the main menu.\n")


    try:
        while True:
            raw = bytearray()
            ser.write(b'D')                 # Set STM to distance Mode  
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

                raw.append(byte[0])

            if len(raw) % 2 != 0:
                raw = raw[:-1]

            print(f"Recording Stopped. Captured {len(raw) // 2} Samples")

            if len(raw) == 0:
                print("No audio Data")
                continue

            data = audio_fix(raw)
            save_files(output_types, data)

    except KeyboardInterrupt:
        """ Uncomment if we want to add stop byte stuff to stop the STM from clogging up otherwise we have to reset the STM for each recording"""
        # ser.write(b'S')
        # time.sleep(0.1)                   # Wait for STM to stop
        # ser.reset_input_buffer()          # Discards old/garbage bits to allow for clean back to back recordings
        print('\n Ending Distance Trigger Mode')

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



        



