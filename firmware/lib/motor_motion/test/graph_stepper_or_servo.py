from numpy import genfromtxt
import matplotlib.pyplot as plt
data=genfromtxt('out.csv',delimiter=',')

steps=[(datum[1],datum[0]) for datum in data]

plt.plot(*zip(*steps))

plt.show()
