from .servo_config_widget import ServoConfigWidget
from .motor_status_widget import MotorStatusWidget
from textual import on
from model.models.driver_models.motor.servo.servo_model import ServoModel
from textual.containers import Horizontal, Vertical, Container
from .command_value_widget import CommandValueWidget
from functools import partial

# ServoStatusWidget class for displaying and managing the status of a servo motor.
# Inherits from MotorStatusWidget, allowing it to display servo-specific data and controls.
class ServoStatusWidget(MotorStatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: ServoModel, move: callable, write_config: callable, read_config: callable, **kwargs):
        """
        Initialize the ServoStatusWidget with a servo model and optional configurations.
        
        Args:
            model (ServoModel): The data model containing servo motor attributes.
            **kwargs: Additional keyword arguments for widget customization.
        """
        # Initialize the parent MotorStatusWidget with the servo model
        super().__init__(model, move, **kwargs)
        #self.command_position_widget.set_orientation(CommandValueWidget.Orientation.VERTICAL)
        self.config_widget = ServoConfigWidget(self.model, partial(read_config, motor_id=self.model.instance), partial(write_config, motor_id=self.model.instance))
        
        self.hidable_items.append(self.config_widget)

    def compose(self):
        """
        Compose the layout of the ServoStatusWidget.
        Includes the motor title, status, position, and jog controls and settings if the widget is selected.
        
        Yields:
            Static and other widgets: The title, motor status, position display, jog controls, command input, and settings.
        """
        if not self.selected:
            yield Container(
            self.compose_title(),  # Motor title
            self.status_display,  # Motor status display
            self.position_display,  # Motor position display
            classes="servo-status-widget-container")
        else:
            yield Horizontal(
            Container(
            self.compose_title(),  # Motor title
            self.status_display,  # Motor status display
            self.position_display,  # Motor position display
            classes="servo-status-widget-display-container"
            ),
            Container(
            # Display jog buttons and command position input if selected
            self.jog_buttons.compose_jog_buttons(),
            classes="servo-status-widget-container"
            ),
            classes="servo-status-widget-container"
            )
            yield self.command_position_widget
            yield self.config_widget
        """if not self.selected:
            yield Container(
            self.compose_title(),  # Motor title
            self.status_display,  # Motor status display
            self.position_display,  # Motor position display
            classes="servo-status-widget-container")
        else:
            yield Horizontal(
            Container(
            self.compose_title(),  # Motor title
            self.status_display,  # Motor status display
            self.position_display,  # Motor position display
            # Display jog buttons and command position input if selected
            self.jog_buttons.compose_jog_buttons(),
            classes="servo-status-widget-container"
            ),
            self.config_widget,
            classes="servo-status-widget-container"
            )
            yield self.command_position_widget"""