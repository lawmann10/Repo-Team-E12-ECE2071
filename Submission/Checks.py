import numpy as np
data = np.loadtxt("outputs/Bit.csv", delimiter=",", skiprows=1)
print(f"Min: {data.min()}, Max: {data.max()}")