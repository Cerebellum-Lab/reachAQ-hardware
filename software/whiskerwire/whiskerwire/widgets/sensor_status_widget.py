from textual.widgets import Static
from .status_widget import StatusWidget
from .graph_widget import GraphWidget
from ..model.models.driver_models.sensors.sensor_model import SensorModel


# Base class for sensor status widgets, providing a display for sensor values and a graph for data visualization.
# Inherits from StatusWidget and is designed to work with any sensor model.
class SensorStatusWidget(StatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: SensorModel, unit: str = "", decimal_digits: int | None = None, **kwargs):
        """
        Initialize the SensorStatusWidget with a sensor model, unit, and optional graph bounds.
        
        Args:
            model (SensorModel): The data model containing sensor attributes and data.
            unit (str): The unit of measurement for the sensor (e.g., "°C", "mV").
            y_min (float | None): Optional minimum value for the graph's y-axis.
            y_max (float | None): Optional maximum value for the graph's y-axis.
            **kwargs: Additional keyword arguments for widget customization.
        """
        self.model = model
        super().__init__(self.model.name, self.model.instance, **kwargs)
        self.unit = unit  # Unit of measurement for the sensor
        self.decimal_digits = decimal_digits  # Number of decimal digits to display

        # Static display for showing the current sensor value
        self.sensor_value_display = Static(
            f"{self.widget_name}: {self.model.sensor_value}{self.unit}",
            id=f"{self.id}-display",
            classes="status-display"
        )

        # GraphWidget for visualizing sensor data over time, with optional y-axis bounds
        self.graph = GraphWidget(self.model)

        # Register a callback to update the sensor value display when the value changes
        self.model._sensor_value.register_on_update(self.on_sensor_data_change)

    def on_sensor_data_change(self):
        """
        Callback to update the sensor value display when the sensor's value changes.
        Refreshes the display to reflect the latest data.
        """
        if self.decimal_digits is None:
            self.sensor_value_display.update(f"{self.widget_name}: {self.model.sensor_value}{self.unit}")
        else:
            self.sensor_value_display.update(
                f"{self.widget_name}: {self.model.sensor_value:.{self.decimal_digits}f}{self.unit}"
            )
        self.refresh(recompose=True)

    def compose(self):
        """
        Compose the layout of the SensorStatusWidget, including the sensor name, value, and graph.
        
        Yields:
            Static and other widgets: The title, sensor value display, and graph widget.
        """
        yield self.compose_title()  # Sensor title
        yield self.sensor_value_display  # Sensor value display

        # Ensure the plot refreshes to reflect the latest data
        self.graph.plot.refresh()
        yield self.graph  # Add the graph widget
