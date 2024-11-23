from enum import Enum
from ....watchable import Watchable
from textual import reactive
class GPIODirection(Enum):
    """Enum representing the direction of a GPIO pin."""
    INPUT = 0
    OUTPUT = 1

class GPIOModel:
    """
    Model representing a GPIO (General Purpose Input/Output) pin, including its name, index, 
    direction, and current state.
    """

    def __init__(self, name: str, index: int, direction: GPIODirection):
        """
        Initialize the GPIOModel with a name, index, and direction.

        Args:
            name (str): The name of the GPIO pin.
            index (int): The index of the GPIO pin.
            direction (GPIODirection): The direction of the GPIO pin, either INPUT or OUTPUT.
        """
        super().__init__()

        # Read-only attributes
        self._name = name  # Name of the GPIO pin
        self._index = index  # Index of the GPIO pin
        self._direction = direction  # Direction of the GPIO pin (INPUT or OUTPUT)

        # Read-write attribute representing the current state of the GPIO pin
        self._state = Watchable(False)

    @property
    def name(self) -> str:
        """str: Returns the name of the GPIO pin."""
        return self._name

    @property
    def index(self) -> int:
        """int: Returns the index of the GPIO pin."""
        return self._index

    @property
    def direction(self) -> GPIODirection:
        """GPIODirection: Returns the direction of the GPIO pin (INPUT or OUTPUT)."""
        return self._direction

    @property
    def state(self) -> bool:
        """bool: Returns the current state of the GPIO pin (True for HIGH, False for LOW)."""
        return self._state.value

    @state.setter
    def state(self, value: bool):
        """
        Set the state of the GPIO pin and trigger reactive updates if registered.

        Args:
            value (bool): The new state of the GPIO pin (True for HIGH, False for LOW).
        """
        self._state.value = value