from textual.widgets import Static
from .status_widget import StatusWidget
from ..model.models.driver_models.gpio.gpio_model import GPIOModel
from ..model.models.driver_models.door_sensor.door_sensor_model import DoorSensorModel
import logging

logger = logging.getLogger("WhiskerWire")

# A mapping to convert numeric GPIO states (0 or 1) to descriptive labels ("LOW" or "HIGH")
STATE_MAP = ["LOW", "HIGH"]


# GPIOStatusWidget class for displaying the state of a GPIO pin.
# Inherits from StatusWidget and updates dynamically based on the GPIO model state.
class GPIOStatusWidget(StatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling this widget

    def __init__(self, model: GPIOModel, **kwargs):
        """
        Initialize the GPIOStatusWidget with a model and optional widget configuration.

        Args:
            model (GPIOModel): The data model containing the GPIO pin's properties and state.
            **kwargs: Additional keyword arguments for widget customization.
        """
        self.model = model
        # Initialize the parent widget with the GPIO name and instance index
        super().__init__(self.model.name, self.model.index, **kwargs)

        # Static display for showing the GPIO state (LOW or HIGH)
        self.state_display = Static(
            "State: LOW", id="gpio-state-display", classes="status-display"
        )

        # Register a callback to update the display when the model's state changes
        self.model._state.register_on_update(self.on_state_change)

    def on_state_change(self):
        """
        Callback function to update the GPIO state display when the `_state` attribute
        in the model changes.
        """
        self.state_display.update(f"State: {STATE_MAP[self.model.state]}")

    def compose(self):
        """
        Compose the layout of the GPIOStatusWidget.
        Includes the GPIO pin title and the current state display.

        Yields:
            Static: The title and state display widgets for the GPIO pin.
        """
        yield self.compose_title()  # GPIO pin title
        yield self.state_display  # Current state display
