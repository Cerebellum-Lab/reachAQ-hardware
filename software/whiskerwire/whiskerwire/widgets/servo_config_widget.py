from textual.widget import Widget
from textual.widgets import Input, Select, Label
from textual.containers import Container, Horizontal, Vertical
from .glitchless_button import GlitchlessButton
from .util import to_valid_identifier
from .labeled_input import LabeledInput
from ..model.models.driver_models.motor.servo.servo_model import ServoModel
import logging

logger = logging.getLogger("WhiskerWire")


class ServoConfigWidget(Widget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: ServoModel, read_config: callable, write_config: callable, **kwargs):
        self.widget_name = "Servo Config"
        self.model = model
        # Initialize the widget with a unique, valid ID derived from the widget name
        super().__init__(id=to_valid_identifier(self.widget_name), **kwargs)

        self.read_config = read_config
        self.write_config = write_config

        self.border_title = self.widget_name  # Display name on the widget's border

        # Button for submitting the servo config
        self.write_button = GlitchlessButton("Write", id=f"{self.id}-write-button", classes="command-value-button",
                                             action=self.write_servo_config)

        # Button for reading the servo config
        self.read_button = GlitchlessButton("Read", id=f"{self.id}-read-button", classes="command-value-button",
                                            action=self.read_servo_config)

        # Create input fields with placeholders, each assigned a unique ID
        self.min_position_widget = LabeledInput(value="", placeholder="Min Position", id="min-position-input",
                                                classes="command-value-input", label="Min Position")
        self.max_position_widget = LabeledInput(value="", placeholder="Max Position", id="max-position-input",
                                                classes="command-value-input", label="Max Position")
        self.min_pwm_duration_widget = LabeledInput(value="", placeholder="Min PWM Duration",
                                                    id="min-pwm-duration-input", classes="command-value-input",
                                                    label="Min PWM Duration")
        self.max_pwm_duration_widget = LabeledInput(value="", placeholder="Max PWM Duration",
                                                    id="max-pwm-duration-input", classes="command-value-input",
                                                    label="Max PWM Duration")
        # self.steps_per_revolution_widget.border_title = "Steps per Revolution"

        self.model._min_position.register_on_update(self.on_min_position_change)
        self.model._max_position.register_on_update(self.on_max_position_change)
        self.model._min_pwm_duration_us.register_on_update(self.on_min_pwm_duration_change)
        self.model._max_pwm_duration_us.register_on_update(self.on_max_pwm_duration_change)

    def on_min_position_change(self):
        self.min_position_widget.set_value(str(self.model.min_position))

    def on_max_position_change(self):
        self.max_position_widget.set_value(str(self.model.max_position))

    def on_min_pwm_duration_change(self):
        self.min_pwm_duration_widget.set_value(str(self.model.min_pwm_duration_us))

    def on_max_pwm_duration_change(self):
        self.max_pwm_duration_widget.set_value(str(self.model.max_pwm_duration_us))

    def write_servo_config(self):
        valid = True
        try:
            min_position = float(self.min_position)
            if self.model.is_valid_position(min_position):
                self.min_position_widget.set_error_status(False)
            else:
                self.min_position_widget.set_error_status(True)
                valid = False
        except:
            self.min_position_widget.set_error_status(True)
            valid = False

        try:
            max_position = float(self.max_position)
            if self.model.is_valid_position(max_position):
                self.max_position_widget.set_error_status(False)
            else:
                self.max_position_widget.set_error_status(True)
                valid = False
        except:
            self.max_position_widget.set_error_status(True)
            valid = False

        try:
            min_pwm_duration = float(self.min_pwm_duration)
            if self.model.is_valid_pwm_duration_us(min_pwm_duration):
                self.min_pwm_duration_widget.set_error_status(False)
            else:
                self.min_pwm_duration_widget.set_error_status(True)
                valid = False
        except:
            self.min_pwm_duration_widget.set_error_status(True)
            valid = False

        try:
            max_pwm_duration = float(self.max_pwm_duration)
            if self.model.is_valid_pwm_duration_us(max_pwm_duration):
                self.max_pwm_duration_widget.set_error_status(False)
            else:
                self.max_pwm_duration_widget.set_error_status(True)
                valid = False
        except:
            self.max_pwm_duration_widget.set_error_status(True)
            valid = False

        if valid:
            self.write_config(min_position=min_position, max_position=max_position,
                              min_pwm_duration_us=min_pwm_duration, max_pwm_duration_us=max_pwm_duration)
            self.min_position_widget.reset()
            self.max_position_widget.reset()
            self.min_pwm_duration_widget.reset()
            self.max_pwm_duration_widget.reset()

    def read_servo_config(self):
        self.read_config()
        # Contents will be updated when the response message is received

    def compose(self):
        """
        Compose the layout of the CommandValueWidget.
        Includes a horizontal arrangement of the "Send" button and input fields.
        
        Yields:
            Horizontal: Container holding the button and input fields.
        """
        yield Horizontal(
            Vertical(
                self.read_button, self.write_button, id="servo-config-button-container"
            ),
            Vertical(
                Horizontal(
                    self.min_position_widget, self.max_position_widget, classes="command-value-container"
                ),
                Horizontal(
                    self.min_pwm_duration_widget, self.max_pwm_duration_widget, classes="command-value-container"
                ),
                classes="command-value-container"
            ),
            classes="command-value-container"
        )
        """yield Vertical(
            Horizontal(
                self.min_position_widget, self.min_pwm_duration_widget, classes="command-value-container"
            ),
            Horizontal(
                self.max_position_widget, self.max_pwm_duration_widget, classes="command-value-container"
            ),
            Horizontal(
                self.read_button, self.write_button, id="servo-config-button-container"
            ),
            classes="command-value-container"
        )"""

    def on_mount(self):
        self.read_servo_config()

    @property
    def min_position(self):
        return self.min_position_widget.value

    @property
    def max_position(self):
        return self.max_position_widget.value

    @property
    def min_pwm_duration(self):
        return self.min_pwm_duration_widget.value

    @property
    def max_pwm_duration(self):
        return self.max_pwm_duration_widget.value
