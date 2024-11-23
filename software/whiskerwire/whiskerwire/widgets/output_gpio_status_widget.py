from .gpio_status_widget import GPIOStatusWidget
from .gpio_buttons import GPIOButtons
from model.models.driver_models.gpio.gpio_model import GPIOModel
from functools import partial

# OutputGPIOStatusWidget class for managing the state of an output GPIO pin.
# Inherits from GPIOStatusWidget, allowing control of the GPIO pin's state (LOW or HIGH).
class OutputGPIOStatusWidget(GPIOStatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: GPIOModel, gpio_write: callable, **kwargs):
        """
        Initialize the OutputGPIOStatusWidget with a GPIO model and optional configurations.
        
        Args:
            model (GPIOModel): The data model containing the GPIO pin's properties.
            **kwargs: Additional keyword arguments for widget customization.
        """
        # Initialize the parent widget with the GPIO model
        super().__init__(model, **kwargs)

        # Initialize GPIO buttons for setting the pin state (LOW or HIGH)
        self.gpio_buttons = GPIOButtons("GPIO", partial(gpio_write, gpio_idx=self.model.index))

        # Add the GPIO buttons to the list of hidable items, shown only when selected
        self.hidable_items.append(self.gpio_buttons)

    def compose(self):
        """
        Compose the layout of the OutputGPIOStatusWidget.
        Includes the GPIO pin title, state display, and control buttons if selected.
        
        Yields:
            Static and other widgets: The title, state display, and GPIO control buttons.
        """
        yield self.compose_title()  # GPIO pin title
        yield self.state_display  # GPIO state display

        # Display GPIO control buttons for setting the pin state if the widget is selected
        if self.selected:
            yield self.gpio_buttons.compose_gpio_buttons()
