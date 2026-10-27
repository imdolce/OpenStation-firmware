#include "safety.h"
#include "app_config.h"
#include "board.h"
#include "measure.h"
#include "power.h"
#include "profiles.h"

static volatile safety_fault_t s_fault;
static fault_snapshot_t s_snapshot;
static uint32_t s_last_off_current_sequence;
static uint8_t s_off_current_bad_samples;
static uint32_t s_last_temp_sequence;
static uint32_t s_peak_time_ms;
static int16_t s_peak_temperature_c;
static uint16_t s_peak_temp_raw;
static bool s_ignore_faults;

void safety_init(void)
{
    s_fault = FAULT_NONE;
    s_last_off_current_sequence = 0;
    s_off_current_bad_samples = 0;
    s_last_temp_sequence = 0;
    s_peak_time_ms = 0;
    s_peak_temperature_c = 0;
    s_peak_temp_raw = 0;
    power_force_off();
}

static void capture(safety_fault_t code, uint8_t state)
{
    const measurement_t *m = measure_get();
    const power_status_t *p = power_get();
    s_snapshot.code = code;
    s_snapshot.timestamp_ms = board_millis();
    s_snapshot.vin_raw = m->vin_raw;
    s_snapshot.temp_raw = m->temp_raw;
    s_snapshot.current_raw = code == FAULT_OFF_CURRENT ? m->off_current_raw : m->current_raw;
    s_snapshot.duty_permille = p->limited_permille;
    s_snapshot.vin_mv = m->vin_mv;
    s_snapshot.current_ma = code == FAULT_OFF_CURRENT ? m->off_current_ma : m->current_ma;
    s_snapshot.temperature_c = m->temperature_c;
    s_snapshot.reference_temperature_c = s_peak_temperature_c;
    s_snapshot.reference_temp_raw = s_peak_temp_raw;
    s_snapshot.state = state;
}

void safety_trip(safety_fault_t code, uint8_t state)
{
    if (code == FAULT_NONE || s_fault != FAULT_NONE) return;
    if (s_ignore_faults && code != FAULT_ADC_STALE && code != FAULT_PULSE_TIMEOUT) return;
    uint16_t duty_before_stop = power_get()->limited_permille;
    power_force_off();
    capture(code, state);
    s_snapshot.duty_permille = duty_before_stop;
    s_fault = code;
    board_buzzer_pattern(3);
}

void safety_trip_isr(safety_fault_t code)
{
    if (code == FAULT_NONE || s_fault != FAULT_NONE) return;
    uint16_t duty_before_stop = power_get()->limited_permille;
    power_emergency_stop_isr();
    capture(code, 0xFFU);
    s_snapshot.duty_permille = duty_before_stop;
    s_fault = code;
}

void safety_supervise(uint8_t state, bool heating_requested)
{
    const measurement_t *m = measure_get();
    const power_status_t *pwr = power_get();
    const tip_profile_t *profile = profile_active();
    uint32_t now = board_millis();
    if (s_fault != FAULT_NONE) return;
    if (!m->quiet_sample_valid) {
        power_force_off();
        uint32_t grace_ms = m->sample_sequence == 0U ? 500U : CONTROL_STALE_MS;
        if (now - m->sample_time_ms > grace_ms)
            safety_trip(FAULT_ADC_STALE, state);
        return;
    }
    if (now - m->sample_time_ms > CONTROL_STALE_MS) { safety_trip(FAULT_ADC_STALE, state); return; }
    if (s_ignore_faults) return;
    if (m->sample_sequence != s_last_temp_sequence) {
        s_last_temp_sequence = m->sample_sequence;
        /* A real cartridge cannot lose over 100 C in 300 ms while power is
         * being requested. A collapsing PA1 signal must not make the PI
         * controller demand full power from a still-hot tip. */
        if (heating_requested && s_peak_time_ms != 0U &&
            (uint32_t)(now - s_peak_time_ms) <= 300U &&
            s_peak_temperature_c >= 130 &&
            s_peak_temperature_c - m->temperature_c > 100) {
            safety_trip(FAULT_SENSOR_UNSTABLE, state);
            return;
        }
        if (s_peak_time_ms == 0U || (uint32_t)(now - s_peak_time_ms) > 300U ||
            m->temperature_c > s_peak_temperature_c) {
            s_peak_temperature_c = m->temperature_c;
            s_peak_temp_raw = m->temp_raw;
            s_peak_time_ms = now;
        }
    }
    if (heating_requested) {
        if (!m->tip_present) { safety_trip(FAULT_SENSOR_OPEN, state); return; }
        if (m->temp_raw < 8U) { safety_trip(FAULT_SENSOR_SHORT, state); return; }
        if (!m->vin_valid) { safety_trip(FAULT_SUPPLY_RANGE, state); return; }
        if (m->temperature_c > profile->maximum_temp_c + 15 || m->temperature_c > STATION_OVERTEMP_C) {
            safety_trip(FAULT_OVERTEMP, state); return;
        }
        if (!profile->characterized && pwr->output_armed && !STATION_SERVICE_ALLOW_UNCHARACTERIZED) {
            safety_trip(FAULT_UNCHARACTERIZED_PROFILE, state); return;
        }
    }
    if (m->off_current_sample_valid && m->sample_sequence != s_last_off_current_sequence) {
        s_last_off_current_sequence = m->sample_sequence;
        if (m->off_current_ma > 500U) {
            if (s_off_current_bad_samples < 2U) ++s_off_current_bad_samples;
            if (s_off_current_bad_samples >= 2U) safety_trip(FAULT_OFF_CURRENT, state);
        } else s_off_current_bad_samples = 0;
    }
}

bool safety_acknowledge(void)
{
    const measurement_t *m = measure_get();
    const tip_profile_t *p = profile_active();
    if (s_fault == FAULT_NONE) return true;
    if (!m->quiet_sample_valid) return false;
    /* After a hot sensor collapse, the displayed cool temperature cannot be
     * trusted. Require a reset and inspection before heating is rearmed. */
    if (s_fault == FAULT_SENSOR_UNSTABLE) return false;
    if ((s_fault == FAULT_SENSOR_OPEN && !m->tip_present) ||
        (s_fault == FAULT_SENSOR_SHORT && m->temp_raw < 20U) ||
        (s_fault == FAULT_OVERTEMP && m->temperature_c > p->maximum_temp_c) ||
        (s_fault == FAULT_OFF_CURRENT && m->off_current_ma > 100U) ||
        (s_fault == FAULT_SUPPLY_RANGE && !m->vin_valid) ||
        (s_fault == FAULT_ADC_STALE && board_millis() - m->sample_time_ms > CONTROL_STALE_MS)) return false;
    s_fault = FAULT_NONE;
    s_off_current_bad_samples = 0;
    s_peak_time_ms = 0;
    power_force_off();
    return true;
}

bool safety_ignore_faults(void) { return s_ignore_faults; }
void safety_set_ignore_faults(bool enabled)
{
    if (s_ignore_faults == enabled) return;
    power_force_off();
    s_ignore_faults = enabled;
    s_peak_time_ms = 0;
    s_off_current_bad_samples = 0;
    if (enabled && s_fault != FAULT_ADC_STALE && s_fault != FAULT_PULSE_TIMEOUT)
        s_fault = FAULT_NONE;
}

bool safety_latched(void) { return s_fault != FAULT_NONE; }
safety_fault_t safety_fault(void) { return s_fault; }
const fault_snapshot_t *safety_snapshot(void) { return &s_snapshot; }

const char *safety_fault_text(safety_fault_t f)
{
    switch (f) {
    case FAULT_NONE: return "NONE";
    case FAULT_SENSOR_OPEN: return "TIP OPEN";
    case FAULT_SENSOR_SHORT: return "SENSOR LOW";
    case FAULT_OVERTEMP: return "OVER TEMP";
    case FAULT_OVERCURRENT: return "OVER CURRENT";
    case FAULT_OFF_CURRENT: return "OFF CURRENT";
    case FAULT_NO_CURRENT: return "NO CURRENT";
    case FAULT_SUPPLY_RANGE: return "VIN RANGE";
    case FAULT_ADC_STALE: return "ADC STALE";
    case FAULT_PULSE_TIMEOUT: return "PULSE TIME";
    case FAULT_UNCHARACTERIZED_PROFILE: return "PROFILE";
    case FAULT_SENSOR_UNSTABLE: return "SENSOR DROP";
    default: return "UNKNOWN";
    }
}
