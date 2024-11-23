from .door_sensor_model import DoorSensorModel
from pyjerrycan import Doors
import logging

logger = logging.getLogger("WhiskerWire")


class DoorSensorsModel:
    def __init__(self, door_sensors: list[DoorSensorModel]):
        self.doors = dict()
        for door in door_sensors:
            self.doors.update({door.index: door})

    def update_from_message(self, msg: Doors):
        opened = msg.opened

        # Update each GPIO's state based on the message data
        for index, door in self.doors.items():
            door.state = bool((opened >> index) & 0b1)
