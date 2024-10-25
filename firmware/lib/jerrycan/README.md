# JerryCAN Library

The JerryCAN library manages the CAN-based communication between individual MouseGYM modules and a host system (if present). It provides a modular way to manage multiple system events, including GPIO management, sensor polling, motor control, and periodic status reporting, with flexible configuration options for each component's transmission rate. Transmission rates for all timers are configurable via Kconfig, enabling dynamic adaptation to various application needs. JerryCAN uses asynchronous polling events for handling incoming and outgoing transmissions, allowing for efficient use of system resources.

## Messages

Message types are defined in `jerrycan_types.h`. 

### Adding a New Message 

#### Provide a Unique Message Identifier

In `jerrycan_types.h`, add a new enum value of the form `JERRYCAN_CMD_*` to `enum jerrycan_cmd_type_t`.

#### Define the Message Structure

In `jerrycan_types.h`, define a new struct of the form `jerrycan_cmd_*_t` and provide a build time assertion `BUILD_ASSERT(sizeof(jerrycan_cmd_*_t) == <MESSAGE_SIZE>, "jerrycan_cmd_*_t should be <MESSAGE_SIZE>")` to ensure message validity.

~TODO: Update to reflect variable size messages upon merge ^

Then add the `jerrycan_cmd_*_t` type as a new filed of the anonymous union in the `jerrycan_msg_t` struct.

### Message Modules

Message modules are responsible for handling incoming and outgoing messages related to specific system components, such as sensors, motors, GPIO, and general status updates. Each module operates within the `modules/` directory, where it defines the message processing logic for its designated component. This structure allows JerryCAN to efficiently manage diverse data sources by modularizing the code base, utilizing conditional compilation, and simplifying the addition of new message types as needed.

#### Key Message Modules

- **Analog Out Module (`modules/analog.c`)**: Manages the transmission and reception of messages related to analog out.
- **GPIO Module (`modules/gpio.c`)**: Manages the transmission and reception of GPIO-related messages, enabling the library to send and receive state updates for digital pins used as inputs or outputs.
- **Heartbeat Module (`modules/heartbeat.c`)**: Manages the reception of a periodic heartbeat signal, represented by an LED blink, to indicate the system’s operational status.
- **Load Cell Module (`modules/load_cell.c`)**: Manages the transmission and reception of load cell related messages.
- **Pressure Sensor Module (`modules/pressure.c`)**: Manages the transmission and reception pressure sensor dat.
- **Servo Module (`modules/servo.c`)**: Manages the transmission and reception of servo-related messages.
- **Status Module (`modules/status.c`)**: Manages the transmission of periodic system status messages.
- **Stepper Module (`modules/stepper.c`)**: Manages the transmission and reception of stepper-related messages.
- **Temperature Sensor Module (`modules/temperature.c`)**: Manages the transmission of temperature and humudity sensor data.
- **Tone Generator Module (`modules/tone.c`)**: Manages the transmission and reception of tone generator related messages.

Each module leverages `jerrycan.c` for core message handling functionality. Transmission rates for each module can be configured via Kconfig settings, making JerryCAN flexible and adaptable to various embedded applications. This modular structure also supports easy expansion, allowing developers to add new modules to handle additional data types as needed.

### Creating a Message Module

To add a message module, create a new C file in the `modules/` directory, add the module as a conditional target to `CMakeLists.txt` like so
```cmake
zephyr_library_sources_ifdef(CONFIG_*
        modules/<new_module>.c
)
```

#### Handling Tx Messages

Periodic transmissions should be set up using zephyr kernal timers and an associated callback function. For example, take this excerpt from the status module
```c
#include "jerrycan.h"

void jerrycan_status_tx() {
    // Send a status message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_STATUS,
        .status =
            {
                .estop_active = 0,
                .limit_switch0 = 0,
                .limit_switch1 = 0,
                .limit_switch2 = 0,
                .button0 = 0,
                .stepper_status0 = 0,
                .stepper_status1 = 0,
                .stepper_status2 = 0,
                .servo_status0 = 0,
                .servo_status1 = 0,
                .servo_status2 = 0,
            },
    };

    jerrycan_tx(&msg, K_NO_WAIT);
}

K_TIMER_DEFINE(jerrycan_status_timer, jerrycan_status_tx, NULL);

static int jerrycan_status_init() {
    // Start the timer that will send the status message periodically
    k_timer_start(&jerrycan_status_timer, K_MSEC(100), K_MSEC(CONFIG_LIB_JERRYCAN_STATUS_TX_PERIOD_MS));
    return 0;
}

SYS_INIT(jerrycan_status_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
```

Additionally, a transmission period should be defined within the `Kconfig` file, of the form `menuconfig LIB_JERRYCAN_*_TX_PERIOD_MS` like so
```Kconfig
menuconfig LIB_JERRYCAN_STATUS_TX_PERIOD_MS
    int "Status TX Period"
    default 100
    depends on LIB_JERRYCAN
    help
      The period at which the status message is transmitted in milliseconds.
```

#### Handling Rx Messages


The reception of incoming messages should be handled by registering filtered RX callbacks with the JerryCAN library core. For example, take this excerpt from heartbeat module:
```c
#include "jerrycan.h"

static void heartbeat_handler(jerrycan_msg_t *msg) {
    // If we receive a heartbeat message, we should blink the LED
    heartbeat_led_start();
}

static jerrycan_rx_callback_t heartbeat_callback = {
    .filter_msg_type = JERRYCAN_CMD_HEARTBEAT,
    .func = heartbeat_handler,
};

static int jerrycan_heartbeat_init() {
    // Register an handler for HEARTBEAT messages
    return jerrycan_register_rx_callback(&heartbeat_callback);
}

SYS_INIT(jerrycan_heartbeat_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
```

#### Integrating with `libjerrycan`

In order to provide support for new messages to the `pyjerrycan` library, the C++ python bindings must be updated to reflect the contents of `jerrycan_types.h`. This is done by updating `software/libjerrycan/bindings/pyjerrycan.cpp`

##### Updating `jerrycan_cmd_type_t` Binding

Enumerated values defined within `enum jerrycan_cmd_type_t` should be reflected in the `JerryCANCmdType` enum binding. Take this excerpt as an example, where `<message_type>` is the substring following `JERRYCAN_CMD_` in the new enumerated value, which binds `JERRYCAN_CMD_<message_type>` to the enum value `<message_type>` within the Python Enum type `JerryCANCmdType`
```c++
    py::enum_<jerrycan_cmd_type_t>(m, "JerryCANCmdType")
        .value("ESTOP", JERRYCAN_CMD_ESTOP)
        /* Remaining values of the enumerated type... */
        .value("<message_type>", JERRYCAN_CMD_<message_type>)
        .export_values()
    ;
```

##### Updating `jerrycan_msg_t` Binding

Fields of the `jerrycan_msg_t` should be reflected in the `JerryCANMsg` class binding. Take this excerpt as an example - where `<field>` is the name of the new message field of the anonymous struct within `jerrycan_msg_t`
```c++
py::class_<jerrycan_msg_t>(m, "JerryCANMsg")
        .def(py::init<>())
        .def_readwrite("type", &jerrycan_msg_t::type)
        .def_readwrite("dst_id", &jerrycan_msg_t::dst_id)
        .def_readwrite("estop", &jerrycan_msg_t::estop)
        /* Remaining fields of the anonymous struct... */
        .def_readwrite("<field>", &jerrycan_msg_t::<field>)
    ;
```

##### Creating the `jerrycan_cmd_*_t` Binding

A new binding must also be provided for the new message struct, `jerrycan_cmd_*_t` which has now been defined in `jerrycan_types.h`. For example, take the `jerrycan_cmd_tone_t` binding, which binds `jerrycan_cmd_tone_t` to the Python class `Tone`
```c++
py::class_<jerrycan_cmd_tone_t>(m, "Tone")
        .def(py::init<>())
        .def_readwrite("instance", &jerrycan_cmd_tone_t::instance)
        .def_readwrite("frequency_hz", &jerrycan_cmd_tone_t::frequency_hz)
        .def_readwrite("duration_ms", &jerrycan_cmd_tone_t::duration_ms)
    ;
```

### Timer and Thread List

The following table outlines the primary entities managed by the JerryCAN library, including their mechanisms, operating rates,
and launch contexts. Periods for configurable timers are set via `CONFIG_LIB_JERRYCAN_*_TX_PERIOD_MS` in the Kconfig file.

| **Entity**           | **Mechanism** | **Period**                                           | **Launch**    |
|----------------------|---------------|------------------------------------------------------|---------------|
| JerryCAN             | Poll Event    | On-Demand                                            | Application   |
| Generic GPIO         | Timer         | `CONFIG_LIB_JERRYCAN_GPIO_TX_PERIOD_MS` ms           | Kernel        |
| Load Cell            | Timer         | `CONFIG_LIB_JERRYCAN_LOAD_CELL_TX_PERIOD_MS` ms      | Kernel        |
| Pressure Sensor      | Timer         | `CONFIG_LIB_JERRYCAN_PRESSURE_TX_PERIOD_MS` ms       | Kernel        |
| Status Message       | Timer         | `CONFIG_LIB_JERRYCAN_STATUS_TX_PERIOD_MS` ms         | Kernel        |
| Temperature Sensor   | Timer         | `CONFIG_LIB_JERRYCAN_TEMPERATURE_TX_PERIOD_MS` ms    | Kernel        |
| Tone Generator       | Timer         | `CONFIG_LIB_JERRYCAN_TONE_TX_PERIOD_MS` ms           | Kernel        |
| Analog Out           | Timer         | `CONFIG_LIB_JERRYCAN_ANALOG_OUT_TX_PERIOD_MS` ms     | Kernel        |

#### Notes
- Periods configured via `CONFIG_LIB_JERRYCAN_*_TX_PERIOD_MS` can be adjusted in the Kconfig file.
