import matplotlib.pyplot as plt

from sinusoidal import SinusoidalCurveGenerator


def main():
    t, position, velocity, acceleration = SinusoidalCurveGenerator.servo_curve(180, 60 / 0.14, 2000)

    # Plot graphs for Position and Acceleration
    plt.figure(figsize=(12, 8))
    plt.suptitle('Cosine Curve Profile')

    # Plot the position graph
    plt.subplot(3, 1, 1)
    plt.plot(t, position, marker='.')
    plt.xlabel('Time (s)')
    plt.ylabel('Position (mm)')

    # Plot the velocity graph
    plt.subplot(3, 1, 2)
    plt.plot(t, velocity)
    plt.xlabel('Time (s)')
    plt.ylabel('Velocity (mm/s)')

    # Plot the acceleration graph
    plt.subplot(3, 1, 3)
    plt.plot(t, acceleration)
    plt.xlabel('Time (s)')
    plt.ylabel('Acceleration (mm/s^2)')

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
