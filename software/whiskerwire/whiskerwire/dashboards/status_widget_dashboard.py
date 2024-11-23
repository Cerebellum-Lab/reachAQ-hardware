from textual.widget import Widget
from textual.widgets import Static
from textual.containers import Grid
from ..widgets.status_widget import StatusWidget
from ..widgets.util import to_valid_identifier


# StatusWidgetDashboard class to manage and display a collection of status widgets in a dashboard
class StatusWidgetDashboard(Widget):
    """
    A dashboard for managing and displaying a collection of status widgets in a grid layout.
    Inherits from the Textual Widget class.
    """

    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, dashboard_name: str, items: list[StatusWidget], **kwargs):
        """
        Initialize the StatusWidgetDashboard.

        Args:
            dashboard_name (str): The name of the dashboard (used for the title and identification).
            items (list[StatusWidget]): A list of status widgets to display on the dashboard.
            **kwargs: Additional keyword arguments for the parent Widget class.
        """
        # Call the parent constructor and initialize with "status-widget-dashboard" styling class
        super().__init__(classes="status-widget-dashboard", **kwargs)

        self.dashboard_name = dashboard_name  # Name of the dashboard
        self.identifier = to_valid_identifier(dashboard_name)  # Generate a valid identifier for the dashboard

        # Ensure items is a list of StatusWidget instances
        if not isinstance(items, list):
            items = [items]
        self.items = items  # Store the list of status widgets

    def compose(self):
        """
        Compose the layout of the StatusWidgetDashboard.
        This includes a title and a grid layout of status widgets.

        Yields:
            Static: A title for the dashboard.
            Grid: A grid containing the status widgets.
        """
        # Dashboard title
        yield Static(f"[bold]{self.dashboard_name}[/bold]", classes="status-dashboard-title")

        # Dashboard container grid with status widgets
        yield Grid(
            *self.items,  # Each status widget in the grid
            id=f"{self.identifier}-dashboard-container",
            classes="dashboard-container"
        )

    def contains_selected_widget(self) -> bool:
        for item in self.items:
            if item.selected:
                return True

        return False
