from textual.widgets import Static, Collapsible, Input
from textual.containers import Horizontal, Vertical, Container
from .motor_status_widget import MotorStatusWidget
from model.models.driver_models.motor.stepper.stepper_model import StepperModel
from textual import on
from .command_value_widget import CommandValueWidget
from .glitchless_button import GlitchlessButton
from .stepper_config_widget import StepperConfigWidget
from functools import partial

DEFAULT_STEPPER_MAX_VELOCITY = 100
DEFAULT_STEPPER_MAX_ACCELERATION = 100
DEFAULT_STEPPER_JOG_MAX_VELOCITY = 100
DEFAULT_STEPPER_JOG_MAX_ACCELERATION = 100

# StepperStatusWidget class for displaying and managing the status of a stepper motor.
# Inherits from MotorStatusWidget, adding specific displays for limit switches and homing status.
class StepperStatusWidget(MotorStatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget
    
    def __init__(self, model: StepperModel, move: callable, home: callable, read_config: callable, write_config: callable, **kwargs):
        """
        Initialize the StepperStatusWidget with a stepper model and optional configurations.
        
        Args:
            model (StepperModel): The data model containing stepper motor attributes.
            **kwargs: Additional keyword arguments for widget customization.
        """
        super().__init__(model, move, *kwargs)

        # Static displays for limit switch status and homing status
        self.limit_switch_display = Static(id="stepper-limit-switch-display", classes="status-display")
        self.homing_status_display = Static(id="stepper-homing-status-display", classes="status-display")

        # Button to trigger homing operation
        self.home_negative_button = GlitchlessButton("Home-", classes="home-button", id="negative-home-button", action=partial(home, motor_id=self.model.instance, forward=False))
        self.home_positive_button = GlitchlessButton("Home+", classes="home-button", id="positive-home-button", action=partial(home, motor_id=self.model.instance, forward=True))

        self.config_widget = StepperConfigWidget(self.model, partial(read_config, motor_id=self.model.instance), partial(write_config, motor_id=self.model.instance))

        self.hidable_items.append(self.config_widget)
        
        self.model._limit_switch.register_on_update(self.on_limit_switch_change)
        self.model._homing_status.register_on_update(self.on_homing_status_change)

    def on_limit_switch_change(self):
        # Limit switches are pulled high, so low value means it has been tripped
        if self.model.limit_switch:
            self.limit_switch_display.update("Limit Switch: [green]Inactive[/green]")
        else:
            self.limit_switch_display.update("Limit Switch: [red]Active[/red]")
    
    def on_homing_status_change(self):
        self.homing_status_display.update(f"Homing Status: {self.model.homing_status}")

    def compose(self):
        """
        Compose the layout of the StepperStatusWidget.
        Includes the motor title, status, position, limit switch, homing status, and jog controls.
        
        Yields:
            Static and other widgets: The title, status displays, limit switch, homing status, jog controls, and home button.
        """
        if not self.selected:
            yield Container(
                self.compose_title(),
                self.status_display,   # Motor status display
                self.position_display,  # Motor position display
                self.limit_switch_display,  # Limit switch status display
                self.homing_status_display,  # Homing status display
                classes="stepper-status-widget-container"
            )
        else:
            yield Horizontal(
                Container(
                self.compose_title(),
                self.status_display,   # Motor status display
                self.position_display,  # Motor position display
                self.limit_switch_display,  # Limit switch status display
                self.homing_status_display,  # Homing status display
                # Display jog controls, command position input, and home button if selected
                Horizontal(self.home_negative_button, self.home_positive_button, classes="home-buttons-container"),
                self.jog_buttons.compose_jog_buttons(),
                classes="stepper-status-widget-container"
            ),
                Container(
                self.config_widget,
                classes="stepper-status-widget-container"
                ),
            classes="stepper-status-widget-container"
            )
            yield self.command_position_widget
            

    def on_mount(self):
        """
        Refresh the display to show the current motor position, limit switch, and homing status.
        Updates on widget mount and reflects the current state from the model.
        """
        super().on_mount()

        # Update limit switch status based on the model
        self.on_limit_switch_change()
        
        # Update homing status (placeholder text for now)
        self.on_homing_status_change()

