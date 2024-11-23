from .sensor_model import SensorModel
from pyjerrycan import LoadCellRead
from config import settings

class LoadCellSensorModel(SensorModel):
    """
    Model representing a load cell sensor, inheriting from SensorModel.
    Provides a method to update the sensor's value based on a LoadCellRead message.
    """
    SETTINGS_KEY = "Load Cell Sensor"

    def __init__(self, name: str, instance: int):
        """
        Initialize the LoadCellSensorModel with a name and instance ID.

        Args:
            name (str): The name of the load cell sensor.
            instance (int): The instance ID of the load cell sensor.
        """
        self.MAX_DATA_POINTS = settings["Load Cell Sensor"]["Graph"]["Max Data Points"]
        self.MIN_UPDATE_PERIOD = settings["Load Cell Sensor"]["Min Update Period"]
        
        super().__init__(name, instance)
    
    def update_from_message(self, msg: LoadCellRead):
        """
        Update the load cell sensor's value based on a LoadCellRead message.

        Args:
            msg (LoadCellRead): The message containing load cell data in millivolts.
        """
        self.sensor_value = msg.load_mv  # Set sensor value directly from message load in mV
