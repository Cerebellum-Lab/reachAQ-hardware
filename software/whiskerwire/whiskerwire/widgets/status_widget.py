from textual.widget import Widget
from textual.widgets import Static
from textual.events import Click, Enter, Leave
from textual import on
from ..utils import get_logger, to_valid_identifier

logger = get_logger()


# Base class for a status widget in the dashboard, used to display dynamic statuses
# and provide interactivity within the WhiskerWire application.
class StatusWidget(Widget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling this widget

    def __init__(self, widget_name: str, instance: int, **kwargs):
        """
        Initialize the StatusWidget with a unique name and instance ID.

        Args:
            widget_name (str): The display name of the widget.
            instance (int): A unique identifier for this widget instance.
        """
        # Call the parent constructor and set widget-specific properties
        Widget.__init__(
            self, id=to_valid_identifier(widget_name), classes="status-widget", **kwargs
        )
        self.instance = instance
        self.widget_name = widget_name
        self.items: list[Widget] = []  # List of child widgets representing status items
        self.hidable_items: list[Widget] = (
            []
        )  # List of items that can be hidden (for conditional display)
        self.selected = False  # Tracks selection state (e.g., for styling on selection)

    def compose_title(self):
        """
        Compose and return the title widget, displayed as the header of this StatusWidget.

        Returns:
            Static: A styled header widget with the widget's display name.
        """
        return Static(f"[bold]{self.widget_name}[/bold]", classes="status-widget-title")

    def on_click(self, event: Click) -> None:
        """
        Handle click events on the widget, toggling its selection state.
        This re-renders the widget to update any visual indications of selection.

        Args:
            event (Click): The click event triggering the handler.
        """
        # Prevent clicks on hidable items from affecting selection state
        for item in self.hidable_items:
            if item.region.contains(event.screen_x, event.screen_y):
                return

        # Toggle the selection state and recompose the widget layout if changed
        self.selected = not self.selected
        self.refresh(recompose=True)

    @on(Enter)
    def on_enter(self, event: Enter) -> None:
        """
        Event handler for when the mouse enters the widget area.
        Changes the text color of the widget's children to indicate a hover effect.

        Args:
            event (Enter): The event signaling mouse entry.
        """
        for child in self.children:
            child.set_styles("color: white;")

    @on(Leave)
    def on_leave(self, event: Leave) -> None:
        """
        Event handler for when the mouse leaves the widget area.
        Resets the text color of the widget's children to the default style.

        Args:
            event (Leave): The event signaling mouse exit.
        """
        for child in self.children:
            child.set_styles("color: #cfd4d1;")  # Reset to original color
