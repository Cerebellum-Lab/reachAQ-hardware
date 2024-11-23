from textual.widget import Widget
from textual.widgets import Input, Label

class LabeledInput(Widget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget
    
    def __init__(self, *children, name = None, id = None, classes = None, disabled = False, value = None, placeholder = "", highlighter = None, password = False, restrict = None, type = "text", max_length = 0, suggester = None, validators = None, validate_on = None, valid_empty = False, tooltip = None, label = ""):
        super().__init__(*children, name=name, id=id, classes=classes, disabled=disabled)
        self._label = Label(label, expand=True)
        self._input = Input(value, placeholder, highlighter, password, restrict=restrict, type=type, max_length=max_length, suggester=suggester, validators=validators, validate_on=validate_on, valid_empty=valid_empty, name=name, id=id, classes=classes, disabled=disabled, tooltip=tooltip)
        self._input.expand = True

    def compose(self):
        yield self._label
        yield self._input

    @property
    def value(self) -> str:
        return self._input.value
    
    def set_value(self, value: str):
        self._input.value = value
        
    def reset(self):
        self._input.value = ""
    
    def set_error_status(self, error: bool):
        if error:
            self._input.set_styles("background: red 20%;")
        else:
            self._input.set_styles("background: grey 10%;")