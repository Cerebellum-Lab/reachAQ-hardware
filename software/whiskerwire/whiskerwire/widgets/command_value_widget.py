from click import command
from textual.widget import Widget
from textual.widgets import Input
from textual.containers import Horizontal, Vertical
from textual.events import Click
from .glitchless_button import GlitchlessButton
from .util import to_valid_identifier
from enum import Enum
import logging
from textual.color import Color

logger = logging.getLogger("WhiskerWire")


# CommandValueWidget class for capturing user input commands and displaying input fields 
# with a submission button in the WhiskerWire application.
class CommandValueWidget(Widget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    class Orientation(Enum):
        HORIZONTAL = 0
        VERTICAL = 1

    def __init__(self, widget_name: str, placeholders: list[str], orientation=Orientation.HORIZONTAL, action=None,
                 **kwargs):
        """
        Initialize the CommandValueWidget with a widget name and input placeholders.
        
        Args:
            widget_name (str): The name of the widget, used for identification and border title.
            placeholders (list[str]): List of placeholder strings for each input field.
            **kwargs: Additional keyword arguments for widget configuration.
        """
        # Initialize the widget with a unique, valid ID derived from the widget name
        super().__init__(id=to_valid_identifier(widget_name), **kwargs)

        self.widget_name = widget_name
        self.border_title = self.widget_name  # Display name on the widget's border

        self.orientation = orientation

        # Button for submitting the input command
        self.command_button = GlitchlessButton("Send", id=f"{self.id}-button", classes="command-value-button",
                                               action=action)

        # Create input fields with placeholders, each assigned a unique ID
        self.command_inputs: list[Input] = [
            Input("", f"{placeholders[i]}", id=to_valid_identifier(f"{placeholders[i]}-input"),
                  classes="command-value-input")
            for i in range(len(placeholders))
        ]

    def compose(self):
        """
        Compose the layout of the CommandValueWidget.
        Includes a horizontal arrangement of the "Send" button and input fields.
        
        Yields:
            Horizontal: Container holding the button and input fields.
        """
        if self.orientation == self.Orientation.HORIZONTAL:
            yield Horizontal(
                self.command_button, *self.command_inputs, classes="command-value-container"
            )
        else:
            yield Vertical(
                *self.command_inputs, self.command_button, classes="command-value-container"
            )

    def get_values(self) -> dict[str, str]:
        """
        Retrieve the current values from all input fields in the widget.
        
        Returns:
            dict[str, str]: A dictionary where keys are input IDs, and values are the input values.
        """
        ids = [command_input.id for command_input in self.command_inputs]
        values = [command_input.value for command_input in self.command_inputs]
        return {key: value for key, value in zip(ids, values)}

    def reset(self) -> None:
        """
        Reset all input fields to empty strings, clearing any user-entered data.
        """
        for command_input in self.command_inputs:
            command_input.value = ""
            command_input.set_styles("background: grey 10%;")

    def set_orientation(self, orientation: Orientation):
        self.orientation = orientation

    def set_input_error_status(self, input_id: str, error_status: bool):
        for command_input in self.command_inputs:
            if command_input.id == input_id:
                input = command_input
                break
        if error_status:
            input.set_styles("background: red 20%;")
        else:
            input.set_styles("background: grey 10%;")
