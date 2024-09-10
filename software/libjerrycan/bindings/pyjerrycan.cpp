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
        .export_values()
    ;
}
/* clang-format on */
