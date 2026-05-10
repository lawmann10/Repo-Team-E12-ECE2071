import serial

ser = serial.Serial("/dev/tty.usbmodem103", 115200, timeout=1)  # change port

print("Listening...")
while True:
    data = ser.read(1)
    if data:
        val = data[0]
        if val == 0xFF:
            print("STOP BYTE received — object moved away")
        else:
            print(f"Audio byte: {val}")