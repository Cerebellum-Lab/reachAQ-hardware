from .sensor_status_widget import SensorStatusWidget
from model.models.driver_models.sensors.load_cell_sensor_model import LoadCellSensorModel
from textual.containers import Horizontal, Vertical
import logging
from .glitchless_button import GlitchlessButton
from functools import partial

logger = logging.getLogger("WhiskerWire")

# LoadCellStatusWidget class for displaying load cell sensor readings.
# Inherits from SensorStatusWidget and is designed specifically for load cell data.
class LoadCellStatusWidget(SensorStatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: LoadCellSensorModel, tare: callable, **kwargs):
        """
        Initialize the LoadCellStatusWidget with a model and optional configuration.
        
        Args:
            model (LoadCellSensorModel): The data model containing the load cell sensor properties.
            **kwargs: Additional keyword arguments for widget customization.
        """
        # Initialize the parent widget, setting the unit as "mV" and the range defined in config.jsonc
        super().__init__(model, "mV", decimal_digits=2, **kwargs)
        
        self.tare_button = GlitchlessButton("Tare", classes="tare-button", action=partial(tare, instance=self.model.instance))
        self.tare_button.can_focus = False

    def compose(self):
        """
        Compose the layout of the SensorStatusWidget, including the sensor name, value, and graph.
        
        Yields:
            Static and other widgets: The title, sensor value display, and graph widget.
        """
        # Ensure the plot refreshes to reflect the latest data
        self.graph.plot.refresh()  
        yield Horizontal(
            Vertical(
                self.compose_title(),  # Sensor title
                self.sensor_value_display,  # Sensor value display
            ),
            self.tare_button, # Tare button
            classes="tare-stack"
        )
        yield self.graph  # Add the graph widget

        