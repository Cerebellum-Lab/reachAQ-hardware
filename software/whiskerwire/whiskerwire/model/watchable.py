from dataclasses import dataclass
from time import time


@dataclass
class Watchable:
    """
    A watchable variable that allows monitoring for changes.
    This class can be used to wrap data that should notify observers
    or trigger callbacks whenever its value is updated. It is useful
    in reactive programming contexts where automatic updates or
    responses to data changes are required.
    """

    __value: any
    __on_update: callable
    __only_on_change: bool

    def __init__(
        self,
        initial_value: any = None,
        only_on_change: bool = True,
        min_update_period: float = 0.0,
    ):
        self.__value = initial_value  # The actual value being watched
        self.__on_update = []
        self.__only_on_change = only_on_change
        self.__min_update_period = min_update_period
        self.__last_update_time = -1.0

    def register_on_update(self, callback: callable):
        self.__on_update.append(callback)

    @property
    def value(self) -> any:
        return self.__value

    @value.setter
    def value(self, value: any):
        if self.__only_on_change and self.__value == value:
            return

        current_time = time()
        if current_time - self.__last_update_time >= self.__min_update_period:
            self.__last_update_time = current_time
            self.__value = value

            for callback in self.__on_update:
                callback()

    @property
    def last_update_time(self) -> float:
        return self.__last_update_time

    def __repr__(self):
        return str(self.__value)
