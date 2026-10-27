# OpenStation firmware datasheet

**Release:** 0.1.0 Build 14  
**Target:** CH32X033F8P6 soldering-station board, ST7735 128 × 160 display  
**Scope:** Implemented firmware behavior. This is not a CH32X033 silicon datasheet or a verified electrical rating for any cartridge.

| Function | Build 14 behavior |
| --- | --- |
| Heater timing | TIM1 runs a 50 ms cycle (**20 Hz**); the on pulse is at most 44 ms, with a 5 ms off-settling interval before the temperature ADC trigger. |
| Temperature control | Selected tip profile supplies calibration and PID constants. Station maximum target is configurable up to **500 °C** and is further bounded by the profile maximum. |
| Boost | Double-click while heating to add **0–100 °C** in **5 °C** menu steps, limited by station and tip maxima. **0 displays OFF** and prevents boost activation. Duration is **15–120 s** in **5 s** steps. Defaults: **+50 °C, 30 s**. Further double-clicks add that duration to the remaining time without compounding temperature, up to **99:59**. A single click stops heating; encoder rotation changes the base target and exits Boost. |
| Boost indication | Both portrait and landscape status boxes display **Boost** above a remaining **MM:SS** timer, with **black text on yellow**. The Set readout displays the effective boosted target in red; a red `(+N°C)` label next to Set shows the actual increase after limits. |
| Sleep | PC17 stand input uses a 0–30 s delay; armed PC3 VIB input requests immediate sleep. Sleep setpoint is profile-specific. Sleep duration is 30–1800 s, after which heating stops; screen-on, dim, and off options are available. |
| Supply acceptance | Selected C115/C210 family: **9–15 V**; other configured families: **12–28 V**. These are software acceptance windows, not cartridge ratings established by this document. |
| Display/UI | **General**, Presets, Tips, Profiles, Display, Sound, System settings groups. The former Mode group is named **General**. Visible sleep and encoder labels are spelled out as **SLEEP** and **ENCODER**. |
| Persistence | Checksummed **version 9, 616-byte** settings record in the 1 KiB page at `0x0800F400`. Build 13 settings load directly; Build 12/version 8 settings migrate in memory and receive the default boost values; **Save to Flash** commits version 9. |

The firmware uses provisional factory tip coefficients and explicitly marks factory profiles uncharacterized. Build 14 enables them for development with `STATION_SERVICE_ALLOW_UNCHARACTERIZED=1`. Confirm tip compatibility, voltage, measured temperature, current, and power on the actual hardware before relying on them. For operation see the [user guide](USER_GUIDE.md); for implementation and flash layout see the [technical reference](TECHNICAL_REFERENCE.md); for pin assignments see the [hardware guide](HARDWARE.md).
