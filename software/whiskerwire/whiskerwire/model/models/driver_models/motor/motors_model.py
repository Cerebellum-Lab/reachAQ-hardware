from .motor_model import MotorModel
import logging
import sys

logger = logging.getLogger("WhiskerWire")

class MotorsModel:
    """
    Model representing a collection of motors, providing methods to access individual motors by name or instance,
    and to get or set their statuses and positions.
    """

    def __init__(self, motors: list[MotorModel]):
        """
        Initialize the MotorsModel with a list of MotorModel instances, ensuring no conflicts in names or instances.

        Args:
            motors (list[MotorModel]): A list of MotorModel instances representing individual motors.

        Raises:
            SystemExit: If duplicate motor names or instances are found.
        """
        # Ensure that no conflicts exist between motor names and instances
        names = [motor.name for motor in motors]
        instances = [motor.instance for motor in motors]
        
        if len(names) != len(set(names)):
            logger.fatal("Duplicate motor names found in motors")
            sys.exit(1)
        
        if len(instances) != len(set(instances)):
            logger.fatal("Duplicate motor instances found in motors")
            sys.exit(1)

        # Store motors in a dictionary by their instance ID for easy access
        self.motors: dict[int, MotorModel] = {motor.instance: motor for motor in motors}

    def get_motor_by_instance(self, instance: int) -> MotorModel | None:
        """
        Retrieve a MotorModel by its instance ID.

        Args:
            instance (int): The instance ID of the motor.

        Returns:
            MotorModel | None: The motor model if found, otherwise None.
        """
        try:
            return self.motors[instance]
        except KeyError:
            logger.error(f"Failed to get motor with instance <{instance}> - a motor with the specified instance does not exist")
            return None

    def get_motor_by_name(self, name: str) -> MotorModel | None:
        """
        Retrieve a MotorModel by its name.

        Args:
            name (str): The name of the motor.

        Returns:
            MotorModel | None: The motor model if found, otherwise None.
        """
        for motor in self.motors.values():
            if motor.name == name:
                return motor
        logger.error(f"Failed to get motor with name <'{name}'> - a motor with the specified name does not exist")
        return None

    def get_motor(self, specifier: int | str) -> MotorModel | None:
        """
        Retrieve a MotorModel by either instance ID or name.

        Args:
            specifier (int | str): The instance ID or name of the motor.

        Returns:
            MotorModel | None: The motor model if found, otherwise None.

        Raises:
            TypeError: If the specifier is not an int or str.
        """
        if isinstance(specifier, int):
            return self.get_motor_by_instance(specifier)
        elif isinstance(specifier, str):
            return self.get_motor_by_name(specifier)
        
        raise TypeError("Failed to get motor - specifier must be an int or str")

    def get_status(self, specifier: int | str) -> int | None:
        """
        Get the status of a motor.

        Args:
            specifier (int | str): The instance ID or name of the motor.

        Returns:
            int | None: The status of the motor if found, otherwise None.
        """
        motor = self.get_motor(specifier)
        if motor is None:
            logger.error(f"Failed to get status of motor with specifier <{specifier}>")
            return None
        return motor.status

    def set_status(self, specifier: int | str, status: int) -> None:
        """
        Set the status of a motor.

        Args:
            specifier (int | str): The instance ID or name of the motor.
            status (int): The new status to set for the motor.
        """
        motor = self.get_motor(specifier)
        if motor is None:
            logger.error(f"Failed to set status of motor with specifier <{specifier}>")
        else:
            motor.status = status

    def get_position(self, specifier: int | str) -> int | None:
        """
        Get the position of a motor.

        Args:
            specifier (int | str): The instance ID or name of the motor.

        Returns:
            int | None: The position of the motor if found, otherwise None.
        """
        motor = self.get_motor(specifier)
        if motor is None:
            logger.error(f"Failed to get position of motor with specifier <{specifier}>")
            return None
        return motor.position

    def set_position(self, specifier: int | str, position: int) -> None:
        """
        Set the position of a motor.

        Args:
            specifier (int | str): The instance ID or name of the motor.
            position (int): The new position to set for the motor.
        """
        motor = self.get_motor(specifier)
        if motor is None:
            logger.error(f"Failed to set position of motor with specifier <{specifier}>")
        else:
            motor.position = position
