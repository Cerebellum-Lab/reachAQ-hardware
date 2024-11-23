from textual.widgets import Button
from ..widgets.tone_generator_status_widget import ToneGeneratorStatusWidget
from .status_widget_dashboard import StatusWidgetDashboard
from textual import on
from pyjerrycan import Tone


# ToneGeneratorDashboard class for managing and displaying the status of multiple tone generators
class ToneGeneratorDashboard(StatusWidgetDashboard):
    """
    Dashboard to manage and control tone generators, providing an interface to send frequency and duration
    commands to each tone generator widget.
    """

    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, tone_generators: list[ToneGeneratorStatusWidget], **kwargs):
        """
        Initialize the ToneGeneratorDashboard.

        Args:
            tone_generators (list[ToneGeneratorStatusWidget]): A list of tone generator status widgets to display in the dashboard.
            write (callable): Function to send a tone write command to the tone generators.
            **kwargs: Additional keyword arguments passed to the parent class.
        """
        # Initialize the dashboard with the list of tone generator widgets and transmission queue
        super().__init__("Tone Generators", tone_generators, **kwargs)
