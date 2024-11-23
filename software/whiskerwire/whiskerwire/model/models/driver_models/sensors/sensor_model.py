from collections import deque
from time import time
from ....watchable import Watchable
from config import settings

class SensorModel:
    """
    Model representing a sensor, with attributes for name, instance, sensor value, and a data history.
    """
    SETTINGS_KEY: str
    
    MAX_DATA_POINTS: int
    MIN_UPDATE_PERIOD: float
    Y_MIN: float
    Y_MAX: float

    def __init__(self, name: str, instance: int):
        """
        Initialize the SensorModel with a name, instance, and a maximum number of data points to retain.

        Args:
            name (str): The name of the sensor.
            instance (int): The instance ID of the sensor.
            max_data_points (int): The maximum number of data points to retain for historical data (default is 30).
        """
        self.MAX_DATA_POINTS = settings[self.SETTINGS_KEY]["Graph"]["Max Data Points"]
        self.Y_MIN = settings[self.SETTINGS_KEY]["Graph"]["Y Min"]
        self.Y_MAX = settings[self.SETTINGS_KEY]["Graph"]["Y Max"]
        self.MIN_UPDATE_PERIOD = settings[self.SETTINGS_KEY]["Min Update Period"]

        # Read-only attributes
        self._name = name  # Sensor name
        self._instance = instance  # Unique identifier for this sensor instance
        
        # Read-write attributes
        self._sensor_value = Watchable(0, only_on_change=False, min_update_period=self.MIN_UPDATE_PERIOD)  # Latest sensor value
        self._sensor_data = deque(maxlen=self.MAX_DATA_POINTS)  # Circular buffer for historical data
        
        # Initialize sensor data with zeroed values
        for _ in range(self.MAX_DATA_POINTS):
            self._sensor_data.append(0)
        
        self._sensor_value.register_on_update(self.__update_sensor_data)
    
    def __update_sensor_data(self):
        self._sensor_data.append(self.sensor_value)
        
    @property
    def name(self) -> str:
        """str: Returns the name of the sensor."""
        return self._name

    @property
    def instance(self) -> int:
        """int: Returns the instance ID of the sensor."""
        return self._instance

    @property
    def sensor_value(self) -> int | float:
        """int | float: Returns the current sensor value."""
        return self._sensor_value.value

    @sensor_value.setter
    def sensor_value(self, value: int | float) -> None:
        """
        Set the current sensor value and add it to the historical data.

        Args:
            value (int | float): The new sensor value.
        """
        self._sensor_value.value = value

    @property
    def sensor_data(self) -> deque[int | float]:
        """deque[int | float]: Returns a deque containing historical sensor data."""
        return self._sensor_data
