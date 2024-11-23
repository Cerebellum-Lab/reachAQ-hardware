from textual.widgets import Static, Input
from .status_widget import StatusWidget
from .command_value_widget import CommandValueWidget
from .jog_buttons import JogButtons
from textual import on
from .util import represents_int
from model.models.driver_models.motor.motor_model import MotorModel
from functools import partial
from pyjerrycan import AbsOrRel

# MotorStatusWidget class for displaying and controlling a motor's status, position, and movement.
# Inherits from StatusWidget, allowing interaction with motor control elements like jog buttons and command inputs.
class MotorStatusWidget(StatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: MotorModel, move: callable, **kwargs):
        """
        Initialize the MotorStatusWidget with a motor model and optional configurations.
        
        Args:
            model (MotorModel): The data model containing motor attributes, including position and status.
            **kwargs: Additional keyword arguments for widget customization.
        """
        
        self.model = model
        # Initialize the parent widget with motor name and instance number
        super().__init__(self.model.name, self.model.instance, **kwargs)

        self.move = partial(move, motor_id=self.model.instance, abs_or_rel=AbsOrRel.ABSOLUTE)

        # Static displays for showing motor status and position
        self.status_display = Static(id="motor-status-display", classes="status-display")
        self.position_display = Static(id="motor-position-display", classes="status-display")
        
        # CommandValueWidget and JogButtons for controlling motor position
        self.command_position_widget = CommandValueWidget("Command Position", ["position", "velocity", "acceleration"], action=self.command_position)
        self.jog_buttons = JogButtons(f"{self.model.name} Jog Buttons", move=partial(move, motor_id=self.model.instance), small_jog_size=self.model.SMALL_JOG_SIZE, big_jog_size=self.model.BIG_JOG_SIZE, max_jog_velocity=self.model.JOG_VELOCITY, max_jog_acceleration=self.model.JOG_ACCELERATION)
        
        # Add widgets to hidable items, shown only when the motor widget is selected
        self.hidable_items.append(self.jog_buttons)
        self.hidable_items.append(self.command_position_widget)
        
        self.model._status.register_on_update(self.on_status_change)
        self.model._position.register_on_update(self.on_position_change)

    def on_status_change(self):
        self.status_display.update(f"Status: {str(self.model.status)}")
        
    def on_position_change(self):
        self.position_display.update(f"Position: {self.model.position:.3f}")
        
    def command_position(self):
        kinematic_parameters = self.command_position_widget.get_values()
        valid = True
        
        # Attempt to retrieve and validate the target position from input
        try:
            position = float(kinematic_parameters["position-input"])
            if self.model.is_valid_position(position):
                self.command_position_widget.set_input_error_status("position-input", False)
            else:
                self.command_position_widget.set_input_error_status("position-input", True)
                valid = False
        except:
            self.command_position_widget.set_input_error_status("position-input", True)
            valid = False

        try:
            velocity = float(kinematic_parameters["velocity-input"])
            if self.model.is_valid_velocity(velocity):
                self.command_position_widget.set_input_error_status("velocity-input", False)
            else:
                self.command_position_widget.set_input_error_status("velocity-input", True)
                valid = False
        except:
            self.command_position_widget.set_input_error_status("velocity-input", True)
            valid = False

        try:
            acceleration = float(kinematic_parameters["acceleration-input"])
            if self.model.is_valid_acceleration(acceleration):
                self.command_position_widget.set_input_error_status("acceleration-input", False)
            else:
                self.command_position_widget.set_input_error_status("acceleration-input", True)
                valid = False
        except:
            self.command_position_widget.set_input_error_status("acceleration-input", True)
            valid = False

        
        # Execute absolute move command
        if valid:
            self.move(position=position, max_velocity=velocity, max_acceleration=acceleration)
            self.command_position_widget.reset()  # Reset the command widget input

    def compose(self):
        """
        Compose the layout of the MotorStatusWidget.
        Includes the motor title, status, position, and jog controls if the widget is selected.
        
        Yields:
            Static and other widgets: The title, motor status, position display, jog controls, and command input.
        """
        yield self.compose_title()  # Motor title
        yield self.status_display  # Motor status display
        yield self.position_display  # Motor position display
        if self.selected:
            # Display jog buttons and command position input if selected
            yield self.jog_buttons.compose_jog_buttons()
            yield self.command_position_widget

    def on_mount(self):
        """
        Refresh the display on widget mount to show the current motor status and position.
        """
        self.status_display.update(f"Status: {str(self.model.status)}")
        self.position_display.update(f"Position: {self.model.position:.3f}")  # Update position display
