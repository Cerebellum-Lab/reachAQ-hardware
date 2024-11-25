from ..model.models.driver_models.rgb_led.rgb_led_model import RGBLEDModel
from .status_widget import StatusWidget
from textual.widgets import Static
from .glitchless_button import GlitchlessButton
from textual.containers import Container
from .command_value_widget import CommandValueWidget
from textual.events import Click
from ..utils import get_logger

logger = get_logger()


class RGBLEDStatusWidget(StatusWidget):
    class InternalStatic(Static):
        def __init__(
            self,
            master: StatusWidget,
            renderable="",
            *,
            expand=False,
            shrink=False,
            markup=True,
            name=None,
            id=None,
            classes=None,
            disabled=False,
        ):
            super().__init__(
                renderable,
                expand=expand,
                shrink=shrink,
                markup=markup,
                name=name,
                id=id,
                classes=classes,
                disabled=disabled,
            )
            self.master = master

        def on_click(self, event: Click):
            self.master.on_click(event)

    def __init__(self, model: RGBLEDModel, write: callable, **kwargs):
        self.model = model

        super().__init__(self.model.name, 0, **kwargs)

        self.write = write

        self.color_display_widget = GlitchlessButton(
            "",
            disabled=False,
            action=self.__toggle_selected,
            classes="color-display-widget",
        )
        self.color_display_widget.active_effect_duration = 0.0001

        self.red_display_widget = self.InternalStatic(
            self, f"Red: {self.model.red}%", classes="rgb-status-display"
        )
        self.green_display_widget = self.InternalStatic(
            self, f"Green: {self.model.green}%", classes="rgb-status-display"
        )
        self.blue_display_widget = self.InternalStatic(
            self, f"Blue: {self.model.blue}%", classes="rgb-status-display"
        )
        """self.red_display_widget = Static(f"Red: {self.model.red}%", classes="status-display")
        self.green_display_widget = Static(f"Green: {self.model.green}%", classes="status-display")
        self.blue_display_widget = Static(f"Blue: {self.model.blue}%", classes="status-display")"""

        self.command_value_widget = CommandValueWidget(
            "Set Color",
            ["Red", "Green", "Blue"],
            orientation=CommandValueWidget.Orientation.VERTICAL,
            action=self.command_rgb_led,
        )

        self.items.append(self)
        self.hidable_items.append(self.command_value_widget)

        self.model._red.register_on_update(self.on_red_change)
        self.model._green.register_on_update(self.on_green_change)
        self.model._blue.register_on_update(self.on_blue_change)

    def command_rgb_led(self):
        values = self.command_value_widget.get_values()
        valid = True

        try:
            red = int(values["red-input"])
            if self.model.is_valid_value(red):
                self.command_value_widget.set_input_error_status("red-input", False)
            else:
                self.command_value_widget.set_input_error_status("red-input", True)
                valid = False
        except:
            self.command_value_widget.set_input_error_status("red-input", True)
            valid = False

        try:
            green = int(values["green-input"])
            if self.model.is_valid_value(green):
                self.command_value_widget.set_input_error_status("green-input", False)
            else:
                self.command_value_widget.set_input_error_status("green-input", True)
                valid = False
        except:
            self.command_value_widget.set_input_error_status("green-input", True)
            valid = False

        try:
            blue = int(values["blue-input"])
            if self.model.is_valid_value(blue):
                self.command_value_widget.set_input_error_status("blue-input", False)
            else:
                self.command_value_widget.set_input_error_status("blue-input", True)
                valid = False
        except:
            self.command_value_widget.set_input_error_status("blue-input", True)
            valid = False

        if valid:
            self.write(red=red, green=green, blue=blue)
            self.command_value_widget.reset()

    def on_red_change(self):
        self.red_display_widget.update(f"Red: {self.model.red}%")
        self.on_color_change()

    def on_green_change(self):
        self.green_display_widget.update(f"Green: {self.model.green}%")
        self.on_color_change()

    def on_blue_change(self):
        self.blue_display_widget.update(f"Blue: {self.model.blue}%")
        self.on_color_change()

    def on_color_change(self):
        def rgb_to_hex(r, g, b):
            return "#{:02x}{:02x}{:02x}".format(int(r), int(g), int(b))

        self.color_display_widget.set_styles(
            f"background: {rgb_to_hex(self.model.red * (255.0 / 100.0), self.model.green * (255.0 / 100.0), self.model.blue * (255.0 / 100.0))} 100%;"
        )

    def compose_title(self):
        """
        Compose and return the title widget, displayed as the header of this StatusWidget.

        Returns:
            Static: A styled header widget with the widget's display name.
        """
        return self.InternalStatic(
            self, f"[bold]{self.widget_name}[/bold]", classes="rgb-status-widget-title"
        )

    def compose(self):
        if not self.selected:
            yield self.compose_title()
            yield self.color_display_widget
            yield self.red_display_widget
            yield self.green_display_widget
            yield self.blue_display_widget
            """yield Container(self.compose_title(),
                            self.color_display_widget,
                            self.red_display_widget,
                            self.green_display_widget,
                            self.blue_display_widget,
                            classes="rgb-led-container"
            )"""
        else:
            yield self.compose_title()
            yield self.color_display_widget
            yield self.red_display_widget
            yield self.green_display_widget
            yield self.blue_display_widget
            yield self.command_value_widget

            """yield Container(self.compose_title(),
                            self.color_display_widget,
                            self.red_display_widget,
                            self.green_display_widget,
                            self.blue_display_widget,
                            self.command_value_widget,
                            classes="rgb-led-container"
            )"""

    def on_mount(self):
        self.on_color_change()

    def on_click(self, event: Click) -> None:
        """
        Handle click events on the widget, toggling its selection state.
        This re-renders the widget to update any visual indications of selection.

        Args:
            event (Click): The click event triggering the handler.
        """
        # Prevent clicks on hidable items from affecting selection state
        if self.command_value_widget.region.contains(event.screen_x, event.screen_y):
            return

        # Toggle the selection state and recompose the widget layout if changed
        self.__toggle_selected()

    def __toggle_selected(self):
        self.selected = not self.selected
        self.refresh(recompose=True)
