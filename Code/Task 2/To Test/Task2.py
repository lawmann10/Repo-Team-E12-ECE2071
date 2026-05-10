import numpy as np
import wave
import serial
import matplotlib.pyplot as plt
import time

# COM = "COM6"
COM = "/dev/tty.usbmodem103"
baudrate = 921600           # Updated to match working group's higher baud rate
SAMPLE_RATE = 9708
Team_ID = "E12"

ser = serial.Serial(COM, baudrate, timeout=1)


def save_wav(data, filename):
    with wave.open(filename, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(1)  # 8-bit
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(data.tobytes())
    print(f"Saved: {filename}")

def save_png(data, filename):
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

def save_csv(data, filename):
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
            ECE2071 Audio Recorder - CLI Help
            Modes:
            manual      -   Record for a user-specified time

            distance    -   Proximity triggered recording (configurable distance)
                            Runs continuously until exited
            
            help        -   Print this message
            quit        -   Exits the program
          """)

def manual_mode():
    try:
        recording_time = float(input(" Recording Length (seconds): "))
    except ValueError:
        print("Invalid input. Enter a number")
        return

    output_types = get_output_types()

    # Send mode command as newline-terminated string (matching working group architecture)
    ser.reset_input_buffer()
    ser.write(b'M\n')

    # Wait for STM ready signal before starting timer
    print("Waiting for STM ready signal...")
    ready = ser.read(1)
    if ready != b'R':
        print("Warning: Did not receive ready signal, proceeding anyway")

    print(f"\n Recording for {recording_time}s")
    audio = bytearray()

    start = time.time()
    while time.time() - start < recording_time:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            audio.extend(chunk)

    ser.write(b'S\n')
    time.sleep(0.1)
    ser.reset_input_buffer()

    print(f"Captured {len(audio)} Samples")

    if len(audio) == 0:
        print("No data received")
        return

    data = np.array(audio, dtype=np.uint8)
    save_files(output_types, data)

def distance_mode():
    try:
        # Allow user to configure distance from Python (sent to STM in command)
        distance = int(input(" Trigger distance in cm (default 10): ") or "10")
    except ValueError:
        distance = 10
        print("Invalid input, using default 10cm")

    output_types = get_output_types()
    print(f"\n  Distance Trigger Mode active (trigger at {distance}cm).")
    print("  The system will record automatically when an object is detected.")
    print("  Press Ctrl+C to return to the main menu.\n")

    # Send 'D' followed by distance and newline e.g. "D10\n"
    # This matches the working group's command protocol
    ser.reset_input_buffer()
    ser.write(f'D{distance}\n'.encode())

    try:
        while True:
            audio = bytearray()
            print("Waiting for trigger...")

            # Wait until STM starts sending data (object detected)
            while ser.in_waiting == 0:
                time.sleep(0.01)

            print("Object Detected — Recording...")

            # Read audio until stop byte (0xFF)
            # STM caps all audio samples to 0xFE so 0xFF is unambiguous
            while True:
                byte = ser.read(1)
                if not byte:
                    continue

                if byte == b'\xff':
                    # Drain any trailing bytes queued after the stop byte
                    time.sleep(0.01)
                    ser.reset_input_buffer()
                    break

                audio.append(byte[0])

            print(f"Recording Stopped. Captured {len(audio)} Samples")

            if len(audio) == 0:
                print("No audio data captured.")
                continue

            data = np.array(audio, dtype=np.uint8)
            save_files(output_types, data)

    except KeyboardInterrupt:
        ser.write(b'S\n')
        time.sleep(0.1)
        ser.reset_input_buffer()
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