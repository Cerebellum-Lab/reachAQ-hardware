from .sensor_model import SensorModel
from pyjerrycan import TempHumRead
from config import settings

class TemperatureSensorModel(SensorModel):
    """
    Model representing a temperature sensor, inheriting from SensorModel.
    Provides a method to update the sensor's value based on a TempHumRead message.
    """
    SETTINGS_KEY = "Temperature Sensor"

    def __init__(self, name: str, instance: int):
        """
        Initialize the TemperatureSensorModel with a name and instance ID.

        Args:
            name (str): The name of the temperature sensor.
            instance (int): The instance ID of the temperature sensor.
        """
        self.MAX_DATA_POINTS = settings["Temperature Sensor"]["Graph"]["Max Data Points"]
        self.MIN_UPDATE_PERIOD = settings["Temperature Sensor"]["Min Update Period"]
        
        super().__init__(name, instance)

    def update_from_message(self, msg: TempHumRead):
        """
        Update the temperature sensor's value based on a TempHumRead message.

        Args:
            msg (TempHumRead): The message containing temperature data.
        """
        self.sensor_value = msg.temperature / 100.0  # Convert to degrees - data is scaled by 100
