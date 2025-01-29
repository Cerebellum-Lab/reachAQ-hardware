from numpy import genfromtxt
import matplotlib.pyplot as plt
data=genfromtxt('out.csv',delimiter=',')

steps=[(datum[0],datum[1]) for datum in data]
n_iters=[(datum[0],datum[2]) for datum in data]

plt.plot(*zip(*steps))
plt.plot(*zip(*n_iters))

plt.show()
