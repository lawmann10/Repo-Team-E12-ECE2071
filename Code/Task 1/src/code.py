import numpy as np
import wave
import serial

COM = "/dev/tty.usbmodem103"
baudrate = 115200

ser = serial.Serial(COM, baudrate, timeout=1)

audio = bytearray()

SAMPLE_RATE = 11000
recordingTime = 5

print("START")

totalSamples = recordingTime * SAMPLE_RATE
audio = ser.read(totalSamples)
data = np.frombuffer(audio, dtype=np.uint8)

with wave.open(f"../outputs/E12_{SAMPLE_RATE}Hz_audio.wav", 'wb') as wf:
    wf.setnchannels(1)
    wf.setsampwidth(1)  # 8-bit
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(data.tobytes())

print("DONE")