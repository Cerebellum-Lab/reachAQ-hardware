from textual.widgets import Button
from textual.widgets.button import ButtonVariant
from rich.console import RenderableType
from rich.text import TextType
from ..utils import get_logger

logger = get_logger()


class GlitchlessButton(Button):
    def __init__(
        self,
        label: TextType | None = None,
        variant: ButtonVariant = "default",
        *,
        name: str | None = None,
        id: str | None = None,
        classes: str | None = None,
        disabled: bool = False,
        tooltip: RenderableType | None = None,
        action=None
    ):
        """Create a Button widget.

        Args:
            label: The text that appears within the button.
            variant: The variant of the button.
            name: The name of the button.
            id: The ID of the button in the DOM.
            classes: The CSS classes of the button.
            disabled: Whether the button is disabled or not.
            tooltip: Optional tooltip.
            action: Action to be run on button press
        """
        super().__init__(
            label=label,
            variant=variant,
            name=name,
            id=id,
            classes=classes,
            disabled=disabled,
            tooltip=tooltip,
        )
        self.active_effect_duration = 0.001

        self.__action = action

    def press(self):
        """Animate the button and send the [Pressed][textual.widgets.Button.Pressed] message.

        Can be used to simulate the button being pressed by a user.

        Returns:
            The button instance.
        """
        if self.disabled or not self.display:
            return self
        # Manage the "active" effect:
        self._start_active_affect()

        # Perform the specified action, if it exists
        if self.__action is not None:
            self.__action()

        return self
