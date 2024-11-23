from ....watchable import Watchable
from .....config import settings
import logging

logger = logging.getLogger("WhiskerWire")


class MotorModel:
    """
    Model representing a motor, including attributes for name, instance, commandable position range,
    status, and position.
    """
    SETTINGS_KEY: str

    MIN_POSITION: float
    MAX_POSITION: float
    MIN_VELOCITY: float
    MAX_VELOCITY: float
    MIN_ACCELERATION: float
    MAX_ACCELERATION: float

    SMALL_JOG_SIZE: float
    BIG_JOG_SIZE: float
    JOG_VELOCITY: float
    JOG_ACCELERATION: float

    def __init__(self, name: str, instance: int):
        """
        Initialize the MotorModel with a name, instance, and commandable position range.

        Args:
            name (str): The name of the motor.
            instance (int): The instance number of the motor.
        """
        if not self.SETTINGS_KEY:
            raise NotImplementedError(f"The class {self.__class__} does not define a SETTINGS_KEY")

        self.MIN_POSITION = settings[self.SETTINGS_KEY]["Min Position"]
        self.MAX_POSITION = settings[self.SETTINGS_KEY]["Max Position"]
        self.MIN_VELOCITY = settings[self.SETTINGS_KEY]["Min Velocity"]
        self.MAX_VELOCITY = settings[self.SETTINGS_KEY]["Max Velocity"]
        self.MIN_ACCELERATION = settings[self.SETTINGS_KEY]["Min Velocity"]
        self.MAX_ACCELERATION = settings[self.SETTINGS_KEY]["Max Velocity"]
        self.SMALL_JOG_SIZE = settings[self.SETTINGS_KEY]["Small Jog Size"]
        self.BIG_JOG_SIZE = settings[self.SETTINGS_KEY]["Big Jog Size"]
        self.JOG_VELOCITY = settings[self.SETTINGS_KEY]["Jog Velocity"]
        self.JOG_ACCELERATION = settings[self.SETTINGS_KEY]["Jog Acceleration"]

        # Read-only attributes
        self._name = name  # Name of the motor
        self._instance = instance  # Instance identifier for the motor

        # Read-write attributes
        self._status = Watchable(None)  # Current status of the motor
        self._position = Watchable(0.0)  # Current position of the motor (defaulted to -1)
        self._max_velocity = 0.0  # Currently configured max velocity of the motor
        self._max_acceleration = 0.0  # Currently configured max acceleration of the motor

    @property
    def name(self) -> str:
        """str: Returns the name of the motor."""
        return self._name

    @property
    def instance(self) -> int:
        """int: Returns the instance number of the motor."""
        return self._instance

    @property
    def status(self) -> int:
        """int: Returns the current status of the motor."""
        return self._status.value

    @status.setter
    def status(self, value: int):
        """
        Set the current status of the motor and trigger reactive updates if registered.

        Args:
            value (int): The new status of the motor.
        """
        self._status.value = value

    @property
    def position(self) -> float:
        """int: Returns the current position of the motor."""
        return self._position.value

    def is_valid_position(cls, position: float):
        try:
            return float(position) >= cls.MIN_POSITION and float(position) <= cls.MAX_POSITION
        except Exception as e:
            logger.error("HERE {str(e)}")
            return False

    @position.setter
    def position(self, value: float):
        """
        Set the current position of the motor and trigger reactive updates if registered.

        Args:
            value (int): The new position of the motor.
        """
        if not self.is_valid_position(value):
            raise ValueError(
                f"Invalid Position <{value}>: must be on the interval [{self.MIN_POSITION},{self.MAX_POSITION}]")
        self._position.value = value

    @property
    def max_velocity(self) -> float:
        return self._max_velocity

    def is_valid_velocity(self, velocity: float):
        try:
            return float(velocity) >= self.MIN_VELOCITY and float(velocity) <= self.MAX_VELOCITY
        except:
            return False

    @max_velocity.setter
    def max_velocity(self, value: float):
        if not self.is_valid_velocity(value):
            raise ValueError(
                f"Invalid Velocity <{value}>: must be on the interval [{self.MIN_VELOCITY},{self.MAX_VELOCITY}]")
        self._max_velocity = value

    @property
    def max_acceleration(self) -> float:
        return self._max_acceleration

    def is_valid_acceleration(self, acceleration: float):
        try:
            return float(acceleration) >= self.MIN_ACCELERATION and float(acceleration) <= self.MAX_ACCELERATION
        except:
            return False

    @max_acceleration.setter
    def max_acceleration(self, value: float):
        if not self.is_valid_acceleration(value):
            raise ValueError(
                f"Invalid Acceleration <{value}>: must be on the interval [{self.MIN_ACCELERATION},{self.MAX_ACCELERATION}]")
        self._max_acceleration = value
