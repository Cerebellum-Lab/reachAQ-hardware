from ..widgets.stepper_status_widget import StepperStatusWidget
from .status_widget_dashboard import StatusWidgetDashboard
from ..model.models.driver_models.motor.stepper.steppers_model import *
from pyjerrycan import AbsOrRel
from textual.widgets import Button
from textual import on


# StepperDashboard class for managing and displaying the status of multiple steppers
class StepperDashboard(StatusWidgetDashboard):
    """
    A dashboard for managing and displaying multiple stepper motor status widgets,
    providing jog, command move, and home actions for each stepper motor.

    Inherits from StatusWidgetDashboard.
    """

    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(
        self,
        model: SteppersModel,
        write_config: callable,
        read_config: callable,
        move: callable,
        home: callable,
        **kwargs
    ):
        """
        Initialize the StepperDashboard.

        Args:
            model (SteppersModel): The model containing stepper information.
            write_config (callable): Function for writing stepper configuration.
            read_config (callable): Function for reading stepper configuration.
            move (callable): Function for executing stepper movement commands.
            home (callable): Function for homing the stepper motors.
            **kwargs: Additional keyword arguments passed to the parent class.
        """
        self.model = model

        # Initialize stepper widgets based on the model
        self.steppers: dict[int, StepperStatusWidget] = {
            instance: StepperStatusWidget(
                stepper, move, home, read_config, write_config
            )
            for instance, stepper in self.model.motors.items()
        }

        # Call the parent constructor with the dashboard name and list of stepper widgets
        super().__init__("Steppers", list(self.steppers.values()), **kwargs)
