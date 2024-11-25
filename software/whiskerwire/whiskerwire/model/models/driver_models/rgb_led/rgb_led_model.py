from ....watchable import Watchable
from .....utils import get_logger

logger = get_logger()

MIN_RGB_VALUE = 0
MAX_RGB_VALUE = 100


class RGBLEDModel:
    def __init__(self, name: str):
        self._name = name

        self._red = Watchable(0)
        self._green = Watchable(0)
        self._blue = Watchable(0)

    def is_valid_value(self, value: int):
        return value <= MAX_RGB_VALUE and value >= MIN_RGB_VALUE

    @property
    def name(self) -> str:
        return self._name

    @property
    def red(self) -> int:
        return self._red.value

    @red.setter
    def red(self, value: int):
        if not self.is_valid_value(value):
            raise ValueError(
                f"Invalid red value: must be on the interval [{MIN_RGB_VALUE}, {MAX_RGB_VALUE}]"
            )

        self._red.value = value

    @property
    def green(self) -> int:
        return self._green.value

    @green.setter
    def green(self, value: int):
        if not self.is_valid_value(value):
            raise ValueError(
                f"Invalid green value: must be on the interval [{MIN_RGB_VALUE}, {MAX_RGB_VALUE}]"
            )

        self._green.value = value

    @property
    def blue(self) -> int:
        return self._blue.value

    @blue.setter
    def blue(self, value: int):
        if not self.is_valid_value(value):
            raise ValueError(
                f"Invalid blue value: must be on the interval [{MIN_RGB_VALUE}, {MAX_RGB_VALUE}]"
            )

        self._blue.value = value

    def update_from_message(self, msg):
        self.red, self.green, self.blue = msg.red, msg.green, msg.blue
