# Pellet board pressure sensors

The pellet board exposes two force-sensing-resistor (FSR) pressure streams over
JerryCAN. Each sensor is a simple resistive divider read by an on-chip ADC.

| Stream | Connector | MCU pin | ADC        | Divider                  |
| ------ | --------- | ------- | ---------- | ------------------------ |
| 0      | `J11`     | `PA0`   | `ADC1_IN1` | On board: R22 10k, C32 0.1 uF |
| 1      | `J21`     | `PA6`   | `ADC2_IN3` | External, built into the cable |

Both sensors sample at 80 Hz. The board transmits every
`CONFIG_LIB_JERRYCAN_PRESSURE_TX_PERIOD_MS` (default 12 ms).

## Wiring

### `J11` (stream 0)

R22 (10k) and C32 (0.1 uF) are already populated on the board, so the connector
needs nothing but the sensor:

1. Solder the FSR leads to a BNC plug: one tab to the center, one tab to the shell.
2. Plug into `J11`.

Before first use, meter `J11` center to U1 pin 12 to confirm the net, and label
the 3V3 wire.

### `J21` (stream 1)

`J21` has no divider on the board, so build one in a small inline box or in the
plug shell:

1. 10k from the 3V3 wire to the BNC center.
2. 0.1 uF from the BNC center to the shell.
3. FSR from the BNC center to the shell, via the cable out to the sensor.
4. 3V3 wire to pin 1 of a spare `DOOR` port (PicoBlade pigtail, leave pin 2
   unconnected), or `TP3` if every door is in use.
5. Plug into `J21`.

```text
3V3 ──[10k]──┬── center
             │
          [0.1µF]        FSR ── shell (GND)
             │            │
           shell ─────────┘
```

## CAN stream format

Each sensor is transmitted as a `JERRYCAN_CMD_PRESSURE_READ` message:

```c
typedef struct __attribute__((packed)) {
    uint8_t instance;
    uint32_t pressure;
} jerrycan_cmd_pressure_read_t;
```

`instance` is the devicetree instance index: `0` for `J11`, `1` for `J21`.

`pressure` is the **raw 12-bit ADC count**, 0 through 4095 — not millivolts.
The docstring in `lib/jerrycan/modules/pressure.c` describes the value as mV,
but `ll_pressure_sensor_get_pressure()` returns the unconverted ADC sample.
Convert host-side if you need volts:

```text
volts = pressure / 4095.0 * 3.3
```

## Why the two sensors use different ADCs

`PA6` can only reach `ADC2_IN3`, but `PA0` can route to either `ADC1_IN1` or
`ADC2_IN1`. The sensors are deliberately split across `adc1` and `adc2` rather
than sharing `adc2`.

The driver samples continuously: `ll_pressure_sensor_enable()` starts an
`adc_read_async()` whose callback returns `ADC_ACTION_REPEAT`, so the sequence
never completes. In Zephyr, `adc_context_release()` deliberately does *not*
release the context lock for an asynchronous read that started successfully, so
the first instance on a given ADC holds that peripheral's lock for its entire
lifetime.

Two `ll,pressure-sensor` nodes on one ADC therefore deadlock: the second
instance's `POST_KERNEL` init blocks forever in `k_sem_take(&ctx->lock,
K_FOREVER)` and the board never finishes booting. Splitting across two ADC
peripherals gives each instance its own context and lock.

The tradeoff is that the two channels run on independent sampling timebases, so
samples are not strictly aligned. At 80 Hz this is not significant.

`die_temp`, `vref`, and `vbat` are also on `adc1`. No firmware reads them, so
they do not contend in practice, but a `sensor get` for any of them from the
shell would block on the `adc1` lock. The magnet board has shipped this same
arrangement since its pressure sensor was added.

## Behavior changes

Adding these streams required freeing `PA0` and `PA6`:

- **`ext_button_1` was removed.** `PA0` was a GPIO key. The `external_button`
  field in `JERRYCAN_CMD_DOOR_SENSOR` is retained so the message layout is
  unchanged, but it is now always `0`. Hosts that watch that bit will never see
  it assert.
- **`analog_out` and `dac2` were removed.** `PA6` was `DAC2_OUT1`. The
  `JERRYCAN_CMD_ANALOG_OUT` module is compiled out on this board, so the pellet
  board no longer answers analog-out commands or publishes analog-out status.
  `dac1` (the tone generator) is untouched.

## Verification

Build and flash using the standard workflow in
[Pellet firmware release and deployment](pellet-firmware-release-and-deployment.md).

From the board shell:

```text
pressure_sensor read 0
pressure_sensor read 1
```

Each should report a stable count that moves when the corresponding FSR is
pressed. An unconnected input floats and may read near 0 or near 4095.

Over CAN, confirm that `JERRYCAN_CMD_PRESSURE_READ` arrives for both
`instance = 0` and `instance = 1`.
