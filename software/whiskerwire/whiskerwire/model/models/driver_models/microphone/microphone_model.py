from math import isinf
from .....config import get_settings
from .....utils import get_logger, is_power_of_two
from ....watchable import Watchable
from pyjerrycan import AudioData, AudioDataCmd
import asyncio
import sys

logger = get_logger()


class MicrophoneModel:
    """
    Model representing the microphone FFt driver, managing attributes such as
    name, instance, and the FFT results.
    """

    def __init__(self, name: str, instance: int):
        """
        Initialize the MicrophoneModel with a name, instance, and number of FFT bins.

        Args:
            name (str): The name of the analog out component.
            instance (int): The instance number of the analog out component.
            bins (int): The number of bins produced by the FFT
        """
        settings = get_settings()
        self.FFT_SIZE = int(settings["Microphone"]["Graph"]["FFT Size"])
        self.Y_MIN = float(settings["Microphone"]["Graph"]["Y Min"])
        self.Y_MAX = float(settings["Microphone"]["Graph"]["Y Max"])
        self.STREAM_TIMEOUT = float(settings["Microphone"]["Stream Timeout"])
        self.MIN_UPDATE_PERIOD = float(settings["Microphone"]["Min Update Period"])

        if not is_power_of_two(self.FFT_SIZE):
            raise ValueError(f"FFT Size must be a power of 2 - got {self.FFT_SIZE}")

        # Read-only attributes
        self._name = name
        self._instance = instance

        self._active_stream = None
        self._stream_data: list[float] = []

        # Read-write attribute for the FFT data - FIXME: is float the right datatype?
        self._fft_data = Watchable(
            [0.0] * self.FFT_SIZE, min_update_period=self.MIN_UPDATE_PERIOD
        )

    @property
    def name(self) -> str:
        """str: Returns the name of the microphone ."""
        return self._name

    @property
    def instance(self) -> int:
        """int: Returns the instance number of the analog out component."""
        return self._instance

    @property
    def bins(self) -> int:
        """int: Returns the number of bins the analog out component."""
        return self.FFT_SIZE

    @property
    def fft_data(self) -> list[float]:
        return self._fft_data.value

    @fft_data.setter
    def fft_data(self, value: list[float]):
        self._fft_data.value = value

    def flush_stream(self):
        self._active_stream = None
        self._stream_data = []

    def open_stream(self, stream_id: int):
        if self._active_stream is not None:
            if self._active_stream == stream_id:
                logger.error(f"Stream with id <{stream_id}> is already open")
            else:
                logger.error(
                    f"Existing stream with id <{self._active_stream}> is already open. Closing and starting new stream with id <{stream_id}>"
                )
        self.flush_stream()
        self._active_stream = stream_id
        asyncio.create_task(self.stream_timeout())
        logger.debug(f"opened stream with id <{stream_id}>")

    def close_stream(self, stream_id: int):
        if self._active_stream is None:
            logger.error(
                f"Cannot close stream with id <{stream_id}>, no active stream exists"
            )
        elif self._active_stream != stream_id:
            logger.error(
                f"Cannot close stream with id <{stream_id}>, the current active stream has id <{self._active_stream}>"
            )

        if self._active_stream is not None:
            self._fft_data.value = (
                self._stream_data.copy()
            )  # self._stream_data[len(self._stream_data)//2:].copy()
            logger.debug(f"Closed active stream with id <{self._active_stream}>")
            self.flush_stream()

    def update_from_audio_data_cmd_start_message(self, msg: AudioDataCmd):
        self.open_stream(msg.stream_id)

    def update_from_audio_data_cmd_end_message(self, msg: AudioDataCmd):
        self.close_stream(msg.stream_id)

    def update_from_audio_data_message(self, msg: AudioData):
        if self._active_stream is None:
            logger.error("Recieved audio message with no active stream")
        else:
            for magnitude in msg.magnitudes:
                if isinf(magnitude):
                    self._stream_data.append(sys.float_info.max)
                else:
                    self._stream_data.append(magnitude)

    async def stream_timeout(self):
        stream_id = int(self._active_stream)
        await asyncio.sleep(self.STREAM_TIMEOUT)
        if self._active_stream is not None and self._active_stream == stream_id:
            logger.error(f"Stream with id <{stream_id}> has timedout")
            self.close_stream(stream_id)
