#pragma once
#include <errno.h>
#include <stdint.h>
#include <zephyr/drivers/i2c.h>

/* NAU7802 Fixed I2C Address */
#define NAU7802_I2CADDR 0x2A

/* Maximum signed 24-bit value */
#define INT24_MAX 0x007FFFFF
#define INT24_MIN 0xFF800000

/* Enumeration of valid gain selection values */
typedef enum {
    NAU7802_GAIN_1X = 0b000,
    NAU7802_GAIN_2X = 0b001,
    NAU7802_GAIN_4X = 0b010,
    NAU7802_GAIN_8X = 0b011,
    NAU7802_GAIN_16X = 0b100,
    NAU7802_GAIN_32X = 0b101,
    NAU7802_GAIN_64X = 0b110,
    NAU7802_GAIN_128X = 0b111,
} __attribute__((__packed__)) nau7802_gains_t;

/* Macro mapping nau7802_gains_t to associated scalar value as an int */
#define NAU7802_GAINS_TO_SCALAR(__gains__) (1U << (__gains__))

/* Enumeration of valid LDO voltage selection values */
typedef enum {
    NAU7802_VLDO_4V5 = 0b000,
    NAU7802_VLDO_4V2 = 0b001,
    NAU7802_VLDO_3V9 = 0b010,
    NAU7802_VLDO_3V6 = 0b011,
    NAU7802_VLDO_3V3 = 0b100,
    NAU7802_VLDO_3V0 = 0b101,
    NAU7802_VLDO_2V7 = 0b110,
    NAU7802_VLDO_2V4 = 0b111,
} __attribute__((__packed__)) nau7802_vldo_t;

/* Enumeration of valid calibration mode selection values */
typedef enum {
    OFFSET_CALIBRATION_INTERNAL = 0b00,
    OFFSET_CALIBRATION_SYSTEM = 0b10,
    GAIN_CALIBRATION_SYSTEM = 0b11,
} __attribute__((__packed__)) nau7802_calmod_t;

/* Enumeration of valid conversion rate selection values */
typedef enum {
    NAU7802_CONVERSION_RATE_10SPS = 0b000,
    NAU7802_CONVERSION_RATE_20SPS = 0b001,
    NAU7802_CONVERSION_RATE_40SPS = 0b010,
    NAU7802_CONVERSION_RATE_80SPS = 0b011,
    NAU7802_CONVERSION_RATE_320SPS = 0b111,
} __attribute__((__packed__)) nau7802_crs_t;

/* Enumeration of valid ADC channel selection values */
typedef enum {
    NAU7802_ADC_CH1 = 0b0,
    NAU7802_ADC_CH2 = 0b1,
} __attribute__((__packed__)) nau7802_chs_t;

/* Enumeration of valid ADC channel selection values */
typedef enum {
    NAU7802_OSCS_INTERNAL = 0b0,
    NAU7802_OSCS_EXTERNAL = 0b1,
} __attribute__((__packed__)) nau7802_oscs_t;

/*************/
/* Registers */
/*************/

/* PU_CTRL: Power-Up Control Register Structure */
typedef union {
    /* Power-UP Control (PU_CTRL) Register Bits */
    struct {
        uint8_t rr : 1;           // Register Reset: 1 - Reset All Except RR, 0 - Normal Operation (default: 0)
        uint8_t pud : 1;          // Power-Up Digital Circuit: 1 - Power Up, 0 - Power Down (default: 0)
        uint8_t pua : 1;          // Power-Up Analog Circuit: 1 - Power Up, 0 - Power Down (default: 0)
        uint8_t pur : 1;          // Power-Up Ready (Read Only): 1 - Ready, 0 - Not Ready (default: 0)
        uint8_t cs : 1;           // Cycle Start ADC: 1 - Start ADC, 0 - Stop ADC (default: 0)
        uint8_t cr : 1;           // Cycle Ready (Read Only): 1 - ADC Data Ready, 0 - No ADC Data (default: 0)
        nau7802_oscs_t oscs : 1;  // Clock Source Select: 1 - External Crystal, 0 - Internal RC Oscillator (default: 0)
        uint8_t avdds : 1;        // AVDD Source Select: 1 - Internal LDO, 0 - AVDD Pin Input (default: 0)
    };

    /* Power-UP Control (PU_CTRL) Register Byte */
    uint8_t byte;
} nau7802_pu_ctrl_reg_t;

/* CTRL1: Control Register 1 Structure */
typedef union {
    /* Control 1 (CTRL1) Register Bits */
    struct {
        nau7802_gains_t gains : 3;  // Gain Selection Bits (default: 1x)
        nau7802_vldo_t vldo : 3;    // LDO Voltage Output Selection Bits - (default: 4V5)
        uint8_t drdy_sel : 1;       // DRDY Pin Function: 1 - Output Clock, 0 - Output Conversion Ready (default: 0)
        uint8_t crp : 1;            // Conversion Ready Polarity: 1 - Active Low, 0 - Active High (default: 0)
    };
    /* Control 2 (CTRL2) Register Byte */
    uint8_t byte;
} nau7802_ctrl1_reg_t;

/* CTRL2: Control Register 2 Structure */
typedef union {
    /* Control 2 (CTRL2) Register Bits */
    struct {
        nau7802_calmod_t calmod : 2;  // Calibration Selection Bits: Gain, Offset, System, Internal (default: 0)
        uint8_t cals : 1;             // Start Calibration: 1 - Start, 0 - Finished (default: 0)
        uint8_t cal_err : 1;          // Calibration Error: 1 - Failed, 0 - No Error (default: 0)
        nau7802_crs_t crs : 3;        // Conversion Rate Selection Bits: 320, N/A, 80, 40, 20, 10 (default: 0)
        nau7802_chs_t chs : 1;        // Analog Input Channel: 1 - Channel 2, 0 - Channel 1 (default: 0)
    };
    /* Control 2 (CTRL2) Register Byte */
    uint8_t byte;
} nau7802_ctrl2_reg_t;

/* I2C_CTRL: I2C Control Register Structure */
typedef union {
    /* I2C Control (I2C_CTRL) Register Bits */
    struct {
        uint8_t bgpcp : 1;  // Bandgap Chopper: 1 - Disable, 0 - Enable (default: 0)
        uint8_t ts : 1;     // Temperature Sensor Select: 1 - Enable, 0 - Disable (default: 0)
        uint8_t bopga : 1;  // PGA Burnout Current Source: 1 - 2.5µA to PGA+, 0 - Disabled (default: 0)
        uint8_t si : 1;     // Short Inputs: 1 - Short, 0 - Floating (default: 0)
        uint8_t wpd : 1;    // Disable Weak Pullup: 1 - Disable, 0 - Enable (default: 0)
        uint8_t spe : 1;    // Enable Strong Pullup: 1 - Enable, 0 - Disable (default: 0)
        uint8_t frd : 1;    // Fast ADC Data: 1 - Enable, 0 - Disable (default: 0)
        uint8_t crsd : 1;   // Pull SDA Low on Conversion: 1 - Enable, 0 - Disable (default: 0)
    };
    /* I2C Control(I2C_CTRL) Register Byte */
    uint8_t byte;
} nau7802_i2c_ctrl_reg_t;

/* ADC_OUT[2:0]: ADC Conversion Result Structure (All three conversion result registers) */
typedef union {
    uint8_t bytes[3];
} __attribute__((packed)) nau7802_adc_out_reg_t;

/* ADC_REG: ADC Register Structure */
typedef union {
    /* ADC Registers (ADC_REG) Register Bits */
    struct {
        uint8_t reg_chp : 2;   // REG_CHP: Select delay between ADC Clock and ADC Chopper Clock
        uint8_t adc_vcm : 2;   // ADC_VCM: Select ADC input common mode for unipolar configuration
        uint8_t reg_chps : 2;  // REG_CHPS: Select the CLK_CHP clock frequency
        uint8_t rsvd : 2;
    };
    /* ADC Registers (ADC_REG) Register Byte */
    uint8_t byte;
} nau7802_adc_reg_reg_t;

/* PGA_REG: Pre-Gain Amplifier Control Register Structure */
typedef union {
    /* Pre-Gain Amplifier Control Register (PGA_REG) Bits */
    struct {
        uint8_t pgachpdis : 1;     // PGACHPDIS: 1 - Chopper disabled, 0 - Default
        uint8_t rsvd : 2;          // Reserved: 0 - Default
        uint8_t pgainv : 1;        // PGAINV: 1 - Invert PGA input phase, 0 - Default
        uint8_t pgabypass_en : 1;  // PGA Bypass Enable: 1 - PGA bypass enable, 0 - PGA bypass disable
        uint8_t pgabuffer_en : 1;  // PGA Output Buffer Enable: 1 - PGA output buffer enable, 0 - PGA out buffer disable
        uint8_t ldomode : 1;  // LDOMODE: 1 - improved stability and lower DC gain (ESR < 5 Ohms), 0 - improved accuracy
                              // and higher DC gain (ESR < 1 Ohms)
        uint8_t
            rd_otp_sel : 1;  // RD_OTP_SEL (Read ADC_REG select): 1 - Read from OTP[31:24], 0 - read from ADC_OUT[2:0]
    };
    /* Pre-Gain Amplifier Control Register (PGA_REG) Byte */
    uint8_t byte;
} nau7802_pga_reg_t;

/* Generic union of all NAU7802 registers */
typedef union {
    nau7802_pu_ctrl_reg_t pu_ctrl;    // PU_CTRL: Power-Up Control Register
    nau7802_ctrl1_reg_t ctrl1;        // CTRL1: Control Register 1
    nau7802_ctrl2_reg_t ctrl2;        // CTRL2: Control Register 2
    nau7802_i2c_ctrl_reg_t i2c_ctrl;  // I2C_CTRL: I2C Control Register
    nau7802_adc_reg_reg_t adc_reg;    // ADC_REG: ADC Register Address
    nau7802_pga_reg_t pga_reg;        // PGA_REG: Pre-Gain Amplifier Control Register Address
    uint8_t byte;                     // Byte representation of register contents
} nau7802_reg_t;

/* Enumeration of NAU7802 register addresses */
typedef enum {
    NAU7802_PU_CTRL = 0x00,   // PU_CTRL: Power-Up Control Register Address
    NAU7802_CTRL1 = 0x01,     // CTRL1: Control Register 1 Address
    NAU7802_CTRL2 = 0x02,     // CTRL2: Control Register 2 Address
    NAU7802_I2C_CTRL = 0x11,  // I2C_CTRL: I2C Control Register Address
    NAU7802_ADC_B2 = 0x12,    // ADC_OUT_B2: ADC Conversion Result [23:16] Address
    NAU7802_ADC_B1 = 0x13,    // ADC_OUT_B1: ADC Conversion Result [15:08] Address
    NAU7802_ADC_B0 = 0x14,    // ADC_OUT_B1: ADC Conversion Result [07:00] Address
    NAU7802_ADC_REG = 0x15,   // ADC_REG: ADC Register Address
    NAU7802_PGA_REG = 0x1B,   // PGA_REG: Pre-Gain Amplifier Control Register Address
} nau7802_reg_address_t;

float nau7802_counts_to_mv(int32_t const raw_counts, const uint32_t vldo_index, const int32_t gain);

int nau7802_power_up(const struct i2c_dt_spec *i2c);

int nau7802_power_sequence(const struct i2c_dt_spec *i2c);

int nau7802_set_adc_channel(const struct i2c_dt_spec *i2c, nau7802_chs_t channel);

int nau7802_set_ldo_voltage(const struct i2c_dt_spec *i2c, nau7802_vldo_t ldo_voltage);

int nau7802_enable_ldo(const struct i2c_dt_spec *i2c);

int nau7802_set_clock_source(const struct i2c_dt_spec *i2c, nau7802_oscs_t clock_source);

int nau7802_set_gain(const struct i2c_dt_spec *i2c, nau7802_gains_t gain);

int nau7802_disable_bw_chopper(const struct i2c_dt_spec *i2c);

int nau7802_set_conversion_rate(const struct i2c_dt_spec *i2c, nau7802_crs_t conversion_rate);

int nau7802_disable_weak_pullup(const struct i2c_dt_spec *i2c);

int nau7802_start_adc(const struct i2c_dt_spec *i2c, bool enable);

int nau7802_read_conversion_result(const struct i2c_dt_spec *i2c, int32_t *result);

int nau7802_set_calibration_mode(const struct i2c_dt_spec *i2c, nau7802_calmod_t mode);

int nau7802_calibrate(const struct i2c_dt_spec *i2c);
