import serial

# Use 'ls /dev/tty.*' in terminal to find your exact port
# Usually it looks like /dev/tty.usbmodemXXXX
ser = serial.Serial('/dev/tty.usbmodem103', 115200, timeout=1)

print("Listening for distance data...")

try:
    while True:
        line = ser.readline().decode('utf-8').strip()
        if line:
            print(f"Distance: {line} cm")
except KeyboardInterrupt:
    ser.close()