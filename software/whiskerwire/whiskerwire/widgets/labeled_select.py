from textual.widget import Widget
from textual.widgets import Select, Label
from textual.widgets._select import SelectType, NoSelection, BLANK, SelectCurrent
from typing import Iterable
from rich.console import RenderableType
import logging

logger = logging.getLogger("WhiskerWire")

class LabeledSelect(Widget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget
    
    def __init__(self, *children, name = None, id = None, classes = None, disabled = False, options: Iterable[tuple[RenderableType, SelectType]],
        prompt: str = "Select",
        allow_blank: bool = True,
        value: SelectType | NoSelection = BLANK,
        tooltip: RenderableType | None = None, label = ""):
        super().__init__(*children, name=name, id=id, classes=classes, disabled=disabled)
        self._label = Label(label, expand=True)
        self._select = Select(options=options, prompt=prompt, allow_blank=allow_blank, value=value, name=name, disabled=disabled, tooltip=tooltip)
        self._select.expand = True
        self._prompt = prompt

    def compose(self):
        yield self._label
        yield self._select

    @property
    def value(self) -> str | int:
        return self._select.value
    
    def set_value(self, value: str | int):
        self._select.value = value
        self._select.prompt = value
    
    def reset(self):
        self._select.clear()
        self._select.prompt = self._prompt
    
    def set_error_status(self, error: bool):
        if error:
            self._select.set_styles("background: red 20%;")
        else:
            self._select.set_styles("background: grey 10%;")