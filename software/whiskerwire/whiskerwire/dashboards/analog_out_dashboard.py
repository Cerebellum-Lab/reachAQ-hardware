from textual.widgets import Button
from widgets.analog_out_widget import AnalogOutWidget
from .status_widget_dashboard import StatusWidgetDashboard
from textual import on
from pyjerrycan import AnalogOut
from functools import partial

# AnalogOutDashboard class for managing and displaying the status of multiple analog outes
class AnalogOutDashboard(StatusWidgetDashboard):
    """
    A dashboard for managing and displaying the status of multiple analog out widgets.
    Inherits from StatusWidgetDashboard.
    """

    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, analog_out: list[AnalogOutWidget], **kwargs):
        """
        Initialize the AnalogOutDashboard.

        Args:
            analog_out (list[AnalogOutWidget]): A list of analog out widgets to display in the dashboard.
            write (callable): A function or method to write/send data (typically a CAN message).
            **kwargs: Additional keyword arguments passed to the parent class.
        """
        # Initialize the dashboard with the list of analog outes
        super().__init__("Analog Out", analog_out, **kwargs)
