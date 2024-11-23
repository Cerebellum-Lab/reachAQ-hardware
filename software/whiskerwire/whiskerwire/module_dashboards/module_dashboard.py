from textual.widget import Widget
from textual.widgets import Static
from textual.containers import Grid
from ..dashboards.status_widget_dashboard import StatusWidgetDashboard
from ..model.models.module_model import ModuleModel
import logging

logger = logging.getLogger("WhiskerWire")


# Base class for managing and displaying status dashboards for a specific module.
# The dashboard aggregates multiple StatusWidgetDashboard instances related to the module.
class ModuleDashboard(Widget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, module_name: str, model: ModuleModel, dashboards: list[StatusWidgetDashboard], **kwargs):
        """
        Initialize the ModuleDashboard with a module name, model, and associated dashboards.
        
        Args:
            module_name (str): The name of the module.
            model (ModuleModel): The data model representing the module's properties.
            dashboards (list[StatusWidgetDashboard]): A list of status dashboards associated with this module.
            **kwargs: Additional keyword arguments passed to the parent class.
        """
        super().__init__(**kwargs)

        self.model = model  # Module data model
        self.module_name = module_name  # Name of the module
        self.dashboards = dashboards  # List of associated status dashboards

        self.title = Static(
            f"[bold]{self.module_name} Module [{str(self.model.can_address)}][/bold]",
            id=f"{self.module_name.lower()}-dashboard-title",
            classes="module-dashboard-title"
        )

    def compose_title(self):
        """
        Compose the title for the module dashboard, showing the module name and CAN address.
        
        Returns:
            Static: A Static widget containing the module's title with its CAN ID.
        """
        return self.title

    def compose(self):
        """
        Compose the layout of the ModuleDashboard, including the title and the associated dashboards.
        
        Yields:
            Static and Grid: The title and a grid container of status dashboards for the module.
        """
        yield self.compose_title()  # Module title display

        # Grid container holding all status dashboards related to the module
        yield Grid(
            *self.dashboards,
            id=f"{self.module_name.lower()}-module-dashboard-container",
            classes="module-dashboard-container"
        )
