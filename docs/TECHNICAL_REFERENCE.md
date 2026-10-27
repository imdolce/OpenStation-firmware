# OpenStation technical reference

This is a source-based reference for **0.1.0 Build 14**. [`src/`](../src/) and [`include/`](../include/) are authoritative for present behavior. The files under [`scrap/`](../scrap/) include historical samples and plans; their descriptions are not necessarily implemented now. The [firmware datasheet](FIRMWARE_DATASHEET.md) is a compact behavior summary; the historical MCU document in `scrap/` describes silicon, not these firmware features.

## Startup and execution

[`main.c`](../src/main.c) calls `board_safe_boot()` before LCD setup so **PB7 is low and the heater is off**. It initializes the ST7735, clears controller RAM, then initializes board inputs, TIM1 power timing, stored settings, ADC measurement, safety, control, and UI. It renders the screen, plays a boot sound, and starts the watchdog. Failure to initialize the LCD leaves the code holding the heater off.

The main loop processes ADC results and input events, runs control when a new quiet-window temperature sample arrives, supervises safety on heater-cycle events, refreshes the display every **100 ms**, and feeds the watchdog. Interrupts handle timing-critical heater switching, ADC progression, the millisecond tick, and buzzer timing. UI fields use partial redraw/caching where practical.

| Module | Responsibility |
| --- | --- |
| [`board.c`](../src/board.c), [`encoder.c`](../src/encoder.c) | GPIO, tick/watchdog, buzzer/backlight, encoder/button events |
| [`power.c`](../src/power.c) | TIM1 heater pulse, current sample and quiet-sample compare events, duty limiting |
| [`measure.c`](../src/measure.c) | ADC1 sequence, filtered sensor values, tip-open detection |
| [`control.c`](../src/control.c) | State machine, PID, presets, boost, stand/VIB sleep |
| [`safety.c`](../src/safety.c) | Fault checks, latched shutdown, fault snapshot |
| [`profiles.c`](../src/profiles.c), [`settings.c`](../src/settings.c) | Factory/custom tip data and flash settings |
| [`ui.c`](../src/ui.c), [`st7735.c`](../src/st7735.c) | Menus, screens, ST7735 drawing and offsets |

## Heater timing and ADC quiet window

[`app_config.h`](../include/app_config.h) sets a **100 kHz** TIM1 timebase and **5000 ticks** per heater cycle: **50 ms / 20 Hz**. The configured pulse is limited to **4400 ticks / 44 ms**, leaving at least **6 ms off** each cycle. PB7 **high** turns the high-side P-channel MOSFET stage on through its NPN gate driver; PB7 low turns it off. The code starts a pulse at the TIM1 update event, samples PA6 current near **tick 100 (1 ms)** during an active pulse, turns the heater off at a compare event, and schedules the quiet ADC trigger **500 ticks (5 ms)** after the pulse ends. These are firmware timings, not oscilloscope-verified switch or op-amp settling times.

The quiet sequence samples PA1 tip temperature only while the heater is commanded off. It discards the first conversion after switching ADC channels, takes **19 temperature conversions**, drops the highest and lowest, and averages the remaining 17. It then samples PA0 voltage and PA6 off-current, also discarding initial channel-switch conversions. Temperature and voltage pass through a three-cycle median filter. If the heater is still on, or the quiet sample is missing/stale, a new heat command cannot use it. The control loop runs on each accepted quiet sample, nominally **20 times per second**. The TIM1 schedule, rather than display refresh, determines this rate.

## Measurements and control math

- **Supply voltage:** PA0 raw ADC → `raw × 3300 mV / 4095` at the pin → multiply by configured divider ratio **16.3**. This ratio is a source setting and needs board calibration.
- **Temperature:** PA1 ADC voltage is converted to microvolts using a 3.3 V, 12-bit model and 64-bit intermediate arithmetic. The code divides by the selected profile's `temp_uv_per_c`, applies its offset, then applies six-point interpolation from that profile. The six anchors are 100, 200, 300, 350, 400, and 450 °C. No independent cold-junction sensor is read by this implementation. An ADC value **≥3900** marks the tip open; **≤3800** restores presence, giving hysteresis. Temperature scale and six-point factory data are provisional, not certified accuracy.
- **Current:** PA6 is read during an on pulse; the zero-current baseline is calibrated while off. The ADC delta is converted with configured **340 µV/mA** at the ADC input. This is an empirical firmware scale, not a verified amplifier/shunt specification. An off-pulse PA6 sample separately checks for unexpected current.
- **Displayed average current:** `I_on × actual_on_ticks / 5000`. **Displayed power:** `VIN × I_average`; the code converts mV × mA into mW. These are estimated electrical averages from one on-pulse sample and commanded pulse duration, not direct continuous integration or a calorimetric heater-power measurement.
- **Duty limiting:** the requested PID duty is bounded by the active profile's maximum duty, configured maximum power and nominal resistance (`P_max × R_nom / VIN²`), rated-voltage rule (`V_rated² / VIN²`), the 44 ms pulse cap, and validity/fault gates. A factory `max_power_mw` of **400000** means a configured **400 W ceiling**, not a demonstrated safe continuous output. The other bounds may reduce effective duty.

The active profile contains Q8 PID constants. The controller computes proportional error, integrates with anti-windup, applies derivative to the measured temperature, clamps the request to 0–1000 permille, and limits an upward change to 100 permille per sample. Build 14's factory C245 constants are **Kp 8, Ki 2, Kd 0.5** in Q8 notation. Control uses the *selected* profile; `TIP_IDENTIFY` is only a short settling state and performs **no automatic resistance identification**.

## States and protection

The state machine declares `BOOT_SAFE`, `NO_TIP`, `TIP_IDENTIFY`, `IDLE`, `HEAT_UP`, `REGULATE`, `BOOST`, `SLEEP`, `COOLING`, and `FAULT_LATCHED`. A click in IDLE with a present tip, valid quiet sample and valid supply starts heating. `HEAT_UP` becomes `REGULATE` within 5 °C of setpoint. A double-click while heating starts boost only when its configured increment is nonzero. Boost adds **0–100 °C** (5 °C menu steps) to the base setpoint, bounded by station and tip limits, for a configured **15–120 s** (5 s steps); defaults are **+50 °C for 30 s**. A further double-click adds that duration to the *remaining* boost time, saturated at **5999 s (99:59)** to fit the two-digit-minute display. It does not compound the temperature increment. A single click shuts the heater off; encoder rotation changes the base setpoint and exits Boost. The status box stays yellow with black **Boost** and `MM:SS`, while the Set readout shows the effective boosted target in red and a red parenthetical actual increment such as `(+60°C)`. Profile changes force heat off. Stand low begins a 0–30 s delay; VIB low after arming causes immediate sleep; sleep time is 30–1800 s and then turns heating off. See the [user guide](USER_GUIDE.md#sleep-and-wake) for screen behavior.

Active checks in [`safety.c`](../src/safety.c) include: missing/stale quiet ADC data (**200 ms** threshold after first sample), tip open/very low PA1 raw, selected-family supply range, measured temperature more than **15 °C** above the profile limit or above **515 °C**, off-current above **500 mA** for two samples, and a temperature drop over **100 °C within 300 ms** while heating. TIM1 also has a pulse-timeout shutdown. Faults latch PB7 off and capture a snapshot. The Sensor Drop fault needs reset and inspection. `Ignore Faults` bypasses most supervisor checks, but **ADC Stale and Pulse Time remain enforced**. `FAULT_OVERCURRENT` and `FAULT_NO_CURRENT` exist as enum names, but Build 14 does not implement an instantaneous current ceiling or a normal no-current trip.

Build 14 has `STATION_HEATER_OUTPUT_ENABLE=1` and `STATION_SERVICE_ALLOW_UNCHARACTERIZED=1`. Factory profiles have `characterized=0`; the latter setting permits them to heat for development. Neither a 400 W software ceiling nor enabled heating validates the MOSFET, supply, wiring, or cartridge limits.

## Persistent flash

The [`linker script`](../scrap/linker.ld) allocates **52 KiB** from `0x08000000` for application code and leaves the upper flash region for data. [`profiles.c`](../src/profiles.c) uses nine 1 KiB pages from **`0x0800D000`** for the nine tip families, each with ten 96-byte custom-profile slots and a checksum. [`settings.c`](../src/settings.c) stores a checksummed, versioned record in the 1 KiB page at **`0x0800F400`**. It reads valid saved values at startup; compiled defaults are used when there is no valid record. The current settings format remains **version 9 (616 bytes)**; Build 13 saves are read without migration. Version 8 settings are migrated in memory and receive the +50 °C / 30 s defaults; saving writes version 9. Earlier supported formats also have migration paths. `Save to Flash` writes current settings, and Factory Reset restores defaults, erases custom profiles, and saves the reset settings. A full-chip erase or a different linker layout can remove stored profiles/settings.
