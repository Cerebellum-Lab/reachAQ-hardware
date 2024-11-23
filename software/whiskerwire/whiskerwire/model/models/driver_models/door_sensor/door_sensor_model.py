from ....watchable import Watchable
from ..gpio.gpio_model import GPIOModel, GPIODirection

class DoorSensorModel(GPIOModel):
    def __init__(self, name: str, index: int):
        super().__init__(name, index, GPIODirection.INPUT)
