from .stepper_model import StepperModel
from ..motors_model import MotorsModel
from pyjerrycan import Status, JerryCANCfgMsg, StepperStatus
from ......utils import get_logger

logger = get_logger()


class SteppersModel(MotorsModel):
    """
    Model representing a collection of stepper motors, providing methods to access individual steppers
    by name or instance and to update their statuses and limit switch states from incoming messages.
    Inherits from MotorsModel.
    """

    def __init__(self, steppers: list[StepperModel]):
        """
        Initialize the SteppersModel with a list of StepperModel instances.

        Args:
            steppers (list[StepperModel]): A list of StepperModel instances representing individual steppers.
        """
        super().__init__(motors=steppers)

    def get_limit_switch(self, specifier: int | str) -> bool | None:
        """
        Retrieve the limit switch state of a specified stepper.

        Args:
            specifier (int | str): The instance ID or name of the stepper.

        Returns:
            bool | None: The limit switch state if the stepper exists, otherwise None.
        """
        stepper = self.get_motor(specifier)
        if stepper is None:
            logger.error(
                f"Failed to get limit switch for stepper with specifier <{specifier}>"
            )
            return None

        return stepper.limit_switch

    def update_from_status_message(self, msg: Status):
        """
        Update each stepper's status and limit switch state from a Status message.

        Args:
            msg (Status): The Status message containing status and limit switch data for each stepper.
        """
        stepper_status = (msg.stepper_status0, msg.stepper_status1, msg.stepper_status2)
        limit_switch_status = (msg.limit_switch0, msg.limit_switch1, msg.limit_switch2)

        for instance, stepper in self.motors.items():
            try:
                stepper.status = stepper_status[instance]
                stepper.limit_switch = limit_switch_status[instance]
            except IndexError:
                logger.error(f"No status available for stepper instance {instance}")

    def update_from_stepper_status_message(self, msg: StepperStatus):
        """
        Update the designated stepper's status, homing status, possition, and limit switch state from
        a StepperStatus message.

        Args:
            msg (StepperStatus): The StepperStatus message containing the status of a stepper.
        """
        stepper = self.get_motor(msg.motor_id)
        if stepper is not None:
            (
                stepper.status,
                stepper.homing_status,
                stepper.position,
                stepper.limit_switch,
            ) = (msg.status, msg.homing_status, msg.position, msg.limit_switch)

    def update_from_cfg_message(self, msg: JerryCANCfgMsg.StepperCfg):
        """
        Placeholder method to handle configuration messages for steppers.

        Args:
            msg (JerryCANCfgMsg.StepperCfg): The configuration message for a stepper.

        Raises:
            NotImplementedError: Indicates that configuration processing is not yet implemented.
        """
        stepper = self.get_motor(msg.motor_id)
        if stepper is not None:
            (
                stepper.microsteps,
                stepper.steps_per_revolution,
                stepper.max_velocity,
                stepper.max_acceleration,
            ) = (
                msg.min_step_inverse,
                msg.steps_per_revolution,
                msg.max_velocity,
                msg.max_acceleration,
            )
