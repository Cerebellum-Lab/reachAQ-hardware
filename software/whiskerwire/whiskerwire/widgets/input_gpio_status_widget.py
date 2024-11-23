from .gpio_status_widget import GPIOStatusWidget
from ..model.models.driver_models.gpio.gpio_model import GPIOModel


# InputGPIOStatusWidget class for displaying the status of an input GPIO pin.
# Inherits from GPIOStatusWidget to represent and track input-specific GPIO status.
class InputGPIOStatusWidget(GPIOStatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: GPIOModel, **kwargs):
        """
        Initialize the InputGPIOStatusWidget with a GPIO model.

        Args:
            model (GPIOModel): The data model containing the GPIO pin's properties.
            **kwargs: Additional keyword arguments for widget customization.
        """
        # Initialize the parent GPIOStatusWidget with the GPIO model and optional settings
        super().__init__(model, **kwargs)
