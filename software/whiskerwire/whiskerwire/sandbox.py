from textual.app import App, ComposeResult
from widgets.command_value_widget import CommandValueWidget


class Sandbox(App):
    CSS_PATH = "static/styles.tcss"

    def __init__(self, driver_class=None, css_path=None, watch_css=False):
        self.theme = "textual-dark"
        super().__init__(driver_class, css_path, watch_css)
        self.cvw = CommandValueWidget("Test", ["value1", "value2"], action=self.on_send)

        for k, v in self.get_css_variables().items():
            with open("test2.txt", "w") as f:
                f.write(f"{k}, {v}")

    def compose(self):
        yield self.cvw

    def on_send(self):
        values = self.cvw.get_values()
        valid = True
        try:
            if int(values["value1-input"]) > 5:
                self.cvw.set_input_error_status("value1-input", True)
                valid = False
            else:
                self.cvw.set_input_error_status("value1-input", False)
        except Exception as e:
            self.cvw.set_input_error_status("value1-input", True)
            valid = False

        try:
            if int(values["value2-input"]) > 10:
                self.cvw.set_input_error_status("value2-input", True)
                valid = False
            else:
                self.cvw.set_input_error_status("value2-input", False)
        except Exception as e:
            with open("test.txt", "w") as f:
                f.write(f"{e}")
            self.cvw.set_input_error_status("value2-input", True)
            valid = False

        if valid:
            self.cvw.reset()


if __name__ == "__main__":
    Sandbox(watch_css=True).run()
