#include <libjerrycan.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

/* clang-format off */
PYBIND11_MODULE(pyjerrycan, m) {
    m.doc() = "JerryCAN Host Interface";
    py::class_<JerryCAN>(m, "JerryCAN")
        .def(py::init<>())
        .def("Open", &JerryCAN::Open)
        .def("Close", &JerryCAN::Close)
        .def("SendMessage", &JerryCAN::SendMessage)
        .def("ReceiveMessage", [](JerryCAN &j) {
            jerrycan_msg_t msg;
            auto ret = j.ReceiveMessage(msg);
            return ret < 0 ? std::nullopt : std::make_optional(msg);
        })
        .def("Heartbeat", &JerryCAN::Heartbeat)
        .def("EStop", &JerryCAN::EStop)
        .def("StepperMove", &JerryCAN::StepperMove)
        .def("ServoMove", &JerryCAN::ServoMove)
        .def("StepperHome", &JerryCAN::StepperHome)
        .def("CfgWrite", &JerryCAN::CfgWrite)
        .def("CfgRead", &JerryCAN::CfgRead)
        .def("GPIOWrite", &JerryCAN::GPIOWrite)
        .def("ToneWrite", &JerryCAN::ToneWrite)
        .def("AnalogOutWrite", &JerryCAN::AnalogOutWrite)
    ;

    py::class_<jerrycan_msg_t>(m, "JerryCANMsg")
        .def(py::init<>())
        .def_readwrite("type", &jerrycan_msg_t::type)
        .def_readwrite("dst_id", &jerrycan_msg_t::dst_id)
        .def_readwrite("estop", &jerrycan_msg_t::estop)
        .def_readwrite("status", &jerrycan_msg_t::status)
        .def_readwrite("heartbeat", &jerrycan_msg_t::heartbeat)
        .def_readwrite("stepper_move", &jerrycan_msg_t::stepper_move)
        .def_readwrite("servo_move", &jerrycan_msg_t::servo_move)
        .def_readwrite("cfg_response", &jerrycan_msg_t::cfg_response)
        .def_readwrite("cfg_read", &jerrycan_msg_t::cfg_read)
        .def_readwrite("cfg_write", &jerrycan_msg_t::cfg_write)
        .def_readwrite("pressure_read", &jerrycan_msg_t::pressure_read)
        .def_readwrite("temp_hum_read", &jerrycan_msg_t::temp_hum_read)
        .def_readwrite("gpio_read", &jerrycan_msg_t::gpio_read)
        .def_readwrite("gpio_write", &jerrycan_msg_t::gpio_write)
        .def_readwrite("tone", &jerrycan_msg_t::tone)
        .def_readwrite("analog_out", &jerrycan_msg_t::analog_out)
        .def_readwrite("load_cell_read", &jerrycan_msg_t::load_cell_read)
    ;

    py::class_<jerrycan_cmd_status_t>(m, "Status")
        .def(py::init<>())
        .def_property("estop_active",
            [](const jerrycan_cmd_status_t &a) { return a.estop_active; },
            [](jerrycan_cmd_status_t &a, uint8_t v) { a.estop_active = v; })
        .def_property("limit_switch0",
            [](const jerrycan_cmd_status_t &a) { return a.limit_switch0; },
            [](jerrycan_cmd_status_t &a, uint8_t v) { a.limit_switch0 = v; })
        .def_property("limit_switch1",
            [](const jerrycan_cmd_status_t &a) { return a.limit_switch1; },
            [](jerrycan_cmd_status_t &a, uint8_t v) { a.limit_switch1 = v; })
        .def_property("limit_switch2",
            [](const jerrycan_cmd_status_t &a) { return a.limit_switch2; },
            [](jerrycan_cmd_status_t &a, uint8_t v) { a.limit_switch2 = v; })
        .def_property("button0",
            [](const jerrycan_cmd_status_t &a) { return a.button0; },
            [](jerrycan_cmd_status_t &a, uint8_t v) { a.button0 = v; })
        .def_property("stepper_status0",
            [](const jerrycan_cmd_status_t &a) { return a.stepper_status0; },
            [](jerrycan_cmd_status_t &a, uint8_t v) { a.stepper_status0 = v; })
        .def_property("stepper_status1",
            [](const jerrycan_cmd_status_t &a) { return a.stepper_status1; },
            [](jerrycan_cmd_status_t &a, uint8_t v) { a.stepper_status1 = v; })
        .def_property("stepper_status2",
            [](const jerrycan_cmd_status_t &a) { return a.stepper_status2; },
            [](jerrycan_cmd_status_t &a, uint8_t v) { a.stepper_status2 = v; })
        .def_property("servo_status0",
            [](const jerrycan_cmd_status_t &a) { return a.servo_status0; },
            [](jerrycan_cmd_status_t &a, uint8_t v) { a.servo_status0 = v; })
        .def_property("servo_status1",
            [](const jerrycan_cmd_status_t &a) { return a.servo_status1; },
            [](jerrycan_cmd_status_t &a, uint8_t v) { a.servo_status1 = v; })
        .def_property("servo_status2",
            [](const jerrycan_cmd_status_t &a) { return a.servo_status2; },
            [](jerrycan_cmd_status_t &a, uint8_t v) { a.servo_status2 = v; })
    ;

    py::class_<jerrycan_cmd_cfg_t> cmd_cfg(m, "JerryCANCfgMsg");
    cmd_cfg.def(py::init<>())
        .def_readwrite("type", &jerrycan_cmd_cfg_t::type)
        .def_readwrite("servo", &jerrycan_cmd_cfg_t::servo)
        .def_readwrite("stepper", &jerrycan_cmd_cfg_t::stepper)
    ;

    py::enum_<jerrycan_cfg_type_t>(cmd_cfg, "Type")
        .value("STEPPER", JERRYCAN_CFG_STEPPER)
        .value("SERVO", JERRYCAN_CFG_SERVO)
        .export_values()
    ;

    py::class_<jerrycan_servo_cfg_t>(cmd_cfg, "Servo")
        .def(py::init<>())
        .def_property("motor_id",
            // This is a workaround for setting bitfields
            [](const jerrycan_servo_cfg_t &a) { return a.motor_id; },
            [](jerrycan_servo_cfg_t &a, uint8_t v) { a.motor_id = v; })
        .def_readwrite("min_position", &jerrycan_servo_cfg_t::min_position)
        .def_readwrite("mid_position", &jerrycan_servo_cfg_t::mid_position)
        .def_readwrite("max_position", &jerrycan_servo_cfg_t::max_position)
    ;

    py::class_<jerrycan_stepper_cfg_t>(cmd_cfg, "Stepper")
        .def(py::init<>())
        .def_property("motor_id",
            [](const jerrycan_stepper_cfg_t &a) { return a.motor_id; },
            [](jerrycan_stepper_cfg_t &a, uint8_t v) { a.motor_id = v; })
        .def_readwrite("min_position", &jerrycan_stepper_cfg_t::min_position)
        .def_readwrite("max_position", &jerrycan_stepper_cfg_t::max_position)
    ;

    py::class_<jerrycan_cmd_pressure_read_t>(m, "PressureRead")
        .def(py::init<>())
        .def_property("instance",
            [](const jerrycan_cmd_pressure_read_t &a) { return a.instance; },
            [](jerrycan_cmd_pressure_read_t &a, uint8_t v) { a.instance = v; })
        .def_property("error",
            [](const jerrycan_cmd_pressure_read_t &a) { return a.error; },
            [](jerrycan_cmd_pressure_read_t &a, uint8_t v) { a.error = v; })
        .def_readwrite("pressure_mv", &jerrycan_cmd_pressure_read_t::pressure_mv)
    ;

    py::class_<jerrycan_cmd_temp_hum_read_t>(m, "TempHumRead")
        .def(py::init<>())
        .def_property("instance",
            [](const jerrycan_cmd_temp_hum_read_t &a) { return a.instance; },
            [](jerrycan_cmd_temp_hum_read_t &a, uint8_t v) { a.instance = v; })
        .def_property("temperature",
            [](const jerrycan_cmd_temp_hum_read_t &a) { return a.temperature; },
            [](jerrycan_cmd_temp_hum_read_t &a, uint8_t v) { a.temperature = v; })
        .def_property("humidity",
            [](const jerrycan_cmd_temp_hum_read_t &a) { return a.humidity; },
            [](jerrycan_cmd_temp_hum_read_t &a, uint8_t v) { a.humidity = v; })
    ;

    py::class_<jerrycan_cmd_gpio_read_t>(m, "GPIORead")
        .def(py::init<>())
        .def_property("instance",
            [](const jerrycan_cmd_gpio_read_t &a) { return a.instance; },
            [](jerrycan_cmd_gpio_read_t &a, uint8_t v) { a.instance = v; })
        .def_property("state",
            [](const jerrycan_cmd_gpio_read_t &a) { return a.state; },
            [](jerrycan_cmd_gpio_read_t &a, uint8_t v) { a.state = v; })
    ;

    py::class_<jerrycan_cmd_gpio_write_t>(m, "GPIOWrite")
        .def(py::init<>())
        .def_property("instance",
            [](const jerrycan_cmd_gpio_write_t &a) { return a.instance; },
            [](jerrycan_cmd_gpio_write_t &a, uint8_t v) { a.instance = v; })
        .def_property("gpio_idx",
            [](const jerrycan_cmd_gpio_write_t &a) { return a.gpio_idx; },
            [](jerrycan_cmd_gpio_write_t &a, uint8_t v) { a.gpio_idx = v; })
        .def_property("state",
            [](const jerrycan_cmd_gpio_write_t &a) { return a.state; },
            [](jerrycan_cmd_gpio_write_t &a, uint8_t v) { a.state = v; })
    ;

    py::class_<jerrycan_cmd_tone_t>(m, "Tone")
        .def(py::init<>())
        .def_readwrite("instance", &jerrycan_cmd_tone_t::instance)
        .def_readwrite("frequency_hz", &jerrycan_cmd_tone_t::frequency_hz)
        .def_readwrite("duration_ms", &jerrycan_cmd_tone_t::duration_ms)
    ;

    py::class_<jerrycan_cmd_analog_out_t>(m, "AnalogOut")
        .def(py::init<>())
        .def_readwrite("instance", &jerrycan_cmd_analog_out_t::instance)
        .def_readwrite("value_mv", &jerrycan_cmd_analog_out_t::value_mv)
    ;

    py::class_<jerrycan_cmd_load_cell_read_t>(m, "LoadCellRead")
        .def(py::init<>())
        .def_readwrite("instance", &jerrycan_cmd_load_cell_read_t::instance)
        .def_readwrite("load_mv", &jerrycan_cmd_load_cell_read_t::load_mv)
    ;

    py::enum_<jerrycan_cmd_type_t>(m, "JerryCANCmdType")
        .value("ESTOP", JERRYCAN_CMD_ESTOP)
        .value("HEARTBEAT", JERRYCAN_CMD_HEARTBEAT)
        .value("STATUS", JERRYCAN_CMD_STATUS)
        .value("STEPPER_MOVE", JERRYCAN_CMD_STEPPER_MOVE)
        .value("SERVO_MOVE", JERRYCAN_CMD_SERVO_MOVE)
        .value("STEPPER_HOME", JERRYCAN_CMD_STEPPER_HOME)
        .value("CFG_WRITE", JERRYCAN_CMD_CFG_WRITE)
        .value("CFG_READ", JERRYCAN_CMD_CFG_READ)
        .value("CFG_RESPONSE", JERRYCAN_CMD_CFG_RESPONSE)
        .value("PRESSURE_READ", JERRYCAN_CMD_PRESSURE_READ)
        .value("TEMP_HUM_READ", JERRYCAN_CMD_TEMP_HUM_READ)
        .value("GPIO_READ", JERRYCAN_CMD_GPIO_READ)
        .value("GPIO_WRITE", JERRYCAN_CMD_GPIO_WRITE)
        .value("TONE", JERRYCAN_CMD_TONE)
        .value("ANALOG_OUT", JERRYCAN_CMD_ANALOG_OUT)
        .value("LOAD_CELL_READ", JERRYCAN_CMD_LOAD_CELL_READ)
        .export_values()
    ;
}
/* clang-format on */
