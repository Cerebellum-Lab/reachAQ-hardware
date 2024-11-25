from .gpio_model import GPIOModel
from .....utils import get_logger
from pyjerrycan import GPIORead
import sys

logger = get_logger()


class GPIOSModel:
    """
    Model representing a collection of GPIO pins, providing methods to access individual GPIOs by name or index
    and to update GPIO states based on incoming messages.
    """

    def __init__(self, gpios: list[GPIOModel]):
        """
        Initialize the GPIOSModel with a list of GPIO models, ensuring no conflicts in names or indices.

        Args:
            gpios (list[GPIOModel]): A list of GPIOModel instances representing individual GPIO pins.

        Raises:
            SystemExit: If duplicate GPIO names or indices are found.
        """
        # Ensure that no conflicts exist between GPIO names and indices
        names = [gpio.name for gpio in gpios]
        indices = [gpio.index for gpio in gpios]

        if len(names) != len(set(names)):
            logger.fatal("Duplicate GPIO names found in gpios")
            sys.exit(1)

        if len(indices) != len(set(indices)):
            logger.fatal("Duplicate GPIO indices found in gpios")
            sys.exit(1)

        # Store GPIOs in a dictionary by their index for easy access
        self.gpios: dict[int, GPIOModel] = {gpio.index: gpio for gpio in gpios}

    @property
    def instance(self) -> int:
        """int: Returns the instance identifier for the GPIO collection, assumed to be 0."""
        return 0

    def get_gpio_by_index(self, index: int) -> GPIOModel | None:
        """
        Retrieve a GPIOModel by its index.

        Args:
            index (int): The index of the GPIO.

        Returns:
            GPIOModel | None: The GPIO model if found, otherwise None.
        """
        try:
            return self.gpios[index]
        except KeyError:
            logger.error(
                f"Failed to get GPIO with index <{index}> - a GPIO with the specified index does not exist"
            )
            return None

    def get_gpio_by_name(self, name: str) -> GPIOModel | None:
        """
        Retrieve a GPIOModel by its name.

        Args:
            name (str): The name of the GPIO.

        Returns:
            GPIOModel | None: The GPIO model if found, otherwise None.
        """
        for gpio in self.gpios.values():
            if gpio.name == name:
                return gpio
        logger.error(
            f"Failed to get GPIO with name <'{name}'> - a GPIO with the specified name does not exist"
        )
        return None

    def get_gpio(self, specifier: int | str) -> GPIOModel | None:
        """
        Retrieve a GPIOModel by either index or name.

        Args:
            specifier (int | str): The index or name of the GPIO.

        Returns:
            GPIOModel | None: The GPIO model if found, otherwise None.

        Raises:
            TypeError: If the specifier is not an int or str.
        """
        if isinstance(specifier, int):
            return self.get_gpio_by_index(specifier)
        elif isinstance(specifier, str):
            return self.get_gpio_by_name(specifier)

        raise TypeError("Failed to get GPIO - specifier must be an int or str")

    def get_state(self, specifier: int | str) -> bool | None:
        """
        Get the state of a GPIO pin.

        Args:
            specifier (int | str): The index or name of the GPIO.

        Returns:
            bool | None: The state of the GPIO if found, otherwise None.
        """
        gpio = self.get_gpio(specifier)
        if gpio is None:
            logger.error(f"Failed to get state of GPIO with specifier <{specifier}>")
            return None
        return gpio.state

    def set_state(self, specifier: int | str, state: bool) -> None:
        """
        Set the state of a GPIO pin.

        Args:
            specifier (int | str): The index or name of the GPIO.
            state (bool): The new state to set for the GPIO.
        """
        gpio = self.get_gpio(specifier)
        if gpio is None:
            logger.error(f"Failed to set state of GPIO with specifier <{specifier}>")
        else:
            gpio.state = state

    def update_from_message(self, msg: GPIORead):
        """
        Update the states of all GPIO pins based on a GPIORead message.

        Args:
            msg (GPIORead): The message containing state information for each GPIO pin.
        """
        # `instance` is unused as it's assumed only one instance of generic GPIOs exists
        state = msg.state

        # Update each GPIO's state based on the message data
        for index, gpio in self.gpios.items():
            gpio.state = bool((state >> index) & 0b1)
