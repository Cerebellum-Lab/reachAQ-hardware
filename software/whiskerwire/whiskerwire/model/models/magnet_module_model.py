from .module_model import ModuleModel
from .driver_models.gpio.gpios_model import GPIOSModel, GPIOModel
from .driver_models.gpio.gpio_model import GPIOModel, GPIODirection
from .driver_models.motor.servo.servos_model import ServosModel, ServoModel
from .driver_models.sensors.temperature_sensor_model import TemperatureSensorModel
from .driver_models.sensors.humidity_sensor_model import HumiditySensorModel
from .driver_models.sensors.pressure_sensor_model import PressureSensorModel
from .driver_models.sensors.load_cell_sensor_model import LoadCellSensorModel
from pyjerrycan import JerryCANMsg, JerryCANCmdType


class MagnetModuleModel(ModuleModel):
    """
    Model representing a magnet module, managing GPIO, servo, and sensor data.
    Inherits from ModuleModel and processes specific CAN messages for sensors.
    """

    DEVICE_TYPE = 0x01  # Device type identifier for the magnet module

    def __init__(self, can_address: int):
        """
        Initialize the MagnetModuleModel with a CAN address, setting up GPIO, servo, and sensor models.

        Args:
            can_address (int): The CAN address for the magnet module (0-3).
        """
        super().__init__(
            can_address,
            gpios=GPIOSModel(
                [
                    GPIOModel("CONT0", 4, GPIODirection.INPUT),
                    GPIOModel("CONT1", 5, GPIODirection.INPUT),
                ]
            ),
            servos=ServosModel([ServoModel("Servo 0", 0), ServoModel("Servo 1", 1)]),
        )

        # Initialize sensor models for temperature, humidity, pressure, and load cell
        self.temperature_sensor = TemperatureSensorModel("Temperature Sensor 0", 0)
        self.humidity_sensor = HumiditySensorModel("Humidity Sensor 0", 0)
        self.pressure_sensor = PressureSensorModel("Pressure Sensor 0", 0)
        self.load_cell_sensor = LoadCellSensorModel("Load Cell Sensor 0", 0)

    def process_message(self, msg: JerryCANMsg):
        """
        Process incoming JerryCAN messages to update module components, including GPIO, servos, and sensors.

        Args:
            msg (JerryCANMsg): The incoming CAN message containing component data.

        Returns:
            JerryCANCmdType: The type of the message processed.
        """
        # Process common messages through the parent class first
        type = super().process_message(msg)

        # Process specific sensor-related messages for the magnet module
        if type == JerryCANCmdType.TEMP_HUM_READ:
            # Update both temperature and humidity sensors from the TEMP_HUM_READ message
            self.temperature_sensor.update_from_message(msg.temp_hum_read)
            self.humidity_sensor.update_from_message(msg.temp_hum_read)
        elif type == JerryCANCmdType.PRESSURE_READ:
            # Update pressure sensor from the PRESSURE_READ message
            self.pressure_sensor.update_from_message(msg.pressure_read)
        elif type == JerryCANCmdType.LOAD_CELL_READ:
            # Update load cell sensor from the LOAD_CELL_READ message
            self.load_cell_sensor.update_from_message(msg.load_cell_read)

        return type
