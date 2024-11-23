from ..widgets.servo_status_widget import ServoStatusWidget
from .status_widget_dashboard import StatusWidgetDashboard
from ..model.models.driver_models.motor.servo.servos_model import *


# ServoDashboard class for managing and displaying the status of multiple servos
class ServoDashboard(StatusWidgetDashboard):
    """
    A dashboard for managing and displaying multiple servo status widgets, allowing
    configuration and movement commands.

    Inherits from StatusWidgetDashboard.
    """

    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(
        self,
        model: ServosModel,
        write_config: callable,
        read_config: callable,
        move: callable,
        **kwargs
    ):
        """
        Initialize the ServoDashboard.

        Args:
            model (ServosModel): The model containing servo information.
            write_config (callable): Function for writing servo configuration.
            read_config (callable): Function for reading servo configuration.
            move (callable): Function for executing servo movement commands.
            **kwargs: Additional keyword arguments passed to the parent class.
        """
        self.model = model

        # Initialize servo widgets based on the model
        self.servos: dict[int, ServoStatusWidget] = {
            instance: ServoStatusWidget(servo, move, write_config, read_config)
            for instance, servo in self.model.motors.items()
        }

        # Call the parent constructor with the dashboard name and list of servo widgets
        super().__init__("Servos", list(self.servos.values()), **kwargs)
