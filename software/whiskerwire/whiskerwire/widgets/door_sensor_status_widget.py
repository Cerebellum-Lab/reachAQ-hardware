from ..model.models.driver_models.door_sensor.door_sensor_model import DoorSensorModel
from .input_gpio_status_widget import InputGPIOStatusWidget
import logging

logger = logging.getLogger("WhiskerWire")


class DoorSensorStatusWidget(InputGPIOStatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: DoorSensorModel, **kwargs):
        super().__init__(model, **kwargs)

    def on_mount(self):
        self.on_state_change()
