class MicrophoneModel:
    """
    Model representing the microphone FFt driver, managing attributes such as
    name, instance, and the FFT results.
    """

    def __init__(self, name: str, instance: int, bins: int):
        """
        Initialize the MicrophoneModel with a name, instance, and number of FFT bins.

        Args:
            name (str): The name of the analog out component.
            instance (int): The instance number of the analog out component.
            bins (int): The number of bins produced by the FFT
        """
        # Read-only attributes
        self._name = name
        self._instance = instance
        self._bins = bins

        # Read-write attribute for the FFT data - FIXME: is float the right datatype?
        self._fft_data: list[float] = list()

        self._on_fft_data_change = None

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
        return self._bins

    @property
    def fft_data(self) -> list[float]:
        return self._fft_data

    @fft_data.setter
    def fft_data(self, value: list[float]):
        self._fft_data = value

    def update_from_message(self, msg):
        raise NotImplementedError(
            "update_from_message has not been implemented for the MicrophoneModel class"
        )

    def register_on_fft_data_change(self, callback: callable):
        if self._on_fft_data_change is not None:
            raise RuntimeError(
                "A callback has already been registered for on_fft_data_change"
            )

        self._on_fft_data_change = callback
