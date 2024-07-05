import math
import numpy as np
from scipy import optimize


class SinusoidalCurveGenerator:
    # From the YouTube here: https://www.youtube.com/watch?v=lpCWwYqE36Y
    # Which is based on the paper here: https://studylib.net/doc/8267759/sinusoidal-velocity-profiles-for-motion-control
    @staticmethod
    def servo_curve(displacement: float, velocity_max: float, acceleration_max: float):
        return SinusoidalCurveGenerator.cos_curve(displacement, velocity_max, acceleration_max, type="servo")

    @staticmethod
    def stepper_curve(displacement: int, velocity_max: int, acceleration_max: int):
        return SinusoidalCurveGenerator.cos_curve(displacement, velocity_max, acceleration_max, type="stepper")

    @staticmethod
    def cos_curve(displacement: float, velocity_max: float, acceleration_max: float, type="servo"):
        # Calculate a cosine curve profile
        # displacement: The total distance to travel
        # velocity_max: The maximum velocity of the axis
        # acceleration_max: The maximum acceleration of the axis
        # Returns: t, position, velocity, acceleration

        Am = acceleration_max
        Vm = velocity_max

        sgn = 1 if displacement > 0 else -1
        Yf = abs(displacement)
        Ys = Yf / 2
        Yaux = Vm ** 2 / Am
        Ya = Ys if Ys <= Yaux else Yaux
        Vw = math.sqrt(Ys * Am) if Ys <= Yaux else Vm
        To = Vw / Am
        Ta = 2 * To
        w = 2 * math.pi / Ta
        Ks = Ta * Vw / (4 * math.pi ** 2)
        Tk = 2 * (Ys - Ya) / Vm
        Ts = Ta + Tk / 2
        Tt = 2 * Ts

        def calculate_positions(t_n):
            if t_n <= 0:
                return 0
            if t_n <= Ta:
                return (Am / 4) * t_n ** 2 + Ks * (np.cos(w * t_n) - 1)
            elif t_n <= Ts:
                return Ys + Vw * (t_n - Ts)
            elif Ts < t_n:
                return Yf - calculate_positions(Tt - t_n)
            else:
                return 0

        def calculate_velocity(t_n):
            if t_n <= 0:
                return 0
            if t_n <= Ta:
                return Ks * w * (w * t_n - np.sin(w * t_n))
            elif t_n <= Ts:
                return Vw
            elif Ts < t_n:
                return calculate_velocity(Tt - t_n)
            else:
                return 0

        def calculate_acceleration(t_n):
            if t_n <= 0:
                return 0
            if t_n <= Ta:
                return (Am / 2) * (1 - np.cos(w * t_n))
            elif t_n <= Ts:
                return 0
            elif Ts < t_n:
                return -calculate_acceleration(Tt - t_n)
            else:
                return 0

        if type == "servo":
            # Generate the time points
            # For servo control there are fixed time steps of 20ms
            # and we only need to command where the position should be.
            # However, for this little simulation, I'll be plotting position, velocity, and acceleration
            t = np.arange(0.02, Tt, 0.02)
            position = np.array([sgn * calculate_positions(t_n) for t_n in t])
            velocity = np.array([sgn * calculate_velocity(t_n) for t_n in t])
            acceleration = np.array([sgn * calculate_acceleration(t_n) for t_n in t])
            return t, position, velocity, acceleration
        elif type == "stepper":
            # Generate positions (in steps) and figure out the times for each position
            position = np.arange(1, Yf)

            # Use numerical methods for figuring out the time corresponding to each position
            def find_t_n(p):
                def eqn(eqn_t, eqn_p):
                    calc_position = calculate_positions(eqn_t)
                    # Return the error between the calculated position and the desired position
                    return calc_position - eqn_p

                return optimize.bisect(eqn, 0, Tt, args=p, xtol=0.00001)

            t = np.array([find_t_n(p) for p in position])
            return t, position
