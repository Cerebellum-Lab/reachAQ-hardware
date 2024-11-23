from .sensor_status_widget import SensorStatusWidget
from ..model.models.driver_models.sensors.temperature_sensor_model import (
    TemperatureSensorModel,
)


# HumidityStatusWidget class for displaying humidity levels as a percentage.
# Inherits from SensorStatusWidget and is designed specifically for humidity sensors.
class HumidityStatusWidget(SensorStatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: TemperatureSensorModel, **kwargs):
        """
        Initialize the HumidityStatusWidget with a model and optional settings.

        Args:
            model (TemperatureSensorModel): The data model containing humidity sensor properties.
            **kwargs: Additional keyword arguments for widget customization.
        """
        # Initialize the parent widget, setting the unit as "%" and the range defined in config.json
        super().__init__(model, "%", **kwargs)
