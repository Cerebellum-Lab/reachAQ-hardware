# IO module and system timing — design

Date: 2026-10-07
Status: design approved section by section in conversation; awaiting spec review
Repos: `reachAQ-hardware` (boards, firmware, protocol header), `reachAQ` (host)

## 1. Goal

Give the rig 8-48 more inputs and outputs without changing what already works, and
put every board's signals on the same timeline as the cameras and tones to within
0.1 ms.

Two deliverables, built as two parallel tracks:

- **Timing track.** Board timestamps, time-sync, a CAN "clap", and a barcode into the
  NI-DAQ. Delivered first on the existing pellet board as additive firmware.
- **Hardware track.** A new CAN "IO module" (helper board) on the existing JerryCAN
  bus. It is also the system's timekeeper.

### Requirements

| Item | Decision |
|---|---|
| Alignment accuracy | 0.1 ms (the NI-DAQ's 10 kHz sample period) |
| IO mix | 12 digital in, 8 TTL out, 8 analog in, 4 power out ("Balanced 32") |
| Levels | 5 V logic, 0-5 V analog, 12 V power outputs |
| Compatibility | Purely additive. Pellet changes are capability-gated. Existing host code keeps working. |
| DAQ link | The IO module generates the barcode on PXI-6221 `port0/line3`, which is currently unwired |
| Analog rate | 1 kHz per channel, all 8 channels (ceiling about 2 kHz, set by the bus budget) |

### Non-goals

- No change to pellet pins, existing message IDs, existing payload layouts, send rates,
  or motor/tone/pulse behaviour.
- No new transport (USB or Ethernet). Timestamps replace the need for a faster link.
- No CAN-controller hardware timestamps on the boards in v1. Interrupt-time stamps meet
  the budget, and hardware timestamps remain the upgrade path (section 12).

## 2. Facts this design rests on

| Fact | Source |
|---|---|
| reachAQ already defines `TIME_SYNC_REQUEST` 0x1F (12 B `<IQ`) and `TIME_SYNC_RESPONSE` 0x20 (20 B `<IQQ`). Firmware implements neither. | `reachAQ/.../socketcan_jerrycan.py`; `firmware/lib/jerrycan/modules/capabilities.c` header comment |
| reachAQ already parses a 20 B timing trailer (`<BBBBIIQ`: tag 0xA7, version 1, kind, flags, boot_id, sequence, board_time_us) that sits after the uuid byte | `socketcan_jerrycan.py` `decode_frame` |
| reachAQ already fits an affine board→host clock (`BoardClockModel`, slope bound ±1000 ppm, min-RTT filtering) | `reachAQ/.../board_clock.py` |
| The reachAQ allowlist lists `timing_trailer` and `time_sync` as optional only because no firmware implements them, and says to make them required when one does | `reachAQ/config/pellet-firmware-compatibility.yaml` |
| Capability bits 0-3 are taken (`timing_trailer`, `time_sync`, `finite_stim3_pulse`, `pellet_precheck`). Bits 4 and up are free. | `reachAQ/tools/acquisition/model/firmware_compatibility.py` |
| Message IDs 0x25-0x29 are unused in both firmware and host enums | `jerrycan_types.h`; `socketcan_jerrycan.py` |
| **Current reachAQ raises `CanTransportReadError("Malformed JerryCAN frame")` on any unknown message type**, and `_addr2tgt` raises on any non-pellet address | `socketcan_jerrycan.py` receive loop; `can_interface.py` |
| Old pellet firmware ignores unknown types: its RX filter accepts only its own node ID, and its size table spans all 64 IDs | `firmware/lib/jerrycan/jerrycan.c` |
| Board type is bits 2-3 of the 5-bit node address. Pellet is type 0, and the magnet board appears to be type 1. | `can_interface.py` `_is_pellet_by_addr`; magnet PCB strap nets |
| Rig CAN: PEAK PCIe-FD (`peak_pciefd`), 1 Mbit/s nominal, 5 Mbit/s data, FD on | `ip -details link show can0` on christielab10 |
| The PEAK card returns hardware RX timestamps at 1 µs resolution. Hardware time shows pressure frames exactly 12.000 ms apart; kernel software stamps jitter about 40 µs and merge frames that were 74 µs apart. | Receive-only `SO_TIMESTAMPING` probe on christielab10, 2026-10-07 |
| PXI-6221 buffered DI is `port0` only. Lines 0-3 are tone1, tone2, cam_frames and barcode; 4-5 are shutters; 6-7 are free but earmarked for laser readback. DAQ sampling is 10 kHz. | `reachAQ/planning.md`; `docs/linux-install/ni-daq-pxi.md` |
| The pellet build has a 64-bit cycle counter at 170 MHz (`CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER=y`) | Rig build `.config` |
| Pellet power: 12 V from the CAN cable (`CAN_PWR_IN`) or a barrel jack, diode-ORed (UPS840), then TPS565208 → 5 V and TPS561201 → 3.3 V. CAN uses ATA6561 on Nano-Fit 2x2 connectors carrying 12 V. | `hardware/pellet_module/pellet.kicad_pcb` |

## 3. Architecture

The IO module is the **timekeeper**. The host does all the clock arithmetic.

```
pellet clock ──clap #N (µs)──► IO-module clock ──barcode #N (0.1 ms)──► NI-DAQ sample index ──► cameras, tones
      │                               │
      └────── TIME_SYNC (fallback) ───┴──────────────────► host clock
```

- Once a second the IO module runs **tick N**. It sends `SYNC_CLAP(N)` on CAN and plays
  `barcode(N)` on DAQ `port0/line3`.
- Every board stamps the end-of-frame moment of clap N on its own clock and reports it.
  The IO module stamps its own clap when transmission completes. All of these stamps
  refer to the same physical instant.
- The host fits pellet→IO module from claps, and IO module→DAQ from barcode edges, then
  composes them. TIME_SYNC (host↔board, NTP-style) stays as the fallback tier.

**Rejected alternatives**
- *Host as timekeeper.* The host-clock→DAQ link is software-timed, and reading the PEAK
  card's hardware stamps needs custom socket code that python-can does not expose.
- *TIME_SYNC plus barcode, no clap.* Pellet accuracy would be bounded by the CAN round
  trip and could miss 0.1 ms.

## 4. Protocol (wire contract)

All integers are little-endian and packed. `wire_schema_version` stays **1**, because no
existing frame changes meaning.

### 4.1 Timing trailer (existing host format, now emitted by firmware)

The trailer follows the uuid byte on **every board-originated frame** whose payload is
43 B or less (that is, payload + uuid + 20 ≤ 64). There are no per-message exceptions:

| Field | Type | Value |
|---|---|---|
| tag | u8 | `0xA7` |
| version | u8 | `1` |
| kind | u8 | `TX_QUEUE 0`, `OBSERVED 1`, `COMMAND_RECEIVED 2`, `PHYSICAL_START 3`, `COMPLETED 4`, `SAMPLED 5` |
| flags | u8 | bit0 `COALESCED`: events were merged by the rate cap |
| boot_id | u32 | Random per boot; the same value reported in `CAPABILITIES_RESPONSE` |
| sequence | u32 | Per-board counter of trailer-bearing frames |
| board_time_us | u64 | Board clock, µs since boot |

`kind` says what `board_time_us` marks. Old host code ignores the bytes after the uuid.

### 4.2 New and reused messages

| ID | Name | Direction | Payload | Trailer |
|---|---|---|---|---|
| 0x1F | `TIME_SYNC_REQUEST` | host → board | `u32 request_id, u64 host_send_perf_ns` (exists in host) | — |
| 0x20 | `TIME_SYNC_RESPONSE` | board → host | `u32 request_id, u64 request_receive_time_us, u64 response_queue_time_us` (exists in host) | `TX_QUEUE`, informational only; the timing is in the payload |
| **0x25** | `SYNC_CLAP` | timekeeper → all | `u32 tick` | `TX_QUEUE`, informational only; the clap's real time is in the clapper's own `SYNC_CLAP_OBSERVED` |
| **0x26** | `SYNC_CLAP_OBSERVED` | each board → host | `u32 tick, u8 clapper_node` | `OBSERVED` = end-of-frame time of the clap |
| **0x27** | `SYNC_BARCODE` | timekeeper → host | `u32 tick` | `PHYSICAL_START` = rising edge of the start pulse |
| **0x28** | `ANALOG_BLOCK` | IO module → host | `u8 instance, u8 channel_count, u8 scan_count, u8 flags, u16 scan_period_us, u16 samples[16]` (38 B, scan-major) | `SAMPLED` = trigger time of the first scan |
| **0x29** | `PWM_SET` | host → IO module | `u8 channel, u16 duty_permyriad, u32 period_us` (`period_us` 0 = keep; a non-zero value applies to **all 4** channels, which share one timer) | — (acknowledged with `RSP_ACK`) |
| reused | `GPIO_READ/WRITE/PULSE/PULSE_STATUS`, `STATUS`, `HEARTBEAT`, `CAPABILITIES_*`, `ESTOP` | unchanged | unchanged | as listed in sections 5 and 7 |

Analog samples are raw 12-bit counts. The host converts them to connector volts with
`count / 4095 × 3.3 / divider_ratio`. The divider ratio is a board constant, recorded in
host configuration.

### 4.3 Capability bits (new)

- bit 4 `sync_listener`: the board reports the claps it hears.
- bit 5 `timekeeper`: the board sends claps and the barcode.

### 4.4 Addresses

The IO module is **board type 2** (addresses 8-11). The low 2 bits come from a DIP
switch. The board type is a firmware/devicetree constant, not strap pins.
`SYNC_CLAP` carries the clapper's own address, as with every other board-originated frame.

### 4.5 Barcode waveform (DAQ `port0/line3`, 5 V TTL)

Each tick: a 1 ms high start pulse, then the 32-bit tick number LSB-first at 1 ms per bit
(high = 1), then low. That is 33 ms per second. A hardware timer driven by DMA generates
it, so every edge has a known board time. The host uses **every edge**, not only the
start edge, which averages away the DAQ's ±50 µs quantisation.

### 4.6 Bus budget (1 Mbit/s nominal, 5 Mbit/s data)

| Traffic | Rate | Approximate load |
|---|---|---|
| Sync (clap + 2 observed + barcode report) | 4 frames/s | negligible |
| Pellet trailers (frames grow from ≤8 to ≤32 B) | about 280 frames/s already | +1% |
| `ANALOG_BLOCK` at 1 kHz, 8 ch | 500 frames/s (64 B DLC) | about 8% |
| IO-module digital events | burst-capped at 2,000/s | worst case about 15% during bursts |

These are estimates, to be confirmed by measurement in acceptance (section 11).

## 5. Pellet firmware v2.4.0 (additive)

A new shared module, `firmware/lib/jerrycan/modules/timing.c` (`CONFIG_LIB_JERRYCAN_TIMING`,
default `y`), used by both boards:

- **Board clock:** `k_cycle_get_64()` / 170 → µs. It uses no timer peripheral; all of
  the pellet's 32-bit timers are taken. `boot_id` moves here from `capabilities.c` so
  both share one value.
- **TIME_SYNC responder:** a dedicated `can_add_rx_filter` for 0x1F addressed to this
  node. Its callback (ISR context) stamps `request_receive_time_us` and queues the reply.
  `response_queue_time_us` is stamped when the reply is queued.
- **Clap listener:** a dedicated filter for type 0x25 from **any** node. The ISR stamps
  it, and the main loop sends `SYNC_CLAP_OBSERVED`. ISR work is limited to one stamp and
  one `k_msgq_put`.
- **Trailer:** appended in `jerrycan_run`, where every frame is built, for payloads of
  43 B or less. The default kind is `TX_QUEUE`, with the time `jerrycan_tx()` was called.
  A new `jerrycan_tx_timed(msg, kind, time_us)` carries better times through an internal
  queue item. **The shared `jerrycan_msg_t` is not changed**, so the host C++ library
  and pyjerrycan are untouched.
- **Better times:**
  - `PRESSURE_READ`: `SAMPLED`, stamped in the ADC callback
  - `GPIO_READ` and `DOOR_SENSOR` periodic state: `SAMPLED`, when the state was read
  - `GPIO_PULSE_STATUS`: `PHYSICAL_START` on assert and `COMPLETED` on return-low
  - tone: `PHYSICAL_START` when DAC playback and the Tone 1/2 TTL start
  - everything else: `TX_QUEUE`
- **Capabilities:** sets `timing_trailer`, `time_sync`, `sync_listener`.
- **Node ID:** `get_can_node_id()` reads the board type from devicetree when a property
  is present, and falls back to the current strap read otherwise. The pellet behaviour
  is unchanged.
- **Unchanged:** pins, IDs, payload layouts, rates, motor, servo, tone and pulse
  behaviour, MCUboot, and the flash layout.
- **Safe on today's reachAQ:** it already parses the trailer and time-sync, and
  `SYNC_CLAP_OBSERVED` is only sent when a timekeeper is clapping. The one required host
  change is a `2.4.0` allowlist entry, because unknown versions are blocked.

## 6. IO module hardware

**MCU:** STM32G474RET6, LQFP-64 (same part and toolchain as the pellet board).

**Copied from the pellet design:**
- 12 V input from the CAN cable or a barrel jack, diode-ORed, then 5 V and 3.3 V bucks
- ATA6561 CAN transceiver, two Nano-Fit 2x2 connectors, termination jumper
- W25Q16 SPI flash for the MCUboot secondary slot
- Tag-Connect ST-Link and LPUART1 debug port
- 8 MHz oscillator, reset and BOOT0 buttons, status LED, 2-position ID DIP switch

**Changed from the pellet:**
- Board type is set in devicetree.
- **VBAT tied to VDD.**
- No USB.

**IO front ends**

| Group | Count | Circuit | Connector |
|---|---|---|---|
| Digital in | 12 | 74LVC buffer at 3.3 V (5 V-tolerant inputs), series resistor, pull-down, ESD. Each input on a **distinct EXTI line**. | 3-pin JST-XH (5 V, GND, SIG) |
| TTL out | 8 | 74AHCT244 at 5 V, 33 Ω series | BNC |
| Barcode out | 1 | TIM3_CH2 → AHCT buffer | BNC → DAQ `port0/line3` |
| Analog in | 8 | 0-5 V → divider to about 0-3.0 V (e.g. 6.8k/10k), RC anti-alias filter sized for 1 kHz, clamp diodes. **All on ADC1.** About 17 kΩ input impedance. | 3-pin JST-XH (5 V, GND, SIG) |
| Power out | 4 | TBD62783A high-side 12 V, PWM on TIM1 CH1-4, flyback diodes | 2-pin screw terminal |

**Pin feasibility:** checked against ST's open pin data for `STM32G474R(B-C-E)Tx`.
- **Fixed (same as pellet), 13 pins:** PG10 NRST, PB8 BOOT0, PF0 OSC_IN, PA13/PA14 SWD,
  PC0/PC1 LPUART1, PB3/PB4 FDCAN3, PA15/PC10/PC11/PC12 SPI3.
- **One verified assignment of the other 39:**
  - ADC1: PA0, PA1, PA2, PA3, PB0, PB1, PB11, PB12
  - TIM1 PWM: PA8, PA9, PC2, PC3
  - Barcode: PA4 (TIM3_CH2)
  - DI on 12 distinct EXTI lines: PF1, PB2, PC4, PA5, PA6, PA7, PC8, PC9, PB10, PA11, PA12, PC13
  - Remaining 14 (PC14, PC15, PC5, PB13, PB14, PB15, PC6, PC7, PA10, PD2, PB5, PB6, PB7, PB9)
    cover 8 TTL outs, the LED and 2 ID inputs, leaving 3 spares.
- PC13-PC15 are low-drive pins, so they are used **only as inputs**. The final
  assignment is re-optimised for routing in the schematic phase, keeping these
  constraints.

**Power:** 4 × 0.5 A at 12 V is 2 A. Feed valve loads from the barrel jack, not through
the CAN cable.

## 7. IO module firmware

New app `firmware/io_module/` and board `boards/arm/cerebellumlab_io_module/`. Reuses
jerrycan (heartbeat, status, capabilities, bootloader, gpio), the `timing` module, and
the `ll,generic-gpios` driver.

- **Timekeeper module:** a timer interrupt at 1 Hz runs tick N.
  - `can_send()` the `SYNC_CLAP`; the TX-complete callback stamps the time and sends its
    own `SYNC_CLAP_OBSERVED`.
  - Load the barcode waveform into TIM3 + DMA, and report `SYNC_BARCODE` with the start
    edge time. TIM3 and the cycle counter share the 170 MHz clock, so the edge time is
    computed rather than sampled.
- **Digital in:** EXTI on both edges, timestamped in the ISR, sends `GPIO_READ`
  immediately with `OBSERVED`. The existing 100 ms periodic state continues as
  `SAMPLED`. A 2,000 msg/s cap merges bursts and sets trailer flag `COALESCED`.
- **TTL out:** existing `GPIO_WRITE` and `GPIO_PULSE` (counter-timed, as on pellet), and
  `GPIO_PULSE_STATUS` with `PHYSICAL_START` / `COMPLETED`.
- **Analog:** TIM6 TRGO at 1 kHz triggers an ADC1 8-channel scan into a circular DMA
  buffer. Every 2 scans → one `ANALOG_BLOCK`, with time computed from the trigger count.
  Sequence numbers expose lost frames.
- **Power out:** `PWM_SET` → TIM1 CH1-4, with a shared period.

## 8. Host changes (reachAQ)

1. **Tolerance (ships first):** unknown message types and unknown board types are
   logged once per ID and skipped. They no longer raise.
2. Decoders for 0x25-0x28, an encoder for 0x29, and capability names `sync_listener`
   and `timekeeper`.
3. **Clock chain:**
   - fit pellet→IO module from `SYNC_CLAP_OBSERVED` pairs by tick
   - fit IO module→DAQ from barcode edges decoded out of `nidaq.h5` line 3, paired with
     `SYNC_BARCODE`
   - compose the two
   - add this as a new alignment tier between "NI physical edge" and "board clock
     model". Raw evidence is never overwritten, matching the existing policy.
4. An IO-module device model; digital, analog and output streams in the session
   recording; output control API (TTL write and pulse, PWM).
5. Allowlist: pellet `2.4.0` with `timing_trailer` and `time_sync` **required**, and a
   new `board_type: io_module` `1.0.0`.

## 9. Safety and error handling

- IO-module power outputs are **off at boot, on `ESTOP`, and after more than 2 s without
  a host heartbeat**. TTL outputs are low at boot and otherwise hold their last state,
  matching the pellet's STIM outputs. Revisit this if a TTL output ever drives something
  hazardous.
- A missing clap or barcode tick leaves a gap; the host fit uses a sliding window and
  reports uncertainty. A `boot_id` change resets that board's models.
- A reset of the timekeeper restarts tick numbering. Ticks pair with `boot_id`, never
  alone.
- Clap and TIME_SYNC times are taken in the ISR, so main-loop delay before
  `SYNC_CLAP_OBSERVED` is sent does not affect accuracy. If the stamp queue overflows,
  the clap is dropped and the host sees a missing tick.
- Bus-off on the IO module: outputs are made safe, and it recovers with the existing
  restart behaviour.

## 10. Rollout order

1. reachAQ tolerance, decoders and allowlist entries.
2. Pellet v2.4.0. This can also ship before step 1 as long as no timekeeper is on the bus.
3. IO module joins the bus.

Each step leaves the system working if the next one never ships.

## 11. Testing and acceptance

- **Dev hardware first:** the IO-module firmware is developed on a NUCLEO-G474RE (same
  MCU) with a CAN transceiver breakout, on the real rig bus, before the PCB exists.
- **Automated:**
  - firmware native tests for trailer packing, barcode waveform generation and
    `ANALOG_BLOCK` packing
  - reachAQ pytest for decoders, unknown-type tolerance, the clap fit, and the barcode
    decoder against synthetic `nidaq.h5` data (noise, dropped ticks, `boot_id` change,
    tick wrap)
- **Rig acceptance (pass/fail):**
  1. **End-to-end:** map the pellet Tone 1/2 `PHYSICAL_START` through clap → barcode and
     compare it with the tone edge already recorded on DAQ `port0/line0-1`.
     **Pass: |error| 95th percentile < 0.1 ms over ≥ 100 tones.**
  2. Loopback: IO-module TTL out → DI. The measured delta matches the commanded pulse
     within 20 µs.
  3. Bus load measured at 1 kHz analog plus pellet traffic. Pass: < 30% sustained, and
     no pellet frame loss (PEAK `dropped` counter unchanged).
  4. Pellet regression: the existing qualification evidence run passes on v2.4.0.
- **PCB:** ERC/DRC clean, power budget, TBD62783A thermal at 4 × 0.5 A, and a bring-up
  checklist (rails, SWD, CAN, each IO group, barcode on a scope).

## 12. Risks and open items

| Risk | Mitigation |
|---|---|
| ISR stamping jitter is estimated (a few µs), not measured | Acceptance test 1 measures it. Fallback: FDCAN RX/TX-event hardware timestamps (16-bit, extended in software). |
| DAQ quantisation of ±50 µs per edge | Fit over all barcode edges (33 per tick) |
| Oscillator drift and temperature | Sliding-window affine fit; the host already bounds slope to ±1000 ppm |
| Host-side clap stamps use kernel software time (±40 µs jitter) | Not on the critical path, which runs through the DAQ. PEAK hardware stamps are an optional host upgrade. |
| Barcode BNC must be wired to PXI-6221 `port0/line3` on each rig | Rig setup step, verified by the decoder in acceptance |
| Magnet board type is inferred, not verified | Confirm before assigning type 2 permanently |

## 13. Implementation plans

Each item gets its own plan, executed in this order within its track:

- **Timing track**
  - T1 reachAQ tolerance + decoders + allowlist
  - T2 shared `timing` module + pellet v2.4.0
  - T3 reachAQ clock chain + barcode decoder
- **Hardware track**
  - H1 IO-module firmware on NUCLEO-G474RE
  - H2 IO-module schematic and PCB
  - H3 bring-up and acceptance
