#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* PB7 high drives Q1, which pulls the high-side P-channel MOSFET gate low. */
#define STATION_HEATER_OUTPUT_ENABLE 1
#define STATION_SERVICE_ALLOW_UNCHARACTERIZED 1

/* Release version and separate hardware-debug build counter. */
#define STATION_FW_VERSION "0.1.0"
#define STATION_FW_BUILD   14U
#define STATION_ADC_VREF_MV 3300UL
#define STATION_ADC_FULL_SCALE 4095UL
#define STATION_VIN_DIVIDER_RATIO 16.3f
/* Original PA6 current-sense transfer coefficient. */
#define CURRENT_SENSE_UV_PER_MA 340UL
#define STATION_MAX_TARGET_C 500
#define STATION_OVERTEMP_C 515
#define STATION_STAND_ACTIVE_LEVEL 0U
#define STATION_VIB_ACTIVE_LEVEL 0U
#define STATION_SLEEP_DELAY_MS 5000UL
#define STATION_SLEEP_TIME_S 1200U

/* TIM1 uses a 100 kHz base and a 50 ms heater cycle. Temperature is sampled
 * a fixed 5 ms after the actual heater pulse ends, rather than at a fixed
 * point near the end of the cycle. */
#define HEATER_TIMER_HZ 100000UL
#define HEATER_PERIOD_TICKS 5000U
#define HEATER_CURRENT_SAMPLE_TICKS 100U
#define HEATER_SENSOR_SETTLE_TICKS 500U
/* Keep at least 5 ms for thermocouple settling and 1 ms for ADC conversion. */
#define HEATER_MAX_ON_TICKS 4400U
#if (HEATER_MAX_ON_TICKS + HEATER_SENSOR_SETTLE_TICKS) >= HEATER_PERIOD_TICKS
#error "No ADC sampling window remains after heater settling"
#endif
#define TEMP_ADC_BURST_SAMPLES 19U

/* The power MOSFET is deliberately switched slowly to limit gate-transition
 * losses. This configuration is 20 Hz and must never be raised above 100 Hz. */
#define HEATER_MAX_SWITCH_HZ 100UL
#if (HEATER_TIMER_HZ / HEATER_PERIOD_TICKS) > HEATER_MAX_SWITCH_HZ
#error "Heater switching frequency exceeds the 100 Hz hardware limit"
#endif

#define BUZZER_CARRIER_ISR_HZ 8000UL
#define BUZZER_TONE_HZ        (BUZZER_CARRIER_ISR_HZ / 2UL)
#define BUZZER_SHORT_MS       25U
#define BUZZER_LONG_MS        400U

#define UI_REFRESH_MS 100U
#define CONTROL_STALE_MS 200U
#define TIP_OPEN_RAW 3900U
#define TIP_PRESENT_RAW 3800U
#define VIN_MIN_MV 9000U
#define VIN_MAX_MV 30000U

#endif
