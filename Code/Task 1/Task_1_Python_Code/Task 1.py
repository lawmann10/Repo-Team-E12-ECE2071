import numpy as np
import wave
import serial
import time

#COM = "COM6"
COM = "/dev/tty.usbmodem103"
baudrate = 115200

ser = serial.Serial(COM, baudrate, timeout=1) #initialises spped

SAMPLE_RATE = 9708 #tells the wav file how quickly to playback audiobytes
recordingTime = 10 #hardcodes recording time

print("START")
audio = bytearray()

start = time.time() #records start time,
while time.time() - start < recordingTime: #allows us to read byte by byte, compared to ser.read which does it all at once
    sample = ser.read(1)
    if len(sample)>0:
        audio.append(sample[0])
       

data = np.array(audio)
data = (data - data.min()) #removes DC offset
if data.max() > 0:  #stop zero division
    data = (data / data.max() * 255).astype(np.uint8) #normalising data to 8 bit range
    
else:
    data = data.astype(np.uint8)

with wave.open(f"E12_{SAMPLE_RATE}Hz_audio.wav", 'wb') as wf:
    wf.setnchannels(1)
    wf.setsampwidth(1)
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(data.tobytes())

print("DONE")