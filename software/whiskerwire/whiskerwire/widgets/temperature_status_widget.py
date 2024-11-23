from .sensor_status_widget import SensorStatusWidget
from ..model.models.driver_models.sensors.temperature_sensor_model import TemperatureSensorModel


# TemperatureStatusWidget class for displaying temperature sensor readings.
# Inherits from SensorStatusWidget, configured specifically for temperature data.
class TemperatureStatusWidget(SensorStatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: TemperatureSensorModel, **kwargs):
        """
        Initialize the TemperatureStatusWidget with a temperature sensor model and optional configurations.
        
        Args:
            model (TemperatureSensorModel): The data model containing temperature sensor properties.
            **kwargs: Additional keyword arguments for widget customization.
        """
        # Initialize the parent widget, setting the unit as "°C" and range from 0.0 to 85.0°C
        super().__init__(model, "°C", **kwargs)
