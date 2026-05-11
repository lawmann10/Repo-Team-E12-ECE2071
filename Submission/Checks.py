import numpy as np
data = np.loadtxt("outputs/E12_44100Hz_11_204442.csv", delimiter=",", skiprows=1)
print(f"Min: {data.min()}, Max: {data.max()}")