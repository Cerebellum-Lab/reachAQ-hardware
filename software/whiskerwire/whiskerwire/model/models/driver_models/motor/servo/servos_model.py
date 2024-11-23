from .servo_model import ServoModel
from ..motors_model import MotorsModel
from pyjerrycan import Status, JerryCANCfgMsg, ServoStatus
import logging
import sys

logger = logging.getLogger("WhiskerWire")


class ServosModel(MotorsModel):
    """
    Model representing a collection of servos, providing methods to update servo statuses
    and configurations from incoming CAN messages. Inherits from MotorsModel.
    """

    def __init__(self, servos: list[ServoModel]):
        """
        Initialize the ServosModel with a list of ServoModel instances.

        Args:
            servos (list[ServoModel]): A list of ServoModel instances representing individual servos.
        """
        super().__init__(motors=servos)

    def update_from_status_message(self, msg: Status):
        """
        Update each servo's status from a Status message.

        Args:
            msg (Status): The Status message containing status data for each servo.
        """
        servo_status = (msg.servo_status0, msg.servo_status1, msg.servo_status2)

        for instance, servo in self.motors.items():
            try:
                servo.status = servo_status[instance]
            except IndexError:
                logger.error(f"No status available for servo instance {instance}")

    def update_from_servo_status_message(self, msg: ServoStatus):
        """
        Update the designated servo's status, homing status, possition, and limit switch state from
        a ServoStatus message.

        Args:
            msg (ServoStatus): The ServoStatus message containing the status of a servo.
        """
        servo = self.get_motor(msg.motor_id)
        if servo is not None:
            servo.status, servo.position = msg.status, msg.position

    def update_from_cfg_message(self, msg: JerryCANCfgMsg.ServoCfg):
        """
        Placeholder method to handle servo configuration messages.

        Args:
            msg (JerryCANCfgMsg.ServoCfg): The configuration message for a servo.

        Raises:
            NotImplementedError: Indicates that configuration processing is not yet implemented.
        """
        servo = self.get_motor(msg.motor_id)
        if servo is not None:
            (
                servo.min_position,
                servo.max_position,
                servo.min_pwm_duration_us,
                servo.max_pwm_duration_us,
                servo.max_velocity,
                servo.max_acceleration,
            ) = (
                msg.min_position,
                msg.max_position,
                msg.min_pwm_duration_us,
                msg.max_pwm_duration_us,
                msg.max_velocity,
                msg.max_acceleration,
            )
