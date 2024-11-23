import logging

from ..motor_model import MotorModel
from .....watchable import Watchable
from ......config import get_settings

logger = logging.getLogger("WhiskerWire")


class StepperModel(MotorModel):
    """
    Model representing a stepper motor, inheriting from MotorModel. 
    Includes additional properties for stepper-specific attributes such as the limit switch state.
    """
    SETTINGS_KEY = "Stepper Motor"

    def __init__(self, name: str, instance: int):
        """
        Initialize the StepperModel with a name, instance, and predefined position limits.

        Args:
            name (str): The name of the stepper motor.
            instance (int): The instance ID of the stepper motor.
        """
        super().__init__(name, instance)  # , self.SETTINGS_KEY)

        settings = get_settings()
        self.MIN_STEPS_PER_REVOLUTION = settings[self.SETTINGS_KEY]["Min Steps per Revolution"]
        self.MAX_STEPS_PER_REVOLUTION = settings[self.SETTINGS_KEY]["Max Steps per Revolution"]

        self._limit_switch = Watchable(False)  # Indicates whether the limit switch is activated
        self._homing_status = Watchable(None)
        self._microsteps = Watchable(0)  # Might want to set only_on_change=False
        self._steps_per_revolution = Watchable(0)  # Might want to set only_on_change=False

    @property
    def limit_switch(self) -> bool:
        """bool: Returns the state of the limit switch (True if active, False if inactive)."""
        return self._limit_switch.value

    @limit_switch.setter
    def limit_switch(self, value: bool):
        """
        Set the state of the limit switch.

        Args:
            value (bool): The new state of the limit switch (True for active, False for inactive).
        """
        self._limit_switch.value = value

    @property
    def homing_status(self) -> int:
        """bool: Returns the homing status."""
        return self._homing_status.value

    @homing_status.setter
    def homing_status(self, value: int):
        """
        Set the state of the limit switch.

        Args:
            value (bool): The new homing status.
        """
        self._homing_status.value = value

    @property
    def microsteps(self):
        return self._microsteps.value

    def is_valid_microsteps(self, value: int):
        return (value & (value - 1) == 0) and value != 0

    @microsteps.setter
    def microsteps(self, value: int):
        # I'm not adding a check for validity here
        # since it is only set by a Select widget
        # or a jerryCAN CfgResponse message
        self._microsteps.value = value

    @property
    def steps_per_revolution(self):
        return self._steps_per_revolution.value

    def is_valid_steps_per_revolution(self, steps_per_revolution: float):
        try:
            return float(steps_per_revolution) >= self.MIN_STEPS_PER_REVOLUTION and float(
                steps_per_revolution) <= self.MAX_STEPS_PER_REVOLUTION
        except:
            return False

    @steps_per_revolution.setter
    def steps_per_revolution(self, value: float):
        if not self.is_valid_steps_per_revolution(value):
            raise ValueError(
                f"Invalid Steps per Revolution <{value}>: must be on the interval [{self.MIN_STEPS_PER_REVOLUTION},{self.MAX_STEPS_PER_REVOLUTION}]")
        self._steps_per_revolution.value = value
