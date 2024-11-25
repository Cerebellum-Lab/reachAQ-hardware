from textual.widgets import Static
from .status_widget import StatusWidget
from .command_value_widget import CommandValueWidget
from ..model.models.driver_models.analog_out.analog_out_model import AnalogOutModel
from ..utils import get_logger
from functools import partial

logger = get_logger()


# AnalogOutWidget class inheriting from StatusWidget, specialized for displaying
# and interacting with analog out values in the WhiskerWire application.
class AnalogOutWidget(StatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: AnalogOutModel, analog_write: callable, **kwargs):
        """
        Initialize the AnalogOutWidget with a model and optional keyword arguments.

        Args:
            model (AnalogOutModel): The data model containing the analog out attributes.
            **kwargs: Additional keyword arguments for widget configuration.
        """
        self.model = model
        # Initialize parent with the model's name and instance number
        super().__init__(self.model.name, self.model.instance, **kwargs)

        self.analog_write = partial(analog_write, instance=self.model.instance)

        # Widget displaying the current analog value
        self.value_display = Static(
            f"Value: {self.model.value_mv}mv",
            id="analog-status-value-display",
            classes="status-display",
        )

        # CommandValueWidget for adjusting the analog value
        self.command_value_widget = CommandValueWidget(
            "Command Value", ["value"], action=self.command_analog_out
        )

        # Add the command widget to the list of hidable items for conditional display
        self.hidable_items.append(self.command_value_widget)

        # Register a callback to update the displayed value when model's value changes
        self.model._value_mv.register_on_update(self.on_value_mv_change)

    def command_analog_out(self):
        command_value_dict = self.command_value_widget.get_values()

        # Validate and assign the value from the command input
        try:
            value_mv = int(command_value_dict["value-input"])
            if self.model.is_valid_value_mv(value_mv):
                self.command_value_widget.set_input_error_status("value-input", False)
                self.analog_write(value_mv=value_mv)  # Send the message
                self.command_value_widget.reset()  # Reset the input field after sending
        except:
            self.command_value_widget.set_input_error_status("value-input", True)

    def on_value_mv_change(self):
        """
        Callback function to update the value display when `value_mv` in the model changes.
        """
        self.value_display.update(
            f"Value: {self.model.value_mv}mv"
        )  # Update value display

    def compose(self):
        """
        Compose the layout of the AnalogOutWidget.
        This includes the analog out title, value display, and command input (if selected).

        Yields:
            Static: The title and value display widgets, and the command input if selected.
        """
        yield self.compose_title()  # Analog status title
        yield self.value_display  # Value display

        # Display the command widget if the widget is currently selected
        if self.selected:
            yield self.command_value_widget
