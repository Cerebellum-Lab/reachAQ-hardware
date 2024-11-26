from pyjerrycan import AnalogOut

from ....watchable import Watchable
from .....config import get_settings


class AnalogOutModel:
    """
    Model representing the analog out driver, managing attributes such as
    name, instance, minimum/maximum values, and the current output value in millivolts (mV).
    """

    SETTINGS_KEY = "Analog Out"

    def __init__(self, name: str, instance: int):
        """
        Initialize the AnalogOutModel with a name, instance, and value range.

        Args:
            name (str): The name of the analog out component.
            instance (int): The instance number of the analog out component.
        """

        settings = get_settings()
        self.MIN_OUTPUT_VALUE = settings[self.SETTINGS_KEY]["Min Output Value"]
        self.MAX_OUTPUT_VALUE = settings[self.SETTINGS_KEY]["Max Output Value"]

        # Read-only attributes
        self._name = name
        self._instance = instance

        # Read-write attribute for the current output value in mV
        self._value_mv = Watchable(0)

    @property
    def name(self) -> str:
        """str: Returns the name of the analog out component."""
        return self._name

    @property
    def instance(self) -> int:
        """int: Returns the instance number of the analog out component."""
        return self._instance

    @property
    def value_mv(self) -> int:
        """int: Returns the current output value in mV."""
        return self._value_mv

    def is_valid_value_mv(self, value_mv: int) -> bool:
        """bool: Returns True if the given value is a valid value_mv, else False"""
        try:
            return (
                value_mv >= self.MIN_OUTPUT_VALUE
                or int(value_mv) <= self.MAX_OUTPUT_VALUE
            )
        except:
            return False

    @value_mv.setter
    def value_mv(self, value: int):
        """Sets the current output value in mV and triggers reactive updates."""
        if not self.is_valid_value_mv(value):
            raise ValueError(
                f"Invalid Value (mV) <{value}>: must be on the interval [{self.MIN_OUTPUT_VALUE},{self.MAX_OUTPUT_VALUE}]"
            )
        self._value_mv.value = value

    def update_from_message(self, msg: AnalogOut):
        """
        Update the model's current value based on an incoming AnalogOut message.

        Args:
            msg (AnalogOut): The message containing the latest output value in mV.
        """
        # if msg.instance = self.model.instance **
        self.value_mv = msg.value_mv
