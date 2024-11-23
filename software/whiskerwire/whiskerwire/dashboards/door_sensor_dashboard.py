from .status_widget_dashboard import StatusWidgetDashboard
from widgets.door_sensor_status_widget import DoorSensorStatusWidget
from model.models.driver_models.door_sensor.door_sensors_model import DoorSensorsModel

class DoorSensorDashboard(StatusWidgetDashboard):
    def __init__(self, model: DoorSensorsModel, **kwargs):
        self.model = model
        items = []
        for door_sensor in self.model.doors.values():
            items.append(DoorSensorStatusWidget(door_sensor))
        
        super().__init__("Door Sensors", items, **kwargs)
        