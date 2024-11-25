from .models.pellet_module_model import PelletModuleModel
from .models.magnet_module_model import MagnetModuleModel
from pyjerrycan import JerryCAN, JerryCANCmdType, JerryCANMsg
from ..utils import get_logger
from .watchable import Watchable

logger = get_logger()


class Model:
    def __init__(self):
        super().__init__()
        self.modules: dict[int, MagnetModuleModel | PelletModuleModel] = dict()

        # This is not used right now, nor are the low-level estop status variables, since
        # E-Stop state is not actually be maintained and transmitted through the status message
        self._estop_status = Watchable(False)

    @property
    def estop_status(self) -> bool:
        """bool: Returns the current emergency stop (E-stop) status."""
        return self._estop_status.value

    @estop_status.setter
    def estop_status(self, value: bool):
        """Set the emergency stop (E-stop) status."""
        self._estop_status.value = value

    def process_message(self, msg: JerryCANMsg):
        dst_id = msg.dst_id
        device_type = dst_id >> 2
        can_address = dst_id & 0b0011
        logger.debug(
            f"Received CAN Message: DeviceType={str(device_type)}, Address={can_address}, Type={msg.type}"
        )

        if msg.type == JerryCANCmdType.STATUS:
            self.estop_status = msg.status.estop_active

        if dst_id in self.modules:
            new_module = False
        else:
            if device_type == PelletModuleModel.DEVICE_TYPE:
                logger.info(
                    f"New Pellet Module detected with CAN address: {can_address}"
                )
                self.modules.update({dst_id: PelletModuleModel(can_address)})
            elif device_type == MagnetModuleModel.DEVICE_TYPE:
                logger.info(
                    f"New Magnet Module detected with CAN address: {can_address}"
                )
                self.modules.update({dst_id: MagnetModuleModel(can_address)})
            new_module = True

        self.modules[dst_id].process_message(msg)

        return new_module, self.modules[dst_id]
