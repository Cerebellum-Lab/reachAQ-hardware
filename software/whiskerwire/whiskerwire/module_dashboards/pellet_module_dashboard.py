from textual.containers import Grid
from ..dashboards.stepper_dashboard import StepperDashboard
from ..dashboards.servo_dashboard import ServoDashboard
from ..dashboards.gpio_dashboard import GPIODashboard
from ..dashboards.tone_generator_dashboard import ToneGeneratorDashboard, ToneGeneratorStatusWidget
from ..dashboards.analog_out_dashboard import AnalogOutDashboard, AnalogOutWidget
from ..dashboards.door_sensor_dashboard import DoorSensorDashboard
from ..widgets.rgb_led_status_widget import RGBLEDStatusWidget
from .module_dashboard import ModuleDashboard
from ..model.models.pellet_module_model import PelletModuleModel
from pyjerrycan import JerryCAN
from functools import partial


# PelletModuleDashboard class for managing and displaying the status of a pellet module.
# Inherits from ModuleDashboard and includes sub-dashboards for stepper, servo, GPIO, tone generator, and analog outes.
class PelletModuleDashboard(ModuleDashboard):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, jc: JerryCAN, model: PelletModuleModel, **kwargs):
        """
        Initialize the PelletModuleDashboard with JerryCAN, a model, and optional configurations.
        
        This dashboard manages multiple sub-dashboards, including stepper, servo, GPIO, tone generator, and analog out.
        
        Args:
            jc (JerryCAN): An instance of JerryCAN for handling CAN communication.
            model (PelletModuleModel): The data model for the pellet module.
            **kwargs: Additional keyword arguments passed to the parent class.
        """
        self.model = model

        # Initialize the GPIODashboard for GPIOs, configuring write actions using JerryCAN
        self.gpio_dashboard = GPIODashboard(
            self.model.gpios, gpio_write=partial(jc.GPIOWrite, dst_id=self.model.dst_id)
        )

        # Initialize the StepperDashboard with actions for three stepper motors
        self.stepper_dashboard = StepperDashboard(
            self.model.steppers,
            write_config=partial(jc.StepperCfgWrite, dst_id=self.model.dst_id),
            read_config=partial(jc.StepperCfgRead, dst_id=self.model.dst_id),
            move=partial(jc.StepperMove, dst_id=self.model.dst_id),
            home=partial(jc.StepperHome, dst_id=self.model.dst_id),
        )

        # Initialize the ServoDashboard for three servo motors with configuration and movement actions
        self.servo_dashboard = ServoDashboard(
            self.model.servos,
            write_config=partial(jc.ServoCfgWrite, dst_id=self.model.dst_id),
            read_config=partial(jc.ServoCfgRead, dst_id=self.model.dst_id),
            move=partial(jc.ServoMove, dst_id=self.model.dst_id)
        )

        self.door_sensor_dashboard = DoorSensorDashboard(self.model.door_sensors)

        self.rgb_led = RGBLEDStatusWidget(self.model.rgb_led, write=partial(jc.RGBLEDWrite, dst_id=self.model.dst_id))

        # Initialize the AnalogOutDashboard with a single analog out widget
        self.analog_out_dashboard = AnalogOutDashboard(
            AnalogOutWidget(self.model.analog_out, analog_write=partial(jc.AnalogOutWrite, dst_id=self.model.dst_id))
        )

        # Initialize the ToneGeneratorDashboard with a single tone generator widget
        self.tone_generator_dashboard = ToneGeneratorDashboard(
            ToneGeneratorStatusWidget(self.model.tone_generator,
                                      tone_write=partial(jc.ToneWrite, dst_id=self.model.dst_id))
        )

        # Call the parent constructor to initialize the module dashboard with all sub-dashboards
        super().__init__(
            "Pellet",
            self.model,
            [
                self.gpio_dashboard,
                self.stepper_dashboard,
                self.servo_dashboard,
                self.door_sensor_dashboard,
                self.analog_out_dashboard,
                self.tone_generator_dashboard,
                self.rgb_led,
            ],
            **kwargs,
        )

    def compose_dashboard(self):
        """
        Compose the layout of the PelletModuleDashboard, including stepper, servo, GPIO, tone generator, and analog out dashboards.
        
        Returns:
            Grid: A grid layout containing all sub-dashboards.
        """
        return Grid(
            self.stepper_dashboard,
            self.servo_dashboard,
            self.gpio_dashboard,
            self.door_sensor_dashboard,
            self.rgb_led,
            self.analog_out_dashboard,
            self.tone_generator_dashboard,
            id=f"{self.module_name.lower()}-module-dashboard-container",
            classes="module-dashboard-container",
        )
