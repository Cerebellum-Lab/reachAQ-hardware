#include "nau7802.h"

#include <zephyr/sys/byteorder.h>

/* Read the specified NAU7802 Register into the given NAU7802 Register Structure */
static inline int nau7802_read(const struct i2c_dt_spec *i2c, nau7802_reg_address_t reg, nau7802_reg_t *value) {
    /* Read current value of register */
    return i2c_reg_read_byte_dt(i2c, reg, &value->byte);
}

/* Write the given NAU7802 Register Structure into the specified NAU7802 Register */
static inline int nau7802_write(const struct i2c_dt_spec *i2c, nau7802_reg_address_t reg, nau7802_reg_t value) {
    /* Read current value of register */
    return i2c_reg_write_byte_dt(i2c, reg, value.byte);
}

int nau7802_power_up(const struct i2c_dt_spec *i2c) {
    nau7802_pu_ctrl_reg_t pu_ctrl;
    int ret;

    /*******************/
    /* Reset Registers */
    /*******************/

    /* Read current value of PU_CTRL register */
    ret = nau7802_read(i2c, NAU7802_PU_CTRL, (nau7802_reg_t *)&pu_ctrl);
    if (ret != 0) {
        return ret;
    }

    /* Set reset registers bit */
    pu_ctrl.rr = 1;

    /* Write updated value to PU_CTRL register */
    ret = nau7802_write(i2c, NAU7802_PU_CTRL, (nau7802_reg_t)pu_ctrl);
    if (ret != 0) {
        return ret;
    }

    /*************************/
    /* Clear Reset Registers */
    /*************************/

    /* Read current value of PU_CTRL register */
    ret = nau7802_read(i2c, NAU7802_PU_CTRL, (nau7802_reg_t *)&pu_ctrl);
    if (ret != 0) {
        return ret;
    }

    /* Clear reset registers bit */
    pu_ctrl.rr = 0;

    /* Write updated value to PU_CTRL register */
    ret = nau7802_write(i2c, NAU7802_PU_CTRL, (nau7802_reg_t)pu_ctrl);
    if (ret != 0) {
        return ret;
    }

    /********************/
    /* Power up digital */
    /********************/

    /* Read current value of PU_CTRL register */
    ret = nau7802_read(i2c, NAU7802_PU_CTRL, (nau7802_reg_t *)&pu_ctrl);
    if (ret != 0) {
        return ret;
    }

    /* Set power up digital bit */
    pu_ctrl.pud = 1;

    /* Write updated value to PU_CTRL register */
    ret = nau7802_write(i2c, NAU7802_PU_CTRL, (nau7802_reg_t)pu_ctrl);
    if (ret != 0) {
        return ret;
    }

    /* Wait until power up */
    do {
        /* Read current value of PU_CTRL register */
        ret = nau7802_read(i2c, NAU7802_PU_CTRL, (nau7802_reg_t *)&pu_ctrl);
        if (ret != 0) {
            return ret;
        }
        /* Exit loop when PUR bit goes high */
    } while (!pu_ctrl.pur);

    /********************/
    /* Power up analog */
    /********************/

    /* Set power up analog bit */
    pu_ctrl.pua = 1;

    /* Write updated value to PU_CTRL register */
    ret = nau7802_write(i2c, NAU7802_PU_CTRL, (nau7802_reg_t)pu_ctrl);
    if (ret != 0) {
        return ret;
    }

    /* Wait until power up */
    do {
        /* Read current value of PU_CTRL register */
        ret = nau7802_read(i2c, NAU7802_PU_CTRL, (nau7802_reg_t *)&pu_ctrl);
        if (ret != 0) {
            return ret;
        }
        /* Exit loop when PUR bit goes high */
    } while (!pu_ctrl.pur);

    return ret;
}

int nau7802_set_adc_channel(const struct i2c_dt_spec *i2c, nau7802_chs_t channel) {
    nau7802_ctrl2_reg_t ctrl2;
    int ret;

    /* Read current value of the CTRL2 register */
    ret = nau7802_read(i2c, NAU7802_CTRL2, (nau7802_reg_t *)&ctrl2);
    if (ret != 0) {
        return ret;
    }

    /* Set the channel select bit */
    ctrl2.chs = channel;

    /* Write the updated value to the CTRL2 register */
    return nau7802_write(i2c, NAU7802_CTRL2, (nau7802_reg_t)ctrl2);
}

int nau7802_set_ldo_voltage(const struct i2c_dt_spec *i2c, nau7802_vldo_t ldo_voltage) {
    nau7802_ctrl1_reg_t ctrl1;
    int ret;

    /* Read current value of the CTRL1 register */
    ret = nau7802_read(i2c, NAU7802_CTRL1, (nau7802_reg_t *)&ctrl1);
    if (ret != 0) {
        return ret;
    }

    /* Set the channel select bit */
    ctrl1.vldo = ldo_voltage;

    /* Write the updated value to the CTRL1 register */
    return nau7802_write(i2c, NAU7802_CTRL1, (nau7802_reg_t)ctrl1);
}

int nau7802_enable_ldo(const struct i2c_dt_spec *i2c) {
    nau7802_pu_ctrl_reg_t pu_ctrl;
    int ret;

    /* Read current value of PU_CTRL register */
    ret = nau7802_read(i2c, NAU7802_PU_CTRL, (nau7802_reg_t *)&pu_ctrl);
    if (ret != 0) {
        return ret;
    }

    /* Set AVDD source select bit to use internal LDO */
    pu_ctrl.avdds = 1;

    /* Write updated value to PU_CTRL register */
    return nau7802_write(i2c, NAU7802_PU_CTRL, (nau7802_reg_t)pu_ctrl);
}

int nau7802_set_clock_source(const struct i2c_dt_spec *i2c, nau7802_oscs_t clock_source) {
    nau7802_pu_ctrl_reg_t pu_ctrl;
    int ret;

    /* Read current value of PU_CTRL register */
    ret = nau7802_read(i2c, NAU7802_PU_CTRL, (nau7802_reg_t *)&pu_ctrl);
    if (ret != 0) {
        return ret;
    }

    /* Set system clock source select bit */
    pu_ctrl.oscs = clock_source;

    /* Write updated value to PU_CTRL register */
    return nau7802_write(i2c, NAU7802_PU_CTRL, (nau7802_reg_t)pu_ctrl);
}

int nau7802_set_gain(const struct i2c_dt_spec *i2c, nau7802_gains_t gain) {
    nau7802_ctrl1_reg_t ctrl1;
    int ret;

    /* Read current value of the CTRL1 register */
    ret = nau7802_read(i2c, NAU7802_CTRL1, (nau7802_reg_t *)&ctrl1);
    if (ret != 0) {
        return ret;
    }

    /* Set the gain selection bits */
    ctrl1.gains = gain;

    /* Write the updated value to the CTRL1 register */
    return nau7802_write(i2c, NAU7802_CTRL1, (nau7802_reg_t)ctrl1);
}

int nau7802_disable_bw_chopper(const struct i2c_dt_spec *i2c) {
    nau7802_reg_t reg;
    int ret;

    /* Read current value of the ADC_REG register */
    ret = nau7802_read(i2c, NAU7802_ADC_REG, &reg);
    if (ret != 0) {
        return ret;
    }

    /* Disable CLK_CHP by setting both bits of REG_CHPS */
    reg.adc_reg.reg_chps = 0b11;

    /* Write the updated value to the ADC_REG register */
    ret = nau7802_write(i2c, NAU7802_ADC_REG, reg);
    if (ret != 0) {
        return ret;
    }

    /* Read current value of the PGA_REG register */
    ret = nau7802_read(i2c, NAU7802_PGA_REG, &reg);
    if (ret != 0) {
        return ret;
    }

    /* Set PGA Chopper Disabled bit */
    reg.pga_reg.pgachpdis = 1;

    /* Write the updated value to the PGA_REG register */
    return nau7802_write(i2c, NAU7802_PGA_REG, reg);
}

int nau7802_set_conversion_rate(const struct i2c_dt_spec *i2c, nau7802_crs_t conversion_rate) {
    nau7802_ctrl2_reg_t ctrl2;
    int ret;

    /* Read current value of the CTRL2 register */
    ret = nau7802_read(i2c, NAU7802_CTRL2, (nau7802_reg_t *)&ctrl2);
    if (ret != 0) {
        return ret;
    }

    /* Set the conversion rate bits */
    ctrl2.crs = conversion_rate;

    /* Write the updated value to the CTRL2 register */
    return nau7802_write(i2c, NAU7802_CTRL2, (nau7802_reg_t)ctrl2);
}

int nau7802_disable_weak_pullup(const struct i2c_dt_spec *i2c) {
    nau7802_i2c_ctrl_reg_t i2c_ctrl;
    int ret;

    /* Read current value of the I2C_CTRL register */
    ret = nau7802_read(i2c, NAU7802_I2C_CTRL, (nau7802_reg_t *)&i2c_ctrl);
    if (ret != 0) {
        return ret;
    }

    /* Set the weak pull-up disable bit */
    i2c_ctrl.wpd = 1;

    /* Write the updated value to the I2C_CTRL register */
    return nau7802_write(i2c, NAU7802_I2C_CTRL, (nau7802_reg_t)i2c_ctrl);
}

int nau7802_start_adc(const struct i2c_dt_spec *i2c) {
    nau7802_pu_ctrl_reg_t pu_ctrl;
    int ret;

    /* Read current value of PU_CTRL register */
    ret = nau7802_read(i2c, NAU7802_PU_CTRL, (nau7802_reg_t *)&pu_ctrl);
    if (ret != 0) {
        return ret;
    }

    /* Set cycle start bit */
    pu_ctrl.cs = 1;

    /* Write updated value to PU_CTRL register */
    return nau7802_write(i2c, NAU7802_PU_CTRL, (nau7802_reg_t)pu_ctrl);
}

int nau7802_read_conversion_result(const struct i2c_dt_spec *i2c, int32_t *result) {
    nau7802_adc_out_reg_t adc_out;
    int ret;

    /* Perform a burst read of the ADC_OUT[2:0] registers into value */
    ret = i2c_burst_read_dt(i2c, NAU7802_ADC_B2, adc_out.bytes, sizeof(nau7802_adc_out_reg_t));
    if (ret != 0) {
        return ret;
    }

    /* Ensure proper endianness of result */
    *result = sys_be32_to_cpu(adc_out.result);

    return ret;
}

int nau7802_set_calibration_mode(const struct i2c_dt_spec *i2c, nau7802_calmod_t mode) {
    nau7802_ctrl2_reg_t ctrl2;
    int ret;

    /* Read current value of the CTRL2 register */
    ret = nau7802_read(i2c, NAU7802_CTRL2, (nau7802_reg_t *)&ctrl2);
    if (ret != 0) {
        return ret;
    }

    /* Set the calibration mode bits */
    ctrl2.calmod = mode;

    /* Write the updated value to the CTRL2 register */
    return nau7802_write(i2c, NAU7802_CTRL2, (nau7802_reg_t)ctrl2);
}

int nau7802_calibrate(const struct i2c_dt_spec *i2c) {
    nau7802_ctrl2_reg_t ctrl2;
    int ret;

    /*********************/
    /* Start calibration */
    /*********************/

    /* Read current value of CTRL2 register */
    ret = nau7802_read(i2c, NAU7802_CTRL2, (nau7802_reg_t *)&ctrl2);
    if (ret != 0) {
        return ret;
    };

    /* Set start calibration bit */
    ctrl2.cals = 1;

    /* Write updated value to CTRL2 register */
    ret = nau7802_write(i2c, NAU7802_CTRL2, (nau7802_reg_t)ctrl2);
    if (ret != 0) {
        return ret;
    };

    /**********************************/
    /* Wait for calibration to finish */
    /**********************************/

    do {
        /* Read current value of CTRL2 register */
        ret = nau7802_read(i2c, NAU7802_CTRL2, (nau7802_reg_t *)&ctrl2);
        if (ret != 0) {
            return ret;
        }
        /* Exit loop when CALS bit goes low */
    } while (ctrl2.cals);

    /* If calibration error occurred return -EIO */
    if (ctrl2.cal_err) {
        ret = -EIO;
    }

    return ret;
}
