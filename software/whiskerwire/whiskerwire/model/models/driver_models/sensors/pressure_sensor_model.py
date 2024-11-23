from pyjerrycan import PressureRead

from .sensor_model import SensorModel
from .....config import get_settings


class PressureSensorModel(SensorModel):
    """
    Model representing a pressure sensor, inheriting from SensorModel.
    Provides a method to update the sensor's value based on a PressureRead message.
    """

    SETTINGS_KEY = "Pressure Sensor"

    def __init__(self, name: str, instance: int):
        """
        Initialize the PressureSensorModel with a name and instance ID.

        Args:
            name (str): The name of the pressure sensor.
            instance (int): The instance ID of the pressure sensor.
        """
        settings = get_settings()
        self.MAX_DATA_POINTS = settings["Pressure Sensor"]["Graph"]["Max Data Points"]
        self.MIN_UPDATE_PERIOD = settings["Pressure Sensor"]["Min Update Period"]

        super().__init__(name, instance)

    def update_from_message(self, msg: PressureRead):
        """
        Update the pressure sensor's value based on a PressureRead message.

        Args:
            msg (PressureRead): The message containing pressure data in millivolts.
        """
        self.sensor_value = (
            msg.pressure_mv
        )  # Set sensor value directly from message pressure in mV
