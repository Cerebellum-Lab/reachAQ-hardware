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

    int StepperMove(uint8_t dst_id, uint8_t motor_id, float position, float max_velocity, float max_acceleration,
                    abs_or_rel_t abs_or_rel);

    int ServoMove(uint8_t dst_id, uint8_t motor_id, float position, float max_velocity, float max_acceleration,
                  abs_or_rel_t abs_or_rel);

    int StepperHome(uint8_t dst_id, uint8_t motor_id, bool forward);

    int CfgWrite(uint8_t dst_id, jerrycan_cmd_cfg_t &cfg);

    int CfgRead(uint8_t dst_id, jerrycan_cmd_cfg_t &cfg);

    int StepperCfgWrite(uint8_t dst_id, uint8_t motor_id, uint16_t min_step_inverse, float steps_per_revolution,
                        float motor_max_velocity, float motor_max_acceleration);

    int ServoCfgWrite(uint8_t dst_id, uint8_t motor_id, float min_position, float max_position,
                      float min_pwm_duration_us, float max_pwm_duration_us);

    int StepperCfgRead(uint8_t dst_id, uint8_t motor_id);

    int ServoCfgRead(uint8_t dst_id, uint8_t motor_id);

    int GPIOWrite(uint8_t dst_id, uint8_t instance, uint16_t gpio_idx, bool state);

    int ToneWrite(uint8_t dst_id, uint8_t instance, uint16_t frequency, uint16_t duration);

    int AnalogOutWrite(uint8_t dst_id, uint8_t instance, uint16_t value_mv);

    int LoadCellTare(uint8_t dst_id, uint8_t instance);

    int PressureSensorTare(uint8_t dst_id, uint8_t instance);

    int RGBLEDWrite(uint8_t dst_id, uint8_t red, uint8_t green, uint8_t blue);

    int BootloaderCommand(uint8_t dst_id, jerrycan_bootloader_subcmd_t subcmd);

    int BootloaderData(uint8_t dst_id, jerrycan_cmd_bootloader_data_t &data);

   private:
    int _can_socket_handle;
};
