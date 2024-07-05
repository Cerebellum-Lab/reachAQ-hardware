import matplotlib.pyplot as plt
import numpy as np

from sinusoidal import SinusoidalCurveGenerator


def main():
    t, position = SinusoidalCurveGenerator.stepper_curve(10000, 3000, 10000)

    time_diff = np.diff(t)

    # Plot graphs for Position and Acceleration
    plt.figure(figsize=(12, 8))
    plt.suptitle('Cosine Curve Profile')

    # Plot the position graph
    plt.subplot(2, 1, 1)
    plt.plot(t, position, marker='.')
    plt.xlabel('Time (s)')
    plt.ylabel('Position (step)')
    plt.grid()

    # Plot the time difference graph
    plt.subplot(2, 1, 2)
    plt.plot(position[1:], time_diff, marker='.')
    plt.xlabel('Position (step)')
    plt.ylabel('Time Difference (s)')
    plt.grid()

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
