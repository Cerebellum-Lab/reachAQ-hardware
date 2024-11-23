from textual.widgets import Static, Button
from textual.containers import Container, Grid
from ..widgets.sensor_status_widget import SensorStatusWidget
from .status_widget_dashboard import StatusWidgetDashboard
from textual import on


# SensorDashboard class inheriting from StatusWidgetDashboard to manage various sensor widgets
class SensorDashboard(StatusWidgetDashboard):
    """
    A dashboard for managing and displaying various sensor widgets.
    Inherits from StatusWidgetDashboard.
    """

    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the dashboard

    def __init__(self, sensors: list[SensorStatusWidget] = [], **kwargs):
        """
        Initialize the SensorDashboard.

        Args:
            sensors (list[SensorStatusWidget]): A list of sensor widgets to display in the dashboard.
            **kwargs: Additional keyword arguments passed to the parent class.
        """
        # Initialize the dashboard with the title "Sensors" and the list of sensor widgets
        super().__init__("Sensors", sensors, **kwargs)

    def compose(self):
        """
        Compose the layout of the SensorDashboard, including a title and a grid of sensor widgets.

        Yields:
            Container: Contains the dashboard title and a grid of sensor widgets.
        """
        yield Container(
            Static(
                f"[bold]{self.dashboard_name}[/bold]", classes="status-dashboard-title"
            ),  # Dashboard title
            Grid(
                *[
                    Container(item, classes="sensor-widget-container")
                    for item in self.items
                ],
                classes="sensor-grid",
            ),
            id="sensor-container",
        )
