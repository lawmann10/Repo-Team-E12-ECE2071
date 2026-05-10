import numpy as np
import wave
import serial

ser = serial.Serial("/dev/tty.usbmodem103", 115200, timeout=1)

audio = bytearray()

SAMPLE_RATE = 22030

print("START")

for i in range(5 * SAMPLE_RATE):
    data = ser.read(1)
    if data:
        audio.append(data[0])

data = np.array(audio, dtype=np.uint8)

with wave.open("audio.wav", 'wb') as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)  # 16-bit
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(data.tobytes())

print("DONE")