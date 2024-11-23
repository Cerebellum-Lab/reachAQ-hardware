from .driver_models.door_sensor.door_sensor_model import DoorSensorModel
from .driver_models.door_sensor.door_sensors_model import DoorSensorsModel
from .driver_models.rgb_led.rgb_led_model import RGBLEDModel
from .module_model import ModuleModel
from .driver_models.gpio.gpios_model import GPIOSModel
from .driver_models.gpio.gpio_model import GPIOModel, GPIODirection
from .driver_models.motor.servo.servos_model import ServosModel, ServoModel
from .driver_models.motor.stepper.steppers_model import SteppersModel, StepperModel
from .driver_models.tone_generator.tone_generator_model import ToneGeneratorModel
from .driver_models.analog_out.analog_out_model import AnalogOutModel
from pyjerrycan import JerryCANMsg, JerryCANCmdType, JerryCANCfgMsg

class PelletModuleModel(ModuleModel):
    """
    Model representing a pellet module, managing GPIOs, servos, steppers, tone generator, and analog out.
    Inherits from ModuleModel and processes specific CAN messages for module components.
    """
    DEVICE_TYPE = 0x00  # Device type identifier for the pellet module

    def __init__(self, can_address: int):
        """
        Initialize the PelletModuleModel with a CAN address, setting up GPIO, servo, stepper, tone generator, 
        and analog out models.

        Args:
            can_address (int): The CAN address for the pellet module (0-3).
        """
        super().__init__(
            can_address,
            gpios=GPIOSModel([
                GPIOModel("STIM0", 4, GPIODirection.OUTPUT), 
                GPIOModel("STIM1", 5, GPIODirection.OUTPUT),
                GPIOModel("STIM2", 6, GPIODirection.OUTPUT), 
                GPIOModel("STIM3", 7, GPIODirection.OUTPUT)
            ]),
            servos=ServosModel([
                ServoModel("Servo 0", 0), 
                ServoModel("Servo 1", 1), 
                ServoModel("Servo 2", 2)
            ]),
        )
        
        # Initialize models for steppers, tone generator, and analog out
        self.steppers = SteppersModel([
            StepperModel("Stepper 0", 0),
            StepperModel("Stepper 1", 1),
            StepperModel("Stepper 2", 2)
        ])
        self.door_sensors = DoorSensorsModel([
            DoorSensorModel("Door Sensor 0", 0),
            DoorSensorModel("Door Sensor 1", 1),
            DoorSensorModel("Door Sensor 2", 2)
        ])
        self.rgb_led = RGBLEDModel("RGB LED")
        self.tone_generator = ToneGeneratorModel("Tone Generator 0", 0)
        self.analog_out = AnalogOutModel("Analog Out 0", 0)

    def process_message(self, msg: JerryCANMsg):
        """
        Process incoming JerryCAN messages to update module components, including GPIOs, servos, steppers, 
        tone generator, and analog out.

        Args:
            msg (JerryCANMsg): The incoming CAN message containing component data.

        Returns:
            JerryCANCmdType: The type of the message processed.
        """
        # Process common messages through the parent class first (GPIO, servos)
        type = super().process_message(msg)

        # Process specific component-related messages for the pellet module
        if type == JerryCANCmdType.STATUS:
            # Update stepper status from the STATUS message
            # Is now handled by servo status message
            #self.steppers.update_from_status_message(msg.status)
            pass # FIXME: Update once new status message has been defined
        elif type == JerryCANCmdType.STEPPER_STATUS:
            # Update stepper status from the stepper status message
            self.steppers.update_from_stepper_status_message(msg.stepper_status)
        elif type == JerryCANCmdType.CFG_RESPONSE:
            cfg_response = msg.cfg_response
            if cfg_response.type == JerryCANCfgMsg.Type.STEPPER:
                # Update stepper configuration from the configuration response message
                self.steppers.update_from_cfg_message(cfg_response.stepper)
        elif type == JerryCANCmdType.TONE:
            # Update tone generator status from the TONE message
            self.tone_generator.update_from_message(msg.tone)
        elif type == JerryCANCmdType.ANALOG_OUT:
            # Update analog out from the ANALOG_STATUS_READ message
            self.analog_out.update_from_message(msg.analog_out)
        elif type == JerryCANCmdType.DOOR_SENSOR:
            self.door_sensors.update_from_message(msg.doors)
        elif type == JerryCANCmdType.RGB_LED:
            self.rgb_led.update_from_message(msg.rgb_led)
        return type
