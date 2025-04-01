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
        .def("SendMessage", &JerryCAN::SendMessage, py::arg("msg"), py::arg("dst_id"))
        .def("ReceiveMessage", [](JerryCAN &j) {
            jerrycan_msg_t msg;
            auto ret = j.ReceiveMessage(msg);
            return ret < 0 ? std::nullopt : std::make_optional(msg);
        })
        .def("Heartbeat", &JerryCAN::Heartbeat)
        .def("EStop", &JerryCAN::EStop)
        .def("StepperMove", &JerryCAN::StepperMove, py::arg("dst_id"), py::arg("motor_id"), py::arg("position"), py::arg("max_velocity"), py::arg("max_acceleration"), py::arg("abs_or_rel"))
        .def("ServoMove", &JerryCAN::ServoMove, py::arg("dst_id"), py::arg("motor_id"), py::arg("position"), py::arg("max_velocity"), py::arg("max_acceleration"), py::arg("abs_or_rel"))
        .def("StepperHome", &JerryCAN::StepperHome, py::arg("dst_id"), py::arg("motor_id"))
        .def("CfgWrite", &JerryCAN::CfgWrite, py::arg("dst_id"), py::arg("cfg"))
        .def("StepperCfgWrite", &JerryCAN::StepperCfgWrite, py::arg("dst_id"), py::arg("motor_id"), py::arg("microsteps"), py::arg("steps_per_revolution"), py::arg("motor_max_velocity"), py::arg("motor_max_acceleration"), py::arg("flip_limit_orientation"))
        .def("ServoCfgWrite", &JerryCAN::ServoCfgWrite, py::arg("dst_id"), py::arg("motor_id"), py::arg("min_position"), py::arg("max_position"), py::arg("min_pwm_duration_us"), py::arg("max_pwm_duration_us"), py::arg("motor_max_velocity"), py::arg("motor_max_acceleration"))
        .def("StepperCfgRead", &JerryCAN::StepperCfgRead, py::arg("dst_id"), py::arg("motor_id"))
        .def("ServoCfgRead", &JerryCAN::ServoCfgRead, py::arg("dst_id"), py::arg("motor_id"))
        .def("CfgRead", &JerryCAN::CfgRead, py::arg("dst_id"), py::arg("cfg"))
        .def("GPIOWrite", &JerryCAN::GPIOWrite, py::arg("dst_id"), py::arg("instance"), py::arg("gpio_idx"), py::arg("state"))
        .def("ToneWrite", &JerryCAN::ToneWrite, py::arg("dst_id"), py::arg("instance"), py::arg("frequency"), py::arg("duration"))
        .def("AnalogOutWrite", &JerryCAN::AnalogOutWrite, py::arg("dst_id"), py::arg("instance"), py::arg("value_mv"))
        .def("LoadCellTare", &JerryCAN::LoadCellTare, py::arg("dst_id"), py::arg("instance"))
        .def("PressureSensorTare", &JerryCAN::PressureSensorTare, py::arg("dst_id"), py::arg("instance"))
        .def("RGBLEDWrite", &JerryCAN::RGBLEDWrite, py::arg("dst_id"), py::arg("red"), py::arg("green"), py::arg("blue"))
        .def("BootloaderCommand", &JerryCAN::BootloaderCommand, py::arg("dst_id"), py::arg("subcmd"))
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
        .def_readwrite("stepper_home", &jerrycan_msg_t::stepper_home)
        .def_readwrite("cfg_response", &jerrycan_msg_t::cfg_response)
        .def_readwrite("cfg_read", &jerrycan_msg_t::cfg_read)
        .def_readwrite("cfg_write", &jerrycan_msg_t::cfg_write)
        .def_readwrite("stepper_status", &jerrycan_msg_t::stepper_status)
        .def_readwrite("servo_status", &jerrycan_msg_t::servo_status)
        .def_readwrite("pressure_read", &jerrycan_msg_t::pressure_read)
        .def_readwrite("temp_hum_read", &jerrycan_msg_t::temp_hum_read)
        .def_readwrite("gpio_read", &jerrycan_msg_t::gpio_read)
        .def_readwrite("gpio_write", &jerrycan_msg_t::gpio_write)
        .def_readwrite("tone", &jerrycan_msg_t::tone)
        .def_readwrite("analog_out", &jerrycan_msg_t::analog_out)
        .def_readwrite("load_cell_read", &jerrycan_msg_t::load_cell_read)
        .def_readwrite("load_cell_tare", &jerrycan_msg_t::load_cell_tare)
        .def_readwrite("pressure_sensor_tare", &jerrycan_msg_t::pressure_sensor_tare)
        .def_readwrite("rgb_led", &jerrycan_msg_t::rgb_led)
        .def_readwrite("doors", &jerrycan_msg_t::doors)
        .def_readwrite("audio_data_cmd", &jerrycan_msg_t::audio_data_cmd)
        .def_readwrite("audio_data", &jerrycan_msg_t::audio_data)
        .def_readwrite("bootloader_command", &jerrycan_msg_t::bootloader_command)
        .def_readwrite("bootloader_response", &jerrycan_msg_t::bootloader_response)
        .def_readwrite("bootloader_data", &jerrycan_msg_t::bootloader_data)
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

    py::class_<jerrycan_servo_cfg_t>(cmd_cfg, "ServoCfg")
        .def(py::init<>())
        .def_property("motor_id",
            // This is a workaround for setting bitfields
            [](const jerrycan_servo_cfg_t &a) { return a.motor_id; },
            [](jerrycan_servo_cfg_t &a, uint8_t v) { a.motor_id = v; })
        .def_property("error",
            [](const jerrycan_servo_cfg_t &a) { return a.error; },
            [](jerrycan_servo_cfg_t &a, uint8_t v) { a.error = v; })
        .def_readwrite("min_position", &jerrycan_servo_cfg_t::min_position)
        .def_readwrite("max_position", &jerrycan_servo_cfg_t::max_position)
        .def_readwrite("min_pwm_duration_us", &jerrycan_servo_cfg_t::min_pwm_duration_us)
        .def_readwrite("max_pwm_duration_us", &jerrycan_servo_cfg_t::max_pwm_duration_us)
        .def_readwrite("motor_max_velocity", &jerrycan_servo_cfg_t::motor_max_velocity)
        .def_readwrite("motor_max_acceleration", &jerrycan_servo_cfg_t::motor_max_acceleration);

    py::class_<jerrycan_stepper_cfg_t>(cmd_cfg, "StepperCfg")
        .def(py::init<>())
        .def_property("motor_id",
            [](const jerrycan_stepper_cfg_t &a) { return a.motor_id; },
            [](jerrycan_stepper_cfg_t &a, uint8_t v) { a.motor_id = v; })
        .def_property("flip_limit_orientation",
            [](const jerrycan_stepper_cfg_t &a) { return a.flip_limit_orientation; },
            [](jerrycan_stepper_cfg_t &a, const uint8_t flip_limit_orientation) { a.flip_limit_orientation = flip_limit_orientation; })
        .def_readwrite("microsteps", &jerrycan_stepper_cfg_t::microsteps)
        .def_readwrite("steps_per_revolution", &jerrycan_stepper_cfg_t::steps_per_revolution)
        .def_readwrite("motor_max_velocity", &jerrycan_stepper_cfg_t::motor_max_velocity)
        .def_readwrite("motor_max_acceleration", &jerrycan_stepper_cfg_t::motor_max_acceleration);

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
        .def_readwrite("temperature", &jerrycan_cmd_temp_hum_read_t::temperature)
        .def_readwrite("humidity", &jerrycan_cmd_temp_hum_read_t::humidity)
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
    
    py::class_<jerrycan_cmd_stepper_home_t>(m, "StepperHome")
        .def(py::init<>())
        .def_property("motor_id",
            [](const jerrycan_cmd_stepper_home_t &a) { return a.motor_id; },
            [](jerrycan_cmd_stepper_home_t &a, uint8_t v) { a.motor_id = v; });

    py::class_<jerrycan_cmd_stepper_status_t>(m, "StepperStatus")
        .def(py::init<>())
        .def_readwrite("motor_id", &jerrycan_cmd_stepper_status_t::motor_id)
        .def_readwrite("status", &jerrycan_cmd_stepper_status_t::status)
        .def_readwrite("homing_status", &jerrycan_cmd_stepper_status_t::homing_status)
        .def_readwrite("position", &jerrycan_cmd_stepper_status_t::position)
        .def_readwrite("limit_switch", &jerrycan_cmd_stepper_status_t::limit_switch)
    ;

    py::class_<jerrycan_cmd_servo_status_t>(m, "ServoStatus")
        .def(py::init<>())
        .def_readwrite("motor_id", &jerrycan_cmd_servo_status_t::motor_id)
        .def_readwrite("status", &jerrycan_cmd_servo_status_t::status)
        .def_readwrite("position", &jerrycan_cmd_servo_status_t::position)
    ;

    py::class_<jerrycan_cmd_load_cell_tare_t>(m, "LoadCellTare")
        .def(py::init<>())
        .def_readwrite("instance", &jerrycan_cmd_load_cell_tare_t::instance)
    ;

    py::class_<jerrycan_cmd_pressure_sensor_tare_t>(m, "PressureSensorTare")
        .def(py::init<>())
        .def_readwrite("instance", &jerrycan_cmd_pressure_sensor_tare_t::instance)
    ;

    py::enum_<abs_or_rel_t>(m, "AbsOrRel")
        .value("ABSOLUTE", JERRYCAN_MOVE_ABSOLUTE)
        .value("RELATIVE", JERRYCAN_MOVE_RELATIVE)
        .export_values()
    ;

    py::class_<jerrycan_cmd_rgb_led_t>(m, "RGBLED")
        .def(py::init<>())
        .def_readwrite("red", &jerrycan_cmd_rgb_led_t::red)
        .def_readwrite("green", &jerrycan_cmd_rgb_led_t::green)
        .def_readwrite("blue", &jerrycan_cmd_rgb_led_t::blue)
    ;
    
    py::class_<jerrycan_cmd_door_closed_t>(m, "Doors")
        .def(py::init<>())
        .def_property("opened",
            [](const jerrycan_cmd_door_closed_t &a) { return a.opened; },
            [](jerrycan_cmd_door_closed_t &a, uint8_t v) { a.opened = v; })
    ;

    py::class_<jerrycan_cmd_audio_data_cmd_t>(m, "AudioDataCmd")
        .def(py::init<>())
        .def_readwrite("stream_id", &jerrycan_cmd_audio_data_cmd_t::stream_id)
    ;

    py::class_<jerrycan_cmd_audio_data_t>(m, "AudioData")
        .def(py::init<>())
        .def_property("magnitudes",
            [](const jerrycan_cmd_audio_data_t &a) { return std::vector<float>(a.magnitudes, a.magnitudes + JERRYCAN_MAX_PAYLOAD_SIZE/sizeof(float)); },
            [](jerrycan_cmd_audio_data_t &a, const std::vector<float> &v) { 
                if (v.size() != JERRYCAN_MAX_PAYLOAD_SIZE/sizeof(float)) {
                    throw std::runtime_error("Size mismatch: Expected array of size " + std::to_string(JERRYCAN_MAX_PAYLOAD_SIZE/sizeof(float)));
                }
                std::memcpy(a.magnitudes, v.data(), JERRYCAN_MAX_PAYLOAD_SIZE); })
    ;

    py::class_<jerrycan_cmd_bootloader_command_t> bootloader_command(m, "JerryCANBootloaderCmd");
    bootloader_command.def(py::init<>())
        .def_readwrite("type", &jerrycan_cmd_bootloader_command_t::type)
    ;

    py::enum_<jerrycan_bootloader_subcmd_t>(bootloader_command, "SubCommand")
        .value("VERSION", JERRYCAN_BOOTLOADER_SUBCMD_VERSION)
        .value("START", JERRYCAN_BOOTLOADER_SUBCMD_START)
        .value("END", JERRYCAN_BOOTLOADER_SUBCMD_END)
        .value("REBOOT", JERRYCAN_BOOTLOADER_SUBCMD_REBOOT)
        .value("FINALIZE", JERRYCAN_BOOTLOADER_SUBCMD_FINALIZE)
        .value("ACK", JERRYCAN_BOOTLOADER_SUBCMD_ACK)
        .value("NACK", JERRYCAN_BOOTLOADER_SUBCMD_NACK)
        .export_values()
    ;

    py::class_<jerrycan_cmd_bootloader_response_t>(m, "BootloaderResponse")
        .def(py::init<>())
        .def_readwrite("type", &jerrycan_cmd_bootloader_response_t::type)
        .def_readwrite("version", &jerrycan_cmd_bootloader_response_t::version)
        .def_readwrite("status", &jerrycan_cmd_bootloader_response_t::status)
    ;

    py::class_<jerrycan_cmd_bootloader_version_t>(m, "BootloaderVersion")
        .def(py::init<>())
        .def_readwrite("running_major", &jerrycan_cmd_bootloader_version_t::running_version_major)
        .def_readwrite("running_minor", &jerrycan_cmd_bootloader_version_t::running_version_minor)
        .def_readwrite("running_patch", &jerrycan_cmd_bootloader_version_t::running_version_patch)
        .def_readwrite("slot1_major", &jerrycan_cmd_bootloader_version_t::slot1_version_major)
        .def_readwrite("slot1_minor", &jerrycan_cmd_bootloader_version_t::slot1_version_minor)
        .def_readwrite("slot1_patch", &jerrycan_cmd_bootloader_version_t::slot1_version_patch)
    ;

    py::class_<jerrycan_cmd_bootloader_data_t>(m, "BootloaderData")
        .def(py::init<>())
        .def_property("data",
            [](const jerrycan_cmd_bootloader_data_t &a) { return a.data; },
            [](jerrycan_cmd_bootloader_data_t &a, uint8_t v[JERRYCAN_MAX_PAYLOAD_SIZE]) { std::memcpy(a.data, v, JERRYCAN_MAX_PAYLOAD_SIZE); })
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
        .value("STEPPER_STATUS", JERRYCAN_CMD_STEPPER_STATUS)
        .value("SERVO_STATUS", JERRYCAN_CMD_SERVO_STATUS)
        .value("PRESSURE_READ", JERRYCAN_CMD_PRESSURE_READ)
        .value("TEMP_HUM_READ", JERRYCAN_CMD_TEMP_HUM_READ)
        .value("GPIO_READ", JERRYCAN_CMD_GPIO_READ)
        .value("GPIO_WRITE", JERRYCAN_CMD_GPIO_WRITE)
        .value("TONE", JERRYCAN_CMD_TONE)
        .value("ANALOG_OUT", JERRYCAN_CMD_ANALOG_OUT)
        .value("LOAD_CELL_READ", JERRYCAN_CMD_LOAD_CELL_READ)
        .value("LOAD_CELL_TARE", JERRYCAN_CMD_LOAD_CELL_TARE)
        .value("PRESSURE_SENSOR_TARE", JERRYCAN_CMD_PRESSURE_SENSOR_TARE)
        .value("RGB_LED", JERRYCAN_CMD_RGB_LED)
        .value("DOOR_SENSOR", JERRYCAN_CMD_DOOR_SENSOR)
        .value("AUDIO_MAGNITUDE_DATA_BEGIN", JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_BEGIN)
        .value("AUDIO_MAGNITUDE_DATA_CONT", JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_CONT)
        .value("AUDIO_MAGNITUDE_DATA_END", JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_END)
        .value("BOOTLOADER_COMMAND", JERRYCAN_CMD_BOOTLOADER_COMMAND)
        .value("BOOTLOADER_RESPONSE", JERRYCAN_CMD_BOOTLOADER_RESPONSE)
        .value("BOOTLOADER_DATA", JERRYCAN_CMD_BOOTLOADER_DATA)
        .export_values()
    ;
}
/* clang-format on */
