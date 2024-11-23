from logging import config
from ..motor_model import MotorModel
from .....watchable import Watchable
from ......config import settings


class ServoModel(MotorModel):
    """
    Model representing a servo motor, inheriting from MotorModel. 
    Sets default commandable position limits specific to servos.
    """
    SETTINGS_KEY = "Servo Motor"

    MIN_PWM_DURATION = settings["Servo Motor"]["Min PWM Duration"]
    MAX_PWM_DURATION = settings["Servo Motor"]["Max PWM Duration"]

    def __init__(self, name: str, instance: int):
        """
        Initialize the ServoModel with a name and instance, setting predefined position limits.

        Args:
            name (str): The name of the servo.
            instance (int): The instance ID of the servo.
        """

        super().__init__(name, instance)  # , self.SETTINGS_KEY)

        self._min_position = Watchable(0.0, only_on_change=False)
        self._max_position = Watchable(0.0, only_on_change=False)
        self._min_pwm_duration_us = Watchable(0.0, only_on_change=False)
        self._max_pwm_duration_us = Watchable(0.0, only_on_change=False)

    @property
    def min_position(self):
        return self._min_position.value

    @min_position.setter
    def min_position(self, value: float):
        if not self.is_valid_position(value):
            raise ValueError(
                f"Invalid Min Position <{value}>: must be on the interval [{self.MIN_POSITION},{self.MAX_POSITION}]")
        self._min_position.value = value

    @property
    def max_position(self):
        return self._max_position.value

    @max_position.setter
    def max_position(self, value: float):
        if not self.is_valid_position(value):
            raise ValueError(
                f"Invalid Max Position <{value}>: must be on the interval [{self.MIN_POSITION},{self.MAX_POSITION}]")
        self._max_position.value = value

    @property
    def min_pwm_duration_us(self):
        return self._min_pwm_duration_us.value

    def is_valid_pwm_duration_us(self, pwm_duration_us: float):
        try:
            return float(pwm_duration_us) >= self.MIN_PWM_DURATION and int(pwm_duration_us) <= self.MAX_PWM_DURATION
        except:
            return False

    @min_pwm_duration_us.setter
    def min_pwm_duration_us(self, value: float):
        if not self.is_valid_pwm_duration_us(value):
            raise ValueError(
                f"Invalid Min PWM Duration <{value}>: must be on the interval [{self.MIN_PWM_DURATION},{self.MAX_PWM_DURATION}]")
        self._min_pwm_duration_us.value = value

    @property
    def max_pwm_duration_us(self):
        return self._max_pwm_duration_us.value

    @max_pwm_duration_us.setter
    def max_pwm_duration_us(self, value: float):
        if not self.is_valid_pwm_duration_us(value):
            raise ValueError(
                f"Invalid Max PWM Duration <{value}>: must be on the interval [{self.MIN_PWM_DURATION},{self.MAX_PWM_DURATION}]")
        self._max_pwm_duration_us.value = value
