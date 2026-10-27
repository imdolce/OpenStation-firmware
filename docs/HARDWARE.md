# OpenStation hardware map

This page records **what 0.1.0 Build 14 configures and assumes** on the CH32X033F8P6 board. It is not a board acceptance test. Pin assignments are taken from [`board.h`](../include/board.h), [`board.c`](../src/board.c), and the [ST7735 I/O port](../src/ch32x033_st7735_io.c). Earlier pin notes in [`scrap/`](../scrap/) are historical and can disagree with current firmware, especially for PC3. Build 14 changes boost controls and UI without changing the pin map or heater cycle.

## Implemented signals

| MCU signal | Firmware use | Direction / active level |
| --- | --- | --- |
| PA0 | Input-supply voltage sense | ADC input; firmware divider ratio 16.3 |
| PA1 | Tip temperature amplifier output | ADC input; raw ≥3900 means open tip |
| PA2 | ST7735 reset | GPIO output, active low at LCD |
| PA3 | ST7735 data/command | GPIO output; low command, high data |
| PA4 | ST7735 chip select | GPIO output, active low |
| PA5 | ST7735 serial clock | SPI1 SCK output |
| PA6 | Heater-current amplifier output | ADC input; sampled on and off |
| PA7 | ST7735 serial data | SPI1 MOSI output |
| PA9 / PA10 | Encoder A / B | GPIO inputs with pull-ups |
| PA11 | Encoder pushbutton | GPIO input with pull-up; press pulls low |
| PB1 | Buzzer | GPIO output, software-timed tone |
| PB7 | Heater drive through NPN/P-MOS stage | GPIO output: **high = heater on**, **low = off** |
| PC3 | Handle tilt/VIB | GPIO floating input; **external pulldown**; high arms, later low triggers sleep |
| PC16 | LCD backlight transistor | GPIO output; backlight PWM/control |
| PC17 | Stand/sleep switch | GPIO input with internal pull-up; **low = in stand** |

PB7's logic level is the MCU-side drive signal. The board switches heater supply on the **high side** with a P-channel MOSFET driven through an NPN stage; PB7 is driven low before peripheral initialization and on shutdown/fault. PC3 is **not** used for cartridge identification. An unconnected PC3 is low from the board pulldown; the firmware requires a preceding stable high before low can count as a tilt event. A low PC17 starts the configured stand delay while heating. Both inputs must clear to wake from sleep.

The LCD is an **ST7735, 128 × 160 physical pixels** in portrait and 160 × 128 in landscape. The driver uses SPI1 plus GPIO chip select, data/command, reset and backlight. It clears controller RAM at startup and applies orientation-specific visible-window offsets: portrait **X=2, Y=1**; landscape **X=1, Y=2**. These offsets are configured for this module and should be checked against the actual glass if display edges or artifacts differ.

## Tip families and supply configuration

Tip selection is manual. The source defines C115, C210, C245, C470, T12, TX1, TX2, GOOT, and APOLLO factory families; up to ten custom profiles can be stored for each. The selected family determines temperature scaling, PID values, nominal resistance, duty/power limits and permitted supply range.

| Selected family | Firmware-accepted measured VIN |
| --- | --- |
| C115, C210 | **9–15 V** |
| C245, C470, T12, TX1, TX2, GOOT, APOLLO | **12–28 V** |

These are **configured acceptance windows** in [`profiles.c`](../src/profiles.c), not measured safe operating ratings for every real cartridge/handle. The firmware's overall ADC VIN constants span 9–30 V, but the selected family's narrower window gates heating. Check cartridge manufacturer ratings and the actual board/supply capability separately.

## Configured values versus verified measurements

The firmware currently configures a 3.3 V ADC reference model, VIN divider ratio **16.3**, PA6 current scale **340 µV/mA**, 400 W factory profile ceiling, and per-family nominal resistance and thermocouple coefficients. Its factory six-point calibration values are placeholders, and factory profiles explicitly carry `characterized=0`. Build 14 nevertheless sets `STATION_SERVICE_ALLOW_UNCHARACTERIZED=1` for development. These source constants have **not** been established here as electrically verified board or cartridge specifications. In particular, displayed voltage, temperature, current, and power should be checked against appropriate external instruments before using them as safety limits.
