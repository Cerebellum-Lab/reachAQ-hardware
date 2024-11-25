from textual.widget import Widget
from textual.containers import Horizontal, Vertical
from .glitchless_button import GlitchlessButton
from ..utils import get_logger, to_valid_identifier
from .labeled_input import LabeledInput
from .labeled_select import LabeledSelect
from ..model.models.driver_models.motor.stepper.stepper_model import StepperModel

logger = get_logger()

MICROSTEP_OPTIONS = [("8", "8"), ("16", "16"), ("32", "32"), ("64", "64")]


class StepperConfigWidget(Widget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(
        self,
        model: StepperModel,
        read_config: callable,
        write_config: callable,
        **kwargs,
    ):
        self.widget_name = "Stepper Config"
        self.model = model

        # Initialize the widget with a unique, valid ID derived from the widget name
        super().__init__(id=to_valid_identifier(self.widget_name), **kwargs)

        self.read_config = read_config
        self.write_config = write_config

        self.border_title = self.widget_name  # Display name on the widget's border

        # Button for submitting the stepper config
        self.write_button = GlitchlessButton(
            "Write",
            id=f"{self.id}-write-button",
            classes="command-value-button",
            action=self.write_stepper_config,
        )

        # Button for reading the stepper config
        self.read_button = GlitchlessButton(
            "Read",
            id=f"{self.id}-read-button",
            classes="command-value-button",
            action=self.read_stepper_config,
        )

        # Create input fields with placeholders, each assigned a unique ID
        self.microsteps_widget = LabeledSelect(
            options=MICROSTEP_OPTIONS,
            prompt="Microsteps",
            id="microsteps-input",
            classes="command-value-select",
            allow_blank=True,
            label="Microsteps",
        )

        self.steps_per_revolution_widget = LabeledInput(
            value="",
            placeholder="Steps per Revolution",
            id="steps-per-revolution-input",
            classes="command-value-input",
            label="Steps per Revolution",
        )

        self.model._microsteps.register_on_update(self.on_microsteps_change)
        self.model._steps_per_revolution.register_on_update(
            self.on_steps_per_revolution_change
        )

    def on_microsteps_change(self):
        self.microsteps_widget.set_value(str(self.model.microsteps))

    def on_steps_per_revolution_change(self):
        self.steps_per_revolution_widget.set_value(str(self.model.steps_per_revolution))

    def write_stepper_config(self):
        valid = True
        try:
            microsteps = int(self.microsteps)
            if self.model.is_valid_microsteps(microsteps):
                self.microsteps_widget.set_error_status(False)
            else:
                self.microsteps_widget.set_error_status(False)
                valid = False
        except:
            self.microsteps_widget.set_error_status(False)
            valid = False

        try:
            steps_per_revolution = int(self.steps_per_revolution)
            if self.model.is_valid_steps_per_revolution(steps_per_revolution):
                self.steps_per_revolution_widget.set_error_status(False)
            else:
                self.steps_per_revolution_widget.set_error_status(False)
                valid = False
        except:
            self.steps_per_revolution_widget.set_error_status(False)
            valid = False

        if valid:
            self.write_config(
                min_step_inverse=microsteps, steps_per_revolution=steps_per_revolution
            )
            self.microsteps_widget.reset()
            self.steps_per_revolution_widget.reset()

    def read_stepper_config(self):
        self.read_config()
        # Contents will be updated when the response message is received

    def compose(self):
        """
        Compose the layout of the CommandValueWidget.
        Includes a horizontal arrangement of the "Send" button and input fields.

        Yields:
            Horizontal: Container holding the button and input fields.
        """
        yield Vertical(
            self.microsteps_widget,
            self.steps_per_revolution_widget,
            Horizontal(
                self.read_button,
                self.write_button,
                id="stepper-config-button-container",
            ),
            classes="command-value-container",
        )

    def on_mount(self):
        self.read_stepper_config()

    @property
    def microsteps(self):
        return self.microsteps_widget.value

    @property
    def steps_per_revolution(self):
        return self.steps_per_revolution_widget.value
