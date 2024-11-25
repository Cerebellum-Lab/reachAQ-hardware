from textual.widget import Widget
from textual.containers import Horizontal
from ..utils import to_valid_identifier
from .glitchless_button import GlitchlessButton
from functools import partial


# GPIOButtons class for controlling a GPIO pin state with two buttons (LOW and HIGH)
class GPIOButtons(Widget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling this widget

    def __init__(self, widget_name: str, gpio_write: callable, **kwargs):
        """
        Initialize the GPIOButtons widget with a widget name and optional configuration.

        Args:
            widget_name (str): The name of the widget, used for generating a unique ID.
            **kwargs: Additional keyword arguments for widget customization.
        """
        # Initialize the widget with a unique, valid ID derived from the widget name
        super().__init__(id=to_valid_identifier(widget_name), **kwargs)

        # Define buttons for setting GPIO state to LOW or HIGH
        self.low_button = GlitchlessButton(
            "LOW", classes="gpio-button", action=partial(gpio_write, state=False)
        )
        self.high_button = GlitchlessButton(
            "HIGH", classes="gpio-button", action=partial(gpio_write, state=True)
        )

    def compose_gpio_buttons(self):
        """
        Compose the layout of the GPIOButtons widget, placing LOW and HIGH buttons
        in a horizontal container.

        Returns:
            Horizontal: A container holding the LOW and HIGH buttons, labeled "Set State".
        """
        gpio_buttons_container = Horizontal(
            self.low_button, self.high_button, classes="gpio-button-container"
        )
        gpio_buttons_container.border_title = "Set State"  # Title for the container
        return gpio_buttons_container
