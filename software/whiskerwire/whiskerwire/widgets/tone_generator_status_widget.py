from textual.widgets import Static, Input
from .status_widget import StatusWidget
from .command_value_widget import CommandValueWidget
from ..model.models.driver_models.tone_generator.tone_generator_model import (
    ToneGeneratorModel,
)
from functools import partial
from time import time
from ..utils import get_logger

logger = get_logger()


# ToneGeneratorStatusWidget class for displaying and controlling a tone generator's frequency and duration.
# Inherits from StatusWidget, allowing for dynamic tone control.
class ToneGeneratorStatusWidget(StatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: ToneGeneratorModel, tone_write: callable, **kwargs):
        """
        Initialize the ToneGeneratorStatusWidget with a tone generator model and optional configurations.

        Args:
            model (ToneGeneratorModel): The data model containing tone generator attributes.
            **kwargs: Additional keyword arguments for widget customization.
        """
        self.model = model
        super().__init__(self.model.name, self.model.instance, **kwargs)

        self.tone_write = partial(tone_write, instance=self.model.instance)

        # Displays for showing frequency and remaining duration
        self.frequency_display = Static(
            f"Tone Frequency: {self.model.frequency}Hz",
            id="tone-generator-frequency-display",
            classes="status-display",
        )
        self.duration_display = Static(
            f"Time Remaining: {self.model.time_remaining}ms",
            id="tone-generator-duration-display",
            classes="status-display",
        )

        # CommandValueWidget for setting tone frequency and duration
        self.command_tone_widget = CommandValueWidget(
            "Command Tone", ["frequency", "duration"], action=self.command_tone
        )

        # Add command widget to hidable items, shown only when selected
        self.hidable_items.append(self.command_tone_widget)

        # Register callbacks to update frequency and duration displays when values change
        self.model._frequency.register_on_update(self.on_frequency_change)
        self.model._time_remaining.register_on_update(self.on_duration_change)

    def command_tone(self):
        command_value_dict = self.command_tone_widget.get_values()
        valid = True

        try:
            frequency = int(command_value_dict["frequency-input"])
            if self.model.is_valid_frequency(frequency):
                self.command_tone_widget.set_input_error_status(
                    "frequency-input", False
                )
            else:
                self.command_tone_widget.set_input_error_status("frequency-input", True)
                valid = False
        except:
            self.command_tone_widget.set_input_error_status("frequency-input", True)
            valid = False

        try:
            duration = int(command_value_dict["duration-input"])
            if self.model.is_valid_duration(duration):
                self.command_tone_widget.set_input_error_status("duration-input", False)
            else:
                self.command_tone_widget.set_input_error_status("duration-input", True)
                valid = False
        except:
            self.command_tone_widget.set_input_error_status("duration-input", True)
            valid = False

        if valid:
            self.tone_write(frequency=frequency, duration=duration)
            self.command_tone_widget.reset()

    def on_frequency_change(self):
        """
        Update the frequency display when the tone generator's frequency changes.
        """
        self.frequency_display.update(f"Tone Frequency: {self.model.frequency}Hz")

    def on_duration_change(self):
        """
        Update the duration display when the tone generator's remaining time changes.
        """
        self.duration_display.update(
            f"Time Remaining: {float(self.model.time_remaining)}ms"
        )

    def compose(self):
        """
        Compose the layout of the ToneGeneratorStatusWidget.
        Includes the tone generator name, frequency display, and duration display.

        Yields:
            Static and other widgets: The title, frequency, and duration displays, with command inputs if selected.
        """
        yield self.compose_title()  # Tone generator title
        yield self.frequency_display  # Frequency display
        yield self.duration_display  # Duration display

        if self.selected:
            yield self.command_tone_widget

    def __interpolate_time_remaining(self):
        if self.model.frequency != 0:
            self.model.time_remaining = min(
                self.model.time_remaining,
                max(
                    int(
                        self.model.time_remaining
                        - (
                            (time() - self.model._time_remaining.last_update_time)
                            * 1000.0
                        )
                    ),
                    0,
                ),
            )
            if self.model.time_remaining == 0:
                self.model.frequency = 0

    def on_mount(self):
        """
        Set up periodic interpolation to decrement the remaining duration of the tone.
        Called when the widget is mounted.
        """
        self.set_interval(
            float(self.model.INTERPOLATION_INTERVAL) / 1000.0,
            self.__interpolate_time_remaining,
        )
