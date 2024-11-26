# WhiskerWire
A diagnostic tool for observing and manipulating the state of the MouseGYM modules.

## Creating a New Widget
The best process for creating a new widget is somewhat non-linear but ultimately straightforward. It begins from the bottom up, and then traverses from the top down.

<details>
<summary><b>Step 1: Create the Model</b></summary>

Begin by creating a new directory within the `model/models/driver_models/` directory, `model/models/driver_models/<new_model_name>`. Within this directory create a `<new_model_name>.py` file and define a new model class which represents your desired data structure. For fields which should be reflected by the widget on update/change, wrap their value in the `Watchable` constructor - optionally providing a minimum update period before which new updates will not be rendered to the screen, and/or an `only_on_change` flag which indicates whether any callbacks registered to the `Watchable` should be called only if the value changes or regardless of change, on assignment.

It is highly recommended that you prepend `Watchable` field names with an underscore (`self._example = Watchable(0.0)`), and expose them via properties - this is because the actual value of the `Watchable` object is stored in the `Watchable.value` field. Thus, providing a getter and setter which returns or sets this `value` field removes the possibility of reassigning the `Watchable` object instead of its value.

If there can/will be multiple instances of the new widget per module, additionally create a plural model file, `<new_model_name>s.py` (note the plural suffix `s`), which defines a model that accepts a list of arbitrary length of the individual models. These individual models should be exposed through a convenient interface - most often as a dictionary whose key-value pairs consist of an instance, index, or name corresponding to the individual model.

Each model, or at least each "plural" or "singleton" model, should expose an `update_from_message()` method (or an `update_from_<message type>_message()` if it accepts multiple message types) which accepts the corresponding message payload, and updates the model's fields with the new values.

<details>
<summary>GPIOModel Example</summary>

```python
# gpio_model.py

class GPIODirection(Enum):
    """Enum representing the direction of a GPIO pin."""

    INPUT = 0
    OUTPUT = 1


class GPIOModel:
    """
    Model representing a GPIO (General Purpose Input/Output) pin, including its name, index,
    direction, and current state.
    """

    def __init__(self, name: str, index: int, direction: GPIODirection):
        """
        Initialize the GPIOModel with a name, index, and direction.

        Args:
            name (str): The name of the GPIO pin.
            index (int): The index of the GPIO pin.
            direction (GPIODirection): The direction of the GPIO pin, either INPUT or OUTPUT.
        """
        super().__init__()

        # Read-only attributes
        self._name = name  # Name of the GPIO pin
        self._index = index  # Index of the GPIO pin
        self._direction = direction  # Direction of the GPIO pin (INPUT or OUTPUT)

        # Read-write attribute representing the current state of the GPIO pin
        self._state = Watchable(False)

    @property
    def name(self) -> str:
        """str: Returns the name of the GPIO pin."""
        return self._name

    @property
    def index(self) -> int:
        """int: Returns the index of the GPIO pin."""
        return self._index

    @property
    def direction(self) -> GPIODirection:
        """GPIODirection: Returns the direction of the GPIO pin (INPUT or OUTPUT)."""
        return self._direction

    @property
    def state(self) -> bool:
        """bool: Returns the current state of the GPIO pin (True for HIGH, False for LOW)."""
        return self._state.value

    @state.setter
    def state(self, value: bool):
        """
        Set the state of the GPIO pin and trigger reactive updates if registered.

        Args:
            value (bool): The new state of the GPIO pin (True for HIGH, False for LOW).
        """
        self._state.value = value
```
</details>

<details>
<summary>GPIOSModel Example</summary>

```python
# gpios_model.py

class GPIOSModel:
    """
    Model representing a collection of GPIO pins, providing methods to access individual GPIOs by name or index
    and to update GPIO states based on incoming messages.
    """

    def __init__(self, gpios: list[GPIOModel]):
        """
        Initialize the GPIOSModel with a list of GPIO models, ensuring no conflicts in names or indices.

        Args:
            gpios (list[GPIOModel]): A list of GPIOModel instances representing individual GPIO pins.

        Raises:
            SystemExit: If duplicate GPIO names or indices are found.
        """
        # Ensure that no conflicts exist between GPIO names and indices
        names = [gpio.name for gpio in gpios]
        indices = [gpio.index for gpio in gpios]

        if len(names) != len(set(names)):
            logger.fatal("Duplicate GPIO names found in gpios")
            sys.exit(1)

        if len(indices) != len(set(indices)):
            logger.fatal("Duplicate GPIO indices found in gpios")
            sys.exit(1)

        # Store GPIOs in a dictionary by their index for easy access
        self.gpios: dict[int, GPIOModel] = {gpio.index: gpio for gpio in gpios}

    @property
    def instance(self) -> int:
        """int: Returns the instance identifier for the GPIO collection, assumed to be 0."""
        return 0

    def get_gpio_by_index(self, index: int) -> GPIOModel | None:
        """
        Retrieve a GPIOModel by its index.

        Args:
            index (int): The index of the GPIO.

        Returns:
            GPIOModel | None: The GPIO model if found, otherwise None.
        """
        try:
            return self.gpios[index]
        except KeyError:
            logger.error(
                f"Failed to get GPIO with index <{index}> - a GPIO with the specified index does not exist"
            )
            return None

    def get_gpio_by_name(self, name: str) -> GPIOModel | None:
        """
        Retrieve a GPIOModel by its name.

        Args:
            name (str): The name of the GPIO.

        Returns:
            GPIOModel | None: The GPIO model if found, otherwise None.
        """
        for gpio in self.gpios.values():
            if gpio.name == name:
                return gpio
        logger.error(
            f"Failed to get GPIO with name <'{name}'> - a GPIO with the specified name does not exist"
        )
        return None

    def get_gpio(self, specifier: int | str) -> GPIOModel | None:
        """
        Retrieve a GPIOModel by either index or name.

        Args:
            specifier (int | str): The index or name of the GPIO.

        Returns:
            GPIOModel | None: The GPIO model if found, otherwise None.

        Raises:
            TypeError: If the specifier is not an int or str.
        """
        if isinstance(specifier, int):
            return self.get_gpio_by_index(specifier)
        elif isinstance(specifier, str):
            return self.get_gpio_by_name(specifier)

        raise TypeError("Failed to get GPIO - specifier must be an int or str")

    def get_state(self, specifier: int | str) -> bool | None:
        """
        Get the state of a GPIO pin.

        Args:
            specifier (int | str): The index or name of the GPIO.

        Returns:
            bool | None: The state of the GPIO if found, otherwise None.
        """
        gpio = self.get_gpio(specifier)
        if gpio is None:
            logger.error(f"Failed to get state of GPIO with specifier <{specifier}>")
            return None
        return gpio.state

    def set_state(self, specifier: int | str, state: bool) -> None:
        """
        Set the state of a GPIO pin.

        Args:
            specifier (int | str): The index or name of the GPIO.
            state (bool): The new state to set for the GPIO.
        """
        gpio = self.get_gpio(specifier)
        if gpio is None:
            logger.error(f"Failed to set state of GPIO with specifier <{specifier}>")
        else:
            gpio.state = state

    def update_from_message(self, msg: GPIORead):
        """
        Update the states of all GPIO pins based on a GPIORead message.

        Args:
            msg (GPIORead): The message containing state information for each GPIO pin.
        """
        # `instance` is unused as it's assumed only one instance of generic GPIOs exists
        state = msg.state

        # Update each GPIO's state based on the message data
        for index, gpio in self.gpios.items():
            gpio.state = bool((state >> index) & 0b1)
            
```
</details>
</details>

<details>
<summary><b>Step 2: Create the Widget</b></summary>

The next step is to create the widget which will act as both the view and controller for your model. The widget should inherit from `textual.widget.Widget`, or one of the existing widget base classes defined in the `widgets/` directory - such as `StatusWidget` or `SensorStatusWidget` - and should likewise be located within the `widgets/` directory. It should take a model of the corresponding type as an argument to its constructor, which it stores as a field and uses to populate its contents - this includes everything from one time assignments like the widgets name and instance number, to the values displayed by the widget to reflect the components state.

For each `Watchable`, `<var>`, present in the model, a corresponding `on_<var>_change(self)` method should be defined in the widget, which is to be called whenever the `Watchable`'s update creteria are met. Preferably within the constructor, these callbacks can then be registered to the `Watchable` fields of the model. This is done by accessing the fields themselves (not through the property's getter) and calling the `register_on_update()` method, like so:
```python
# Register a callback to update the displayed value when model's value changes
self.model._value_mv.register_on_update(self.on_value_mv_change)
```

The constructor should also accept and store `callable`s corresponding to any `pyjerrycan` interactions associated with the widget's intended behavior. These `callable`s should make use of `functools`' partial function application capabilities to heirarchically abstract the function parameters and expose a minimal API to the corresponding widget - more on this in step 4.

If the widget should be expandable on click (must be an instance of the `StatusWidget` class or any of its children), any child widgets of the new widget which are hidden when in the collapsed state should be appended to the widget's `hidable_items` list, while those present regardless of state should be appended to the widgets `items` list.

To dictate how the widget should be displayed, override the `compose()` method and `yield` the child widgets, either individually or within one or several Textual `Container`, `Grid`, `Horizontal`, or `Vertical` objects. If the widget is expandable, conditionally `yield` the hidable items based on the value of the `selected` instance variable

If there can/will be multiple instances of the widget and a "plural" model has been defined, then an additional widget should be created within the `dashboards` directory, in a similar fashion, which accepts an instance of the "plural" model as an argument to its constructor - in addition to any JerryCAN `callable`s needed to expose the desired behavior. Dashboards should extended the `StatusWidgetDashboard` class, store the corresponding "plural" model as a field, and either create the individual widgets from the contents this model.

<details>
<summary>GPIOStatusWidget Example</summary>

```python
# gpio_status_widget.py

# A mapping to convert numeric GPIO states (0 or 1) to descriptive labels ("LOW" or "HIGH")
STATE_MAP = ["LOW", "HIGH"]


# GPIOStatusWidget class for displaying the state of a GPIO pin.
# Inherits from StatusWidget and updates dynamically based on the GPIO model state.
class GPIOStatusWidget(StatusWidget):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling this widget

    def __init__(self, model: GPIOModel, **kwargs):
        """
        Initialize the GPIOStatusWidget with a model and optional widget configuration.

        Args:
            model (GPIOModel): The data model containing the GPIO pin's properties and state.
            **kwargs: Additional keyword arguments for widget customization.
        """
        self.model = model
        # Initialize the parent widget with the GPIO name and instance index
        super().__init__(self.model.name, self.model.index, **kwargs)

        # Static display for showing the GPIO state (LOW or HIGH)
        self.state_display = Static(
            "State: LOW", id="gpio-state-display", classes="status-display"
        )

        # Register a callback to update the display when the model's state changes
        self.model._state.register_on_update(self.on_state_change)

    def on_state_change(self):
        """
        Callback function to update the GPIO state display when the `_state` attribute
        in the model changes.
        """
        self.state_display.update(f"State: {STATE_MAP[self.model.state]}")

    def compose(self):
        """
        Compose the layout of the GPIOStatusWidget.
        Includes the GPIO pin title and the current state display.

        Yields:
            Static: The title and state display widgets for the GPIO pin.
        """
        yield self.compose_title()  # GPIO pin title
        yield self.state_display  # Current state display     
```
</details>

<details>
<summary>GPIODashboard Example</summary>

```python
# gpio_dashboard.py

# GPIODashboard class for managing and displaying GPIO states and controls
class GPIODashboard(StatusWidgetDashboard):
    """
    A dashboard for managing and displaying GPIO states and controls.
    Inherits from StatusWidgetDashboard.
    """

    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, model: GPIOSModel, gpio_write: callable, **kwargs):
        """
        Initialize the GPIODashboard.

        Args:
            model (GPIOSModel): The model containing GPIOs for input/output status.
            gpio_write (callable): Function to write/send GPIO state changes, typically via CAN.
            **kwargs: Additional keyword arguments for the parent class.
        """
        self.model = model
        self.gpio_write = gpio_write

        # Separate GPIO widgets based on direction (input/output)
        self.input_gpios: dict[int, InputGPIOStatusWidget] = {}
        self.output_gpios: dict[int, OutputGPIOStatusWidget] = {}

        for index, gpio in self.model.gpios.items():
            if gpio.direction == GPIODirection.INPUT:
                self.input_gpios[index] = InputGPIOStatusWidget(gpio)
            elif gpio.direction == GPIODirection.OUTPUT:
                self.output_gpios[index] = OutputGPIOStatusWidget(
                    gpio, gpio_write=partial(gpio_write, instance=self.model.instance)
                )

        # Gather all GPIO widgets for dashboard display
        self.items = list(self.input_gpios.values()) + list(self.output_gpios.values())
        super().__init__("GPIO", self.items, **kwargs)

    def compose(self):
        """
        Compose the layout of the GPIODashboard, including input and output GPIO sections.
        Arranges inputs and outputs in separate containers within the dashboard.

        Yields:
            Static: Title for the dashboard.
            Horizontal: Container holding both input and output GPIO sections.
        """
        yield Static(
            f"[bold]{self.dashboard_name}[/bold]", classes="status-dashboard-title"
        )

        # Create sub-dashboard sections for output and input GPIOs
        sub_dashboards = []

        if self.output_gpios:
            sub_dashboards.append(
                Vertical(
                    Static(f"[bold]Outputs[/bold]", classes="status-dashboard-title"),
                    Grid(
                        *self.output_gpios.values(),
                        classes="output-gpio-dashboard-container",
                    ),
                    classes="gpio-dashboard-container",
                )
            )

        if self.input_gpios:
            sub_dashboards.append(
                Vertical(
                    Static(f"[bold]Inputs[/bold]", classes="status-dashboard-title"),
                    Grid(
                        *self.input_gpios.values(),
                        classes="input-gpio-dashboard-container",
                    ),
                    classes="gpio-dashboard-container",
                )
            )

        # Arrange input and output sections horizontally within the dashboard
        yield Horizontal(*sub_dashboards, classes="dashboard-container")
```
</details>
</details>

<details>
<summary><b>Step 3: Update the Module Model</b></summary>

Within the `model/models/` directory, add your model to the corresponding module model (`MagnetModuleModel` or `PelletModuleModel`) or to the base `ModuleModel` class if both modules possess instances of the driver/component associated with your new model. This is where the read-only model values like name, instance number, index, etc. are explicitly defined for a given module.

Within the corresponding module model (Magnet, Pellet, or base), update the `process_message()` method to handle the desired message type and pass it to the corresponding child-model's `update_from_message()` method.

<details>
<summary>ModuleModel Example</summary>

```python
# module_model.py

def process_message(self, msg: JerryCANMsg) -> JerryCANCmdType:
    """
    Process incoming JerryCAN messages to update GPIO and servo states.

    Args:
        msg (JerryCANMsg): The incoming message from the CAN bus.

    Returns:
        JerryCANCmdType: The type of the message processed.
    """
    if msg.type == JerryCANCmdType.GPIO_READ:
        # Update GPIO states from the message data
        self.gpios.update_from_message(msg.gpio_read)
    elif msg.type == JerryCANCmdType.SERVO_STATUS:
        # Update servo status from the servo status message
        self.servos.update_from_servo_status_message(msg.servo_status)
    elif msg.type == JerryCANCmdType.CFG_RESPONSE:
        # Update configuration settings for servos from the configuration response
        cfg_response = msg.cfg_response
        if cfg_response.type == JerryCANCfgMsg.Type.SERVO:
            self.servos.update_from_cfg_message(cfg_response.servo)

    return msg.type
```
</details>
</details>

<details>
<summary><b>Step 4: Update the Module Dashboard</b></summary>

Within the `module_dashboards/` directory, add an instance of your dashboard or "singleton" status widget as a member of the corresponding module dashboard. Here we have access to the module's associated model, which now contains your new "plural" or "singleton" model as a member - grab your new model from the module's model and pass it to the dashboard/"singleton" widget's constructor.

Here is also where we gain temporary access to the JerryCAN instance. As such, in addition to providing the model, any `callable` arguments accepted by your widget's constructor can be passed through the first level of abstraction - use `functools`' `partial` function to provide the `dst_id` parameter to the desired functions. The resulting function is then passed to the constructor - potentially being further abstracted (by supplying instance-specific parameters like index, instance number, and any other values which remain constant for a given instance) before being passed down to the individual widgets involved. In this way, for example, each widget need not know the CAN ID of the module to which it belongs, only the parameters specific to the behavior it exposes.

Remember to also add your new dashboard to the `compose_dashboard()` method so that it is rendered with the module dashboard

<details>
<summary>MagnetModuleDashboard Example</summary>

```python
# magnet_module_dashboard.py

# MagnetModuleDashboard class for managing and displaying the status of a magnet module.
# Inherits from ModuleDashboard and includes GPIO, servo, and sensor sub-dashboards.
class MagnetModuleDashboard(ModuleDashboard):
    CSS_PATH = "static/styles.tcss"  # Path to the CSS file for styling the widget

    def __init__(self, jc: JerryCAN, model: MagnetModuleModel, **kwargs):
        """
        Initialize the MagnetModuleDashboard with JerryCAN, a model, and optional configurations.

        This dashboard manages multiple sub-dashboards, including servo, GPIO, sensor, and status dashboards.

        Args:
            jc (JerryCAN): An instance of JerryCAN for handling CAN communication.
            model (MagnetModuleModel): The data model for the magnet module.
            **kwargs: Additional keyword arguments passed to the parent class.
        """
        self.model = model

        # Initialize the GPIODashboard with GPIOs and configure write actions using JerryCAN
        self.gpio_dashboard = GPIODashboard(
            self.model.gpios, gpio_write=partial(jc.GPIOWrite, self.model.dst_id)
        )

        # Initialize the ServoDashboard with two servo status widgets and actions via JerryCAN
        self.servo_dashboard = ServoDashboard(
            self.model.servos,
            write_config=partial(jc.ServoCfgWrite, self.model.dst_id),
            read_config=partial(jc.ServoCfgRead, self.model.dst_id),
            move=partial(jc.ServoMove, dst_id=self.model.dst_id),
        )

        # Initialize the SensorDashboard with temperature, humidity, pressure, and load cell widgets
        self.sensor_dashboard = SensorDashboard(
            [
                TemperatureStatusWidget(self.model.temperature_sensor),
                HumidityStatusWidget(self.model.humidity_sensor),
                PressureStatusWidget(
                    self.model.pressure_sensor,
                    tare=partial(jc.PressureSensorTare, self.model.dst_id),
                ),
                LoadCellStatusWidget(
                    self.model.load_cell_sensor,
                    tare=partial(jc.LoadCellTare, self.model.dst_id),
                ),
            ]
        )

        # Call the parent constructor to initialize the module dashboard with all sub-dashboards
        super().__init__(
            "Magnet",
            self.model,
            [self.gpio_dashboard, self.servo_dashboard, self.sensor_dashboard],
            **kwargs,
        )

    def compose_dashboard(self):
        """
        Compose the layout of the MagnetModuleDashboard, including servo, GPIO, and sensor dashboards.

        Returns:
            Grid: A grid layout containing all sub-dashboards.
        """
        return Grid(
            self.servo_dashboard,
            self.gpio_dashboard,
            self.sensor_dashboard,
            id=f"{self.module_name.lower()}-module-dashboard-container",
            classes="module-dashboard-container",
        )
```
</details>
</details>

<details>
<summary><b>Step 5: Update the `config.jsonc` file (optional)</b></summary>

If your widget possesses global read-only settings which are shared by all of its instances, you can add these settings to the `config.jsonc` file - which is loaded into a global `settings` dictionary at startup - and acquire them by grabbing the global `settings` dictionary with the `get_settings()` function, and indexing the desired settings via the chosen JSON field names.

<details>
<summary>Humidity Sensor Settings Example</summary>

```javascript
/* config.jsonc */

/* Humidity Sensor Settings */
"Humidity Sensor": {
    /*
    * The minimum period of time between display updates in seconds.
    * If 0, updates will occur upon the reception of each associated message.
    */
    "Min Update Period": 0.750,
    "Graph": {
    /*
    * The maximum number of data points to display in the graph.
    */
    "Max Data Points": 30,
    /*
    * The minimum value for the graph's Y-axis
    */
    "Y Min": 0.0,
    /*
    * The maximum value for the graph's Y-axis
    */
    "Y Max": 100.0
    }
}
```
</details>
</details>

<details>
<summary><b>Step 6: Update the `styles.tcss` file (optional)</b></summary>

If you aren't satisfied with the defualt layout, spacing, color, or dimensions of your widget and/or its contents, you can use scoped selectors to adjust its various style properties. Textual uses its own flavor of `CSS` which it calls `TCSS`. `TCSS` shares a lot of syntax and style properties with `CSS`, but there are marked differences which can be quite frustrating to identify through trial and error - it is strongly recommended to "forget everything you know about `CSS`" and diligently utilize [Textual's `TCSS` Guide](https://textual.textualize.io/guide/CSS/).

The WhiskerWire application is run with the `watch_css` flag enabled. This allows changes to the `styles.tcss` file to be reflected in the running application with each save operation - however, if invalid `TCSS` code is present when the file is saved, no update or error will be displayed. If you observe this ocurring and suspect invalid `TCSS` code as the culprit, closing and reopening the application will result in an exception on startup which delineates the source and nature of the error.

#### Selectors
A widget can be specified through one of three selectors: <i>type</i>, `id`, or class-name. 

##### <i>Type</i> Selector
The <i>type</i> selector matches the name of the Python class. Consider the following widget class:
```python
from textual.widgets import Static

class Alert(Static):
    pass
```
Alert widgets may be styled with the following CSS (to give them a red border):
```css
Alert {
  border: solid red;
}
```

The <i>type</i> selector will also match a widget's base classes. Consequently, a `Static` selector will also style the button because the `Alert` (Python) class extends `Static`.
```css
Static {
  background: blue;
  border: rounded green;
}
```

##### `id` Selector
Every Widget can have a single `id` attribute, which is set via the constructor. The ID should be unique to its container.

Here's an example of a widget with an ID:
```python
yield Button(id="next")
```
You can match an ID with a selector starting with a hash (`#`). Here is how you might draw a red outline around the above button:
```css
#next {
  outline: red;
}
```
A Widget's `id` attribute can not be changed after the Widget has been constructed.

#### Class-name Selector
Every widget can have a number of class names applied. The term "class" here is borrowed from web CSS, and has a different meaning to a Python class. You can think of a CSS class as a tag of sorts. Widgets with the same tag will share styles.

CSS classes are set via the widget's `classes` parameter in the constructor. Here's an example:
```python
yield Button(classes="success")
```
This button will have a single class called `"success"` which we could target via CSS to make the button a particular color.

You may also set multiple classes separated by spaces. For instance, here is a button with both an error class and a disabled class:
```python
yield Button(classes="error disabled")
```
To match a Widget with a given class in CSS you can precede the class name with a dot (.). Here's a rule with a class selector to match the `"success"` class name:
```css
.success {
  background: green;
  color: white;
}
```
You can apply a class name to any widget, which means that widgets of different types could share classes.

<details>
<summary>GPIODashboard TCSS Example</summary>

```scss
/* styles.tcss */

GPIODashboard {
    .gpio-dashboard-container {
        border: round $border_color;
        height: auto;
        width: auto;

        .input-gpio-dashboard-container {
            grid-size: 1;
            grid-rows: auto;
            grid-columns: auto;
            width: auto;
            height: auto;
        }

        .output-gpio-dashboard-container {
            grid-size: 1;
            grid-rows: auto;
            grid-columns: auto;
            width: auto;
            height: auto;
        }
    }
}
```
</details>
</details>

## Utility Widgets
Not all widgets residing in the `widgets/` directory are intended to be used as standalone widgets, they are instead meant to be building blocks for constructing other more complex widgets. Such widgets include:
- `LabeledInput`
    - An `Input` widget which also possess a title/label to indicate its purpose
- `LabeledSelect`
    - A `Select` widget which also possess a title/label to indicate its purpose
- `GlitchlessButton`
    - A `Button` widget whose `active_effect_duration` attribute is changed from the default value of `200ms` to `1ms` on initialization. This is useful for combatting a bug which occurs when a widget is unmounted while the active effect is in progress
- `GraphWidget`
    - A widget used to display a graphical representation of data
- `CommandValueWidget`
    - A widget consisting of an arbitrary number of inputs and single button, for the purpose of commanding values
- `GPIOButtons`
    - A widget used to command GPIO pin state, consisting of `HIGH` and `LOW` buttons
- `JogButtons`
    - A widget used to command a jog action, consisting of four buttons `<<`, `<`, `>`, and `>>`
