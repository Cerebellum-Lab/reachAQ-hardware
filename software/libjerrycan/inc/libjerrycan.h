#pragma once
#include <string>

#include "jerrycan_types.h"

class JerryCAN {
   public:
    JerryCAN() = default;
    int Open();
    int Close();

    int SendMessage(jerrycan_msg_t &msg, uint16_t dst_id);
    int ReceiveMessage(jerrycan_msg_t &msg);

    int Heartbeat();

    int EStop(bool enable);

    int StepperMove(uint8_t dst_id, uint8_t stepper_id, uint16_t position, uint16_t max_velocity,
                    uint16_t max_acceleration, bool abs_or_rel);

    int ServoMove(uint8_t dst_id, uint8_t servo_id, uint16_t position, uint16_t max_velocity, uint16_t max_acceleration,
                  bool abs_or_rel);

    int StepperHome(uint8_t dst_id, uint8_t stepper_id);

    int CfgWrite(uint8_t dst_id, jerrycan_cmd_cfg_t &cfg);

    int CfgRead(uint8_t dst_id, jerrycan_cmd_cfg_t &cfg);

   private:
    int _can_socket_handle;
};
