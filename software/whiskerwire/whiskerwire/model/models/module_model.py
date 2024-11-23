from .driver_models.gpio.gpios_model import GPIOSModel
from .driver_models.motor.servo.servos_model import ServosModel
from pyjerrycan import JerryCANMsg, JerryCANCmdType, JerryCANCfgMsg
from ..watchable import Watchable


class ModuleModel:
    """
    Base model for a module with GPIO and servo components, supporting CAN communication.
    """

    DEVICE_TYPE = None  # Define in subclasses for specific module types

    def __init__(self, can_address: int, gpios: GPIOSModel, servos: ServosModel):
        """
        Initialize the ModuleModel with a CAN address, GPIO model, and servo model.

        Args:
            can_address (int): The CAN address for the module (0-3).
            gpios (GPIOSModel): An instance of GPIOSModel representing GPIO states.
            servos (ServosModel): An instance of ServosModel representing servo states.

        Raises:
            ValueError: If the CAN address is not in the range [0, 3].
        """
        super().__init__()

        # Validate CAN address range
        if not (0 <= can_address <= 3):
            raise ValueError(
                f"CAN address must be in the range [0, 3] - received {can_address}"
            )

        self._can_address = can_address  # CAN address for the module
        self.gpios = gpios  # GPIO model instance for handling GPIO states
        self.servos = servos  # Servo model instance for handling servo states

        # This is not used right now, nor is the high-level estop status variable, since
        # E-Stop state is not actually be maintained and transmitted through the status message
        self._estop_status = Watchable(False)  # Initial emergency stop (E-stop) status

    @property
    def can_address(self) -> int:
        """int: Returns the CAN address for the module."""
        return self._can_address

    @property
    def dst_id(self) -> int:
        """int: Compute and return the destination ID by combining device type and CAN address."""
        return (self.DEVICE_TYPE << 2) | self.can_address

    @property
    def estop_status(self) -> bool:
        """bool: Returns the current emergency stop (E-stop) status."""
        return self._estop_status.value

    @estop_status.setter
    def estop_status(self, value: bool):
        """Set the emergency stop (E-stop) status."""
        self._estop_status.value = value

    def process_message(self, msg: JerryCANMsg) -> JerryCANCmdType:
        """
        Process incoming JerryCAN messages to update GPIO and servo states.

        Args:
            msg (JerryCANMsg): The incoming message from the CAN bus.

        Returns:
            JerryCANCmdType: The type of the message processed.
        """
        if msg.type == JerryCANCmdType.GPIO_READ:
            # Update GPIO states from the message data
            self.gpios.update_from_message(msg.gpio_read)
        elif msg.type == JerryCANCmdType.STATUS:
            # Update servo statuses from the status message
            # Is now handled by servo status message
            # self.servos.update_from_status_message(msg.status)
            pass  # FIXME: Update once new status message has been defined
        elif msg.type == JerryCANCmdType.SERVO_STATUS:
            # Update servo status from the servo status message
            self.servos.update_from_servo_status_message(msg.servo_status)
        elif msg.type == JerryCANCmdType.CFG_RESPONSE:
            # Update configuration settings for servos from the configuration response
            cfg_response = msg.cfg_response
            if cfg_response.type == JerryCANCfgMsg.Type.SERVO:
                self.servos.update_from_cfg_message(cfg_response.servo)

        return msg.type
