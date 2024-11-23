from textual.containers import Grid
from ..dashboards.servo_dashboard import ServoDashboard
from ..dashboards.gpio_dashboard import GPIODashboard
from ..dashboards.sensor_dashboard import SensorDashboard
from ..widgets.temperature_status_widget import TemperatureStatusWidget
from ..widgets.humidity_status_widget import HumidityStatusWidget
from ..widgets.pressure_status_widget import PressureStatusWidget
from ..widgets.load_cell_status_widget import LoadCellStatusWidget
from .module_dashboard import ModuleDashboard
from ..model.models.magnet_module_model import MagnetModuleModel
from pyjerrycan import JerryCAN
from functools import partial
import logging

from ..utils import get_logger

logger = get_logger()


# MagnetModuleDashboard class for managing and displaying the status of a magnet module.
# Inherits from ModuleDashboard and includes GPIO, servo, and sensor sub-dashboards.
class MagnetModuleDashboard(ModuleDashboard):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, jc: JerryCAN, model: MagnetModuleModel, **kwargs):
        """
        Initialize the MagnetModuleDashboard with JerryCAN, a model, and optional configurations.
        
        This dashboard manages multiple sub-dashboards, including servo, GPIO, sensor, and status dashboards.
        
        Args:
            jc (JerryCAN): An instance of JerryCAN for handling CAN communication.
            model (MagnetModuleModel): The data model for the magnet module.
            **kwargs: Additional keyword arguments passed to the parent class.
        """
        self.model = model

        # Initialize the GPIODashboard with GPIOs and configure write actions using JerryCAN
        self.gpio_dashboard = GPIODashboard(
            self.model.gpios,
            gpio_write=partial(jc.GPIOWrite, self.model.dst_id)
        )

        # Initialize the ServoDashboard with two servo status widgets and actions via JerryCAN
        self.servo_dashboard = ServoDashboard(
            self.model.servos,
            write_config=partial(jc.ServoCfgWrite, self.model.dst_id),
            read_config=partial(jc.ServoCfgRead, self.model.dst_id),
            move=partial(jc.ServoMove, dst_id=self.model.dst_id)
        )

        # Initialize the SensorDashboard with temperature, humidity, pressure, and load cell widgets
        self.sensor_dashboard = SensorDashboard([
            TemperatureStatusWidget(self.model.temperature_sensor),
            HumidityStatusWidget(self.model.humidity_sensor),
            PressureStatusWidget(self.model.pressure_sensor, tare=partial(jc.PressureSensorTare, self.model.dst_id)),
            LoadCellStatusWidget(self.model.load_cell_sensor, tare=partial(jc.LoadCellTare, self.model.dst_id))
        ])

        # Call the parent constructor to initialize the module dashboard with all sub-dashboards
        super().__init__("Magnet", self.model, [self.gpio_dashboard, self.servo_dashboard, self.sensor_dashboard],
                         **kwargs)

    def compose_dashboard(self):
        """
        Compose the layout of the MagnetModuleDashboard, including servo, GPIO, and sensor dashboards.
        
        Returns:
            Grid: A grid layout containing all sub-dashboards.
        """
        return Grid(
            self.servo_dashboard,
            self.gpio_dashboard,
            self.sensor_dashboard,
            id=f"{self.module_name.lower()}-module-dashboard-container",
            classes="module-dashboard-container"
        )
