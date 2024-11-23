from pyjerrycan import TempHumRead

from .sensor_model import SensorModel


class HumiditySensorModel(SensorModel):
    """
    Model representing a humidity sensor, inheriting from SensorModel.
    Provides a method to update the sensor's value based on a TempHumRead message.
    """

    SETTINGS_KEY = "Humidity Sensor"

    def __init__(self, name: str, instance: int):
        """
        Initialize the HumiditySensorModel with a name and instance ID.

        Args:
            name (str): The name of the humidity sensor.
            instance (int): The instance ID of the humidity sensor.
        """
        super().__init__(name, instance)

    def update_from_message(self, msg: TempHumRead):
        """
        Update the humidity sensor's value based on a TempHumRead message.

        Args:
            msg (TempHumRead): The message containing humidity data.
        """
        self.sensor_value = msg.humidity / 100.0  # Convert to percentage
