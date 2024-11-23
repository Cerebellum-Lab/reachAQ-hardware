from .sensor_status_widget import SensorStatusWidget
from model.models.driver_models.sensors.pressure_sensor_model import PressureSensorModel
from textual.containers import Horizontal, Vertical
import logging
from .glitchless_button import GlitchlessButton
from functools import partial

logger = logging.getLogger("WhiskerWire")


# PressureStatusWidget class for displaying pressure sensor readings.
# Inherits from SensorStatusWidget and is tailored specifically for pressure data.
class PressureStatusWidget(SensorStatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget
    
    def __init__(self, model: PressureSensorModel, tare: callable, **kwargs):
        """
        Initialize the PressureStatusWidget with a pressure sensor model and optional configurations.
        
        Args:
            model (PressureSensorModel): The data model containing pressure sensor properties.
            **kwargs: Additional keyword arguments for widget customization.
        """
        # Initialize the parent widget, setting the unit as "mV" and the range defined in config.json
        super().__init__(model, "mV", **kwargs)

        self.tare_button = GlitchlessButton("Tare", classes="tare-button", action=partial(tare, instance=self.model.instance))
        self.tare_button.can_focus = False
        
        self._tare = tare
        
    def compose(self):
        """
        Compose the layout of the SensorStatusWidget, including the sensor name, value, and graph.
        
        Yields:
            Static and other widgets: The title, sensor value display, and graph widget.
        """

        yield Horizontal(
            Vertical(
                self.compose_title(),  # Sensor title
                self.sensor_value_display,  # Sensor value display
            ),
            self.tare_button, # Tare button
            classes="tare-stack"
        )

        # Ensure the plot refreshes to reflect the latest data
        self.graph.plot.refresh()  
        yield self.graph  # Add the graph widget
