from textual.widgets import Static
from textual.containers import Horizontal, Vertical, Grid
from ..widgets.input_gpio_status_widget import InputGPIOStatusWidget
from ..widgets.output_gpio_status_widget import OutputGPIOStatusWidget
from .status_widget_dashboard import StatusWidgetDashboard
from pyjerrycan import GPIOWrite
from ..model.models.driver_models.gpio.gpios_model import GPIOSModel
from ..model.models.driver_models.gpio.gpio_model import GPIODirection
from ..utils import get_logger
from functools import partial

logger = get_logger()


# GPIODashboard class for managing and displaying GPIO states and controls
class GPIODashboard(StatusWidgetDashboard):
    """
    A dashboard for managing and displaying GPIO states and controls.
    Inherits from StatusWidgetDashboard.
    """

    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: GPIOSModel, gpio_write: callable, **kwargs):
        """
        Initialize the GPIODashboard.

        Args:
            model (GPIOSModel): The model containing GPIOs for input/output status.
            gpio_write (callable): Function to write/send GPIO state changes, typically via CAN.
            **kwargs: Additional keyword arguments for the parent class.
        """
        self.model = model
        self.gpio_write = gpio_write

        # Separate GPIO widgets based on direction (input/output)
        self.input_gpios: dict[int, InputGPIOStatusWidget] = {}
        self.output_gpios: dict[int, OutputGPIOStatusWidget] = {}

        for index, gpio in self.model.gpios.items():
            if gpio.direction == GPIODirection.INPUT:
                self.input_gpios[index] = InputGPIOStatusWidget(gpio)
            elif gpio.direction == GPIODirection.OUTPUT:
                self.output_gpios[index] = OutputGPIOStatusWidget(
                    gpio, gpio_write=partial(gpio_write, instance=self.model.instance)
                )

        # Gather all GPIO widgets for dashboard display
        self.items = list(self.input_gpios.values()) + list(self.output_gpios.values())
        super().__init__("GPIO", self.items, **kwargs)

    def compose(self):
        """
        Compose the layout of the GPIODashboard, including input and output GPIO sections.
        Arranges inputs and outputs in separate containers within the dashboard.

        Yields:
            Static: Title for the dashboard.
            Horizontal: Container holding both input and output GPIO sections.
        """
        yield Static(
            f"[bold]{self.dashboard_name}[/bold]", classes="status-dashboard-title"
        )

        # Create sub-dashboard sections for output and input GPIOs
        sub_dashboards = []

        if self.output_gpios:
            sub_dashboards.append(
                Vertical(
                    Static(f"[bold]Outputs[/bold]", classes="status-dashboard-title"),
                    Grid(
                        *self.output_gpios.values(),
                        classes="output-gpio-dashboard-container",
                    ),
                    classes="gpio-dashboard-container",
                )
            )

        if self.input_gpios:
            sub_dashboards.append(
                Vertical(
                    Static(f"[bold]Inputs[/bold]", classes="status-dashboard-title"),
                    Grid(
                        *self.input_gpios.values(),
                        classes="input-gpio-dashboard-container",
                    ),
                    classes="gpio-dashboard-container",
                )
            )

        # Arrange input and output sections horizontally within the dashboard
        yield Horizontal(*sub_dashboards, classes="dashboard-container")
