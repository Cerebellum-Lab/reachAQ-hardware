from pyjerrycan import Tone
import logging
from ....watchable import Watchable
from config import settings

logger = logging.getLogger("WhiskerWire")

class ToneGeneratorModel:
    """
    Model representing a tone generator.
    Provides methods for setting frequency and duration within defined limits and for updating from CAN messages.
    """
    MIN_FREQUENCY = settings["Tone Generator"]["Min Frequency"]
    MAX_FREQUENCY = settings["Tone Generator"]["Max Frequency"]
    MIN_DURATION = settings["Tone Generator"]["Min Duration"]
    MAX_DURATION = settings["Tone Generator"]["Max Duration"]
    INTERPOLATION_INTERVAL = settings["Tone Generator"]["Interpolation Interval"]
    
    def __init__(self, name: str, instance: int):
        """
        Initialize the ToneGeneratorModel with a name and instance ID.

        Args:
            name (str): The name of the tone generator.
            instance (int): The instance ID of the tone generator.
        """
        # Read-only attributes
        self._name = name            # Name of the tone generator
        self._instance = instance    # Unique identifier for this instance
    
        # Read-write attributes
        self._frequency = Watchable(0)          # Frequency of the tone in Hz
        self._time_remaining = Watchable(0)     # Remaining time of the tone in milliseconds

    @property
    def name(self) -> str:
        """str: Returns the name of the tone generator."""
        return self._name

    @property
    def instance(self) -> int:
        """int: Returns the instance ID of the tone generator."""
        return self._instance

    @property
    def frequency(self) -> int:
        """int: The frequency of the tone in Hz."""
        return self._frequency.value

    @frequency.setter
    def frequency(self, value: int):
        """
        Set the frequency of the tone.

        Args:
            value (int): The frequency in Hz.
        """
        if not self.is_valid_frequency(value):
            raise ValueError(f"Invalid Duration/Time Remaining: must be on interval [{self.MIN_FREQUENCY},{self.MAX_FREQUENCY}]")    
        self._frequency.value = value

    @property
    def time_remaining(self) -> int:
        """int: The remaining time of the tone in milliseconds."""
        return self._time_remaining.value

    @time_remaining.setter
    def time_remaining(self, value: int):
        """
        Set the remaining duration of the tone in milliseconds.

        Args:
            value (int): The remaining time in milliseconds.
        """
        if not self.is_valid_duration(value):
            raise ValueError(f"Invalid Duration/Time Remaining <{value}>: must be on interval [{self.MIN_DURATION},{self.MAX_DURATION}]")    
        self._time_remaining.value = value

    def is_valid_frequency(self, frequency: int) -> bool:
        try:
            return int(frequency) >= self.MIN_FREQUENCY and int(frequency) <= self.MAX_FREQUENCY
        except:
            return False

    def is_valid_duration(self, duration: int) -> bool:
        try:
            return int(duration) >= self.MIN_DURATION and int(duration) <= self.MAX_DURATION
        except:
            return False

    def update_from_message(self, msg: Tone):
        """
        Update the tone generator's frequency and remaining time based on a Tone message.

        Args:
            msg (Tone): The message containing frequency and remaining duration data.
        """
        self.frequency = msg.frequency_hz
        self.time_remaining = msg.duration_ms
