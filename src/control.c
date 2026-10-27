#include "control.h"
#include "app_config.h"
#include "board.h"
#include "measure.h"
#include "power.h"
#include "profiles.h"
#include "safety.h"

static volatile control_status_t s_control;
static uint8_t s_tip_stable;
static uint8_t s_stand_stable;
static uint8_t s_vib_stable;
static uint8_t s_vib_high_stable;
static bool s_vib_armed;
static bool s_stand_beeped;
static int16_t s_previous_temperature_c;
static uint32_t s_previous_sample_ms;
static bool s_derivative_valid;
static uint8_t s_encoder_step_c = 5U;
static uint16_t s_max_target_c = STATION_MAX_TARGET_C;
static uint32_t s_sleep_delay_ms = STATION_SLEEP_DELAY_MS;
static uint16_t s_sleep_time_s = STATION_SLEEP_TIME_S;
static sleep_type_t s_sleep_type = SLEEP_TYPE_SCREEN_ON;
static uint8_t s_boost_delta_c = 50U;
static uint8_t s_boost_time_s = 30U;
/* The status box displays MM:SS with two minute digits. */
#define BOOST_MAX_ACCUMULATED_SECONDS 5999U
static uint16_t s_presets_c[3] = {300U, 350U, 400U};
static uint8_t s_preset_index;
static bool s_preset_selected;

static bool active_state(station_state_t s)
{
    return s == STATE_HEAT_UP || s == STATE_REGULATE || s == STATE_BOOST || s == STATE_SLEEP;
}

static int16_t boost_target_c(void)
{
    const tip_profile_t *p = profile_active();
    int16_t target = (int16_t)(s_control.target_c + s_boost_delta_c);
    if (target > p->maximum_temp_c) target = p->maximum_temp_c;
    if (target > s_max_target_c) target = (int16_t)s_max_target_c;
    return target;
}

static void clear_sleep_session(bool beep)
{
    bool was_sleeping = s_control.state == STATE_SLEEP ||
                        s_control.sleep_timed_out;
    s_control.sleep_since_ms = 0U;
    s_control.sleep_timed_out = false;
    s_control.stand_since_ms = 0U;
    board_backlight_override(false, 0U);
    if (beep && was_sleeping) board_buzzer_pattern(2U);
}

static void update_sleep_screen(void)
{
    if (s_control.sleep_timed_out && s_sleep_type == SLEEP_TYPE_SCREEN_DIM)
        board_backlight_override(true, 20U);
    else if (s_control.sleep_timed_out && s_sleep_type == SLEEP_TYPE_SCREEN_OFF)
        board_backlight_override(true, 0U);
    else board_backlight_override(false, 0U);
}

static void enter_state(station_state_t next)
{
    if (s_control.state == next) return;
    bool was_active = active_state(s_control.state);
    bool will_be_active = active_state(next);
    s_control.state = next;
    if (next != STATE_BOOST) s_control.boost_seconds = 0U;
    s_control.state_since_ms = board_millis();
    /* The stand beeps when its countdown begins; VIB sleeps immediately. */
    if (next == STATE_SLEEP) {
        s_control.sleep_since_ms = s_control.state_since_ms;
        s_control.sleep_timed_out = false;
        if (!s_stand_beeped) board_buzzer_pattern(2U);
    }
    if (!will_be_active) {
        s_control.integral_q8 = 0;
        s_control.requested_permille = 0;
        s_derivative_valid = false;
        power_request(0);
    } else if (!was_active) {
        /* Seed D from the first active sample; do not kick on enable. */
        s_derivative_valid = false;
    }
}

void control_init(void)
{
    const tip_profile_t *p = profile_active();
    s_control = (control_status_t){0};
    s_tip_stable = 0;
    s_stand_stable = 0;
    s_vib_stable = 0;
    s_vib_high_stable = 0;
    s_vib_armed = false;
    s_stand_beeped = false;
    s_derivative_valid = false;
    s_preset_index = 2U;
    s_preset_selected = false;
    s_control.state = STATE_BOOT_SAFE;
    s_control.target_c = p->default_temp_c > s_max_target_c ?
                         (int16_t)s_max_target_c : p->default_temp_c;
    if (s_control.target_c > p->maximum_temp_c)
        s_control.target_c = p->maximum_temp_c;
    s_control.active_setpoint_c = s_control.target_c;
    s_control.state_since_ms = board_millis();
    board_backlight_override(false, 0U);
    power_request(0);
}

void control_reconfigure(void)
{
    power_force_off();
    control_init();
}

void control_run_cycle(void)
{
    const measurement_t *m = measure_get();
    const tip_profile_t *p = profile_active();
    uint32_t now = board_millis();

    if (safety_latched()) {
        clear_sleep_session(false);
        enter_state(STATE_FAULT_LATCHED);
        power_request(0);
        return;
    }

    if (m->tip_present) { if (s_tip_stable < 4U) ++s_tip_stable; }
    else s_tip_stable = 0;
    if (board_stand_raw() == STATION_STAND_ACTIVE_LEVEL) { if (s_stand_stable < 4U) ++s_stand_stable; }
    else s_stand_stable = 0;
    s_control.stand_active = s_stand_stable >= 3U;
    if (s_sleep_type != SLEEP_TYPE_NONE && active_state(s_control.state) &&
        s_control.state != STATE_SLEEP &&
        s_control.stand_active) {
        if (!s_stand_beeped) {
            board_buzzer_pattern(2U);
            s_stand_beeped = true;
        }
    } else if (!s_control.stand_active || !active_state(s_control.state) ||
               s_sleep_type == SLEEP_TYPE_NONE) {
        if (s_stand_beeped && !s_control.stand_active &&
            s_control.state != STATE_SLEEP) board_buzzer_pattern(2U);
        s_stand_beeped = false;
    }
    /* An unconnected PC3 is held low externally, just like a triggered
     * sensor. Arm VIB only after the connected sensor drives PC3 high. */
    if (board_vib_raw() != STATION_VIB_ACTIVE_LEVEL) {
        if (s_vib_high_stable < 3U) ++s_vib_high_stable;
        if (s_vib_high_stable >= 3U) s_vib_armed = true;
    } else s_vib_high_stable = 0U;
    if (s_vib_armed && board_vib_raw() == STATION_VIB_ACTIVE_LEVEL) {
        if (s_vib_stable < 3U) ++s_vib_stable;
    } else if (s_vib_stable > 0U) --s_vib_stable;
    if (s_vib_stable >= 2U) s_control.vib_active = true;
    else if (s_vib_stable == 0U) s_control.vib_active = false;

    bool sleep_requested = s_sleep_type != SLEEP_TYPE_NONE &&
                           (s_control.stand_active || s_control.vib_active);
    if (s_control.sleep_timed_out && !sleep_requested)
        clear_sleep_session(true);

    if (!m->tip_present) {
        if (s_control.sleep_timed_out) clear_sleep_session(false);
        if (active_state(s_control.state)) safety_trip(FAULT_SENSOR_OPEN, (uint8_t)s_control.state);
        else enter_state(STATE_NO_TIP);
        power_request(0);
        return;
    }

    if (s_control.state == STATE_BOOT_SAFE || s_control.state == STATE_NO_TIP) {
        if (s_tip_stable >= 3U) enter_state(STATE_TIP_IDENTIFY);
        return;
    }
    if (s_control.state == STATE_TIP_IDENTIFY) {
        /* Manual profile is authoritative until bounded diagnostic pulses are bench characterized. */
        if (now - s_control.state_since_ms >= 100U) enter_state(STATE_IDLE);
        return;
    }

    if (active_state(s_control.state)) {
        if (sleep_requested) {
            if (s_control.stand_since_ms == 0U) s_control.stand_since_ms = now;
            if (s_control.state != STATE_SLEEP &&
                (s_control.vib_active || now - s_control.stand_since_ms >= s_sleep_delay_ms)) {
                /* Drop the previous heat-up command immediately. The PI loop
                 * will then regulate against the lower sleep setpoint. */
                power_request(0);
                s_control.integral_q8 = 0;
                s_control.requested_permille = 0;
                s_derivative_valid = false;
                s_control.active_setpoint_c = p->sleep_temp_c;
                enter_state(STATE_SLEEP);
            }
            if (s_control.state == STATE_SLEEP &&
                (uint32_t)(board_millis() - s_control.sleep_since_ms) >=
                (uint32_t)s_sleep_time_s * 1000UL) {
                s_control.sleep_timed_out = true;
                s_control.heating_enabled = false;
                enter_state(STATE_COOLING);
                update_sleep_screen();
            }
        } else {
            s_control.stand_since_ms = 0;
            if (s_control.state == STATE_SLEEP) {
                clear_sleep_session(true);
                s_control.active_setpoint_c = s_control.target_c;
                enter_state(STATE_HEAT_UP);
            }
        }
    }

    if (s_control.inactivity_ms >= 1800000UL && active_state(s_control.state) &&
        !sleep_requested) {
        s_control.heating_enabled = false;
        clear_sleep_session(false);
        enter_state(STATE_COOLING);
    }

    /* Validate the new quiet-window reading before it can increase duty. */
    safety_supervise((uint8_t)s_control.state, active_state(s_control.state));
    if (safety_latched()) {
        enter_state(STATE_FAULT_LATCHED);
        power_request(0);
        return;
    }

    if (active_state(s_control.state)) {
        int32_t error = (int32_t)s_control.active_setpoint_c - m->temperature_c;
        int32_t p_term = ((int32_t)p->kp_q8 * error) >> 8;
        int32_t d_term = 0;
        uint32_t dt_ms = s_derivative_valid ? now - s_previous_sample_ms : 50U;
        if (s_derivative_valid) {
            if (dt_ms >= 10U && dt_ms <= CONTROL_STALE_MS) {
                int32_t temperature_delta = (int32_t)m->temperature_c -
                                            s_previous_temperature_c;
                /* Derivative on measurement avoids setpoint-change kick.
                 * kd_q8 is duty-permille seconds per degree C in Q8. */
                int64_t scaled = -((int64_t)p->kd_q8 * temperature_delta * 1000LL);
                d_term = (int32_t)(scaled / ((int64_t)256 * dt_ms));
                if (d_term > 300) d_term = 300;
                if (d_term < -300) d_term = -300;
            }
        }
        s_previous_temperature_c = m->temperature_c;
        s_previous_sample_ms = now;
        s_derivative_valid = true;
        /* Integrate throughout heat-up. The previous +/-40 C gate could
         * leave a steady-state error with no way for I to increase power.
         * Stop positive windup when the request is already being limited. */
        int32_t provisional = p_term + (s_control.integral_q8 >> 8) + d_term;
        bool limited_high = s_control.requested_permille >
                            (uint32_t)power_get()->limited_permille + 10U;
        if (dt_ms >= 10U && dt_ms <= CONTROL_STALE_MS &&
            !((error > 0 && (provisional >= 1000 || limited_high)) ||
              (error < 0 && provisional <= 0)))
            s_control.integral_q8 += (int32_t)(((int64_t)p->ki_q8 * error * dt_ms) / 1000LL);
        if (s_control.integral_q8 < 0) s_control.integral_q8 = 0;
        int32_t integral_limit_q8 = (int32_t)p->max_integral_permille << 8;
        if (s_control.integral_q8 > integral_limit_q8) s_control.integral_q8 = integral_limit_q8;
        int32_t request = p_term + (s_control.integral_q8 >> 8) + d_term;
        if (request < 0) request = 0;
        if (request > 1000) request = 1000;
        if (request > (int32_t)s_control.requested_permille + 100) request = s_control.requested_permille + 100;
        s_control.requested_permille = (uint16_t)request;
        power_request(s_control.requested_permille);
        if (s_control.state == STATE_HEAT_UP && m->temperature_c >= s_control.active_setpoint_c - 5) {
            enter_state(STATE_REGULATE);
            if (board_setpoint_beep_get()) board_buzzer_pattern(2);
        }
    } else {
        power_request(0);
        if (s_control.state == STATE_COOLING && m->temperature_c < 60) enter_state(STATE_IDLE);
    }
    safety_supervise((uint8_t)s_control.state, active_state(s_control.state));
}

void control_tick_1ms(void)
{
    if (s_control.inactivity_ms < 0xFFFFFFFFUL) ++s_control.inactivity_ms;
    if (s_control.state == STATE_BOOST && s_control.boost_seconds != 0U && board_millis() % 1000U == 0U) {
        if (--s_control.boost_seconds == 0U) {
            s_control.active_setpoint_c = s_control.target_c;
            enter_state(STATE_REGULATE);
        }
    }
}

void control_rotate(int8_t detents)
{
    if (detents == 0) return;
    const tip_profile_t *p = profile_active();
    int32_t target = s_control.target_c + (int32_t)detents * s_encoder_step_c;
    if (target < 150) target = 150;
    if (target > s_max_target_c) target = s_max_target_c;
    if (target > p->maximum_temp_c) target = p->maximum_temp_c;
    if (target != s_control.target_c) s_preset_selected = false;
    s_control.target_c = (int16_t)target;
    if (s_control.state == STATE_BOOST) enter_state(STATE_REGULATE);
    if (s_control.state != STATE_SLEEP) s_control.active_setpoint_c = (int16_t)target;
    s_control.inactivity_ms = 0;
}

void control_set_target_c(int16_t target_c)
{
    const tip_profile_t *p = profile_active();
    if (target_c < 100) target_c = 100;
    if (target_c > (int16_t)s_max_target_c) target_c = (int16_t)s_max_target_c;
    if (target_c > p->maximum_temp_c) target_c = p->maximum_temp_c;
    s_control.target_c = target_c;
    if (s_control.state == STATE_BOOST) s_control.active_setpoint_c = boost_target_c();
    else if (s_control.state != STATE_SLEEP)
        s_control.active_setpoint_c = target_c;
    s_control.inactivity_ms = 0;
    s_preset_selected = false;
}

void control_change_profile(int8_t detents)
{
    int16_t index = (int16_t)profile_active_index() + detents;
    int16_t count = profile_count();
    while (index < 0) index += count;
    while (index >= count) index -= count;
    control_select_profile((uint8_t)index);
}

static void selected_profile_changed(void)
{
    power_force_off();
    clear_sleep_session(false);
    measure_profile_changed();
    s_derivative_valid = false;
    const tip_profile_t *p = profile_active();
    s_control.target_c = p->default_temp_c;
    if (s_control.target_c > p->maximum_temp_c) s_control.target_c = p->maximum_temp_c;
    if (s_control.target_c > s_max_target_c) s_control.target_c = s_max_target_c;
    if (s_control.target_c < 150) s_control.target_c = 150;
    s_control.active_setpoint_c = s_control.target_c;
    s_control.integral_q8 = 0;
    s_control.heating_enabled = false;
    enter_state(measure_get()->tip_present ? STATE_TIP_IDENTIFY : STATE_NO_TIP);
    s_control.inactivity_ms = 0;
    s_preset_selected = false;
}

void control_select_profile(uint8_t index)
{
    if (index >= profile_count()) return;
    profile_select(index);
    selected_profile_changed();
}

bool control_select_custom_profile(uint8_t family, uint8_t slot)
{
    if (!profile_custom_select(family, slot)) return false;
    selected_profile_changed();
    return true;
}

void control_short_press(void)
{
    s_control.inactivity_ms = 0;
    if (safety_latched()) { if (safety_acknowledge()) enter_state(STATE_IDLE); return; }
    if (active_state(s_control.state)) {
        s_control.heating_enabled = false;
        clear_sleep_session(false);
        enter_state(STATE_COOLING);
    } else if ((s_control.state == STATE_IDLE || s_control.state == STATE_COOLING) &&
               measure_get()->tip_present) {
        if (!measure_get()->quiet_sample_valid) return;
        if (!measure_get()->vin_valid) {
            safety_trip(FAULT_SUPPLY_RANGE, (uint8_t)s_control.state);
            return;
        }
#if STATION_HEATER_OUTPUT_ENABLE
        if (profile_active()->characterized || STATION_SERVICE_ALLOW_UNCHARACTERIZED) {
            clear_sleep_session(false);
            s_control.heating_enabled = true;
            if (s_sleep_type != SLEEP_TYPE_NONE && (s_control.vib_active ||
                (s_vib_armed && board_vib_raw() == STATION_VIB_ACTIVE_LEVEL))) {
                s_vib_stable = 2U;
                s_control.vib_active = true;
                s_control.active_setpoint_c = profile_active()->sleep_temp_c;
                s_control.stand_since_ms = board_millis();
                enter_state(STATE_SLEEP);
            } else {
                s_control.active_setpoint_c = s_control.target_c;
                enter_state(STATE_HEAT_UP);
            }
        } else safety_trip(FAULT_UNCHARACTERIZED_PROFILE, (uint8_t)s_control.state);
#else
        board_buzzer_pattern(1);
#endif
    }
}

void control_long_press(void)
{
    s_control.inactivity_ms = 0;
    if (active_state(s_control.state)) {
        s_control.heating_enabled = false;
        clear_sleep_session(false);
        enter_state(STATE_COOLING);
    }
    power_force_off();
}

void control_double_press(void)
{
    const tip_profile_t *p = profile_active();
    if (s_control.state == STATE_HEAT_UP || s_control.state == STATE_REGULATE) {
        if (s_boost_delta_c == 0U) return;
        s_control.active_setpoint_c = boost_target_c();
        s_control.boost_seconds = s_boost_time_s;
        enter_state(STATE_BOOST);
    } else if (s_control.state == STATE_BOOST) {
        uint32_t stacked = (uint32_t)s_control.boost_seconds + s_boost_time_s;
        if (stacked > BOOST_MAX_ACCUMULATED_SECONDS)
            stacked = BOOST_MAX_ACCUMULATED_SECONDS;
        s_control.boost_seconds = (uint16_t)stacked;
    } else if (!active_state(s_control.state) && !safety_latched()) {
        uint8_t candidate = s_preset_index;
        uint16_t ceiling = p->maximum_temp_c < s_max_target_c ?
                           (uint16_t)p->maximum_temp_c : s_max_target_c;
        for (uint8_t tries = 0; tries < 3U; ++tries) {
            candidate = (uint8_t)((candidate + 1U) % 3U);
            if (s_presets_c[candidate] <= ceiling) break;
        }
        if (s_presets_c[candidate] > ceiling) return;
        s_preset_index = candidate;
        s_preset_selected = true;
        int16_t target = (int16_t)s_presets_c[s_preset_index];
        if (target > s_max_target_c) target = s_max_target_c;
        if (target > p->maximum_temp_c) target = p->maximum_temp_c;
        s_control.target_c = target;
        s_control.active_setpoint_c = target;
    }
}

uint16_t control_preset_c(uint8_t index)
{
    return index < 3U ? s_presets_c[index] : 300U;
}
int8_t control_selected_preset(void)
{
    return s_preset_selected ? (int8_t)s_preset_index : -1;
}

void control_set_preset_c(uint8_t index, uint16_t temperature_c)
{
    if (index >= 3U) return;
    if (temperature_c < 150U) temperature_c = 150U;
    if (temperature_c > 500U) temperature_c = 500U;
    s_presets_c[index] = temperature_c;
    if (s_preset_selected && index == s_preset_index &&
        s_control.target_c != (int16_t)temperature_c) s_preset_selected = false;
}

const control_status_t *control_get(void) { return (const control_status_t *)&s_control; }
uint8_t control_encoder_step_c(void) { return s_encoder_step_c; }
void control_set_encoder_step_c(uint8_t value)
{
    s_encoder_step_c = value <= 1U ? 1U : (value <= 5U ? 5U : 10U);
}
uint16_t control_max_target_c(void) { return s_max_target_c; }
void control_set_max_target_c(uint16_t value)
{
    if (value < 150U) value = 150U;
    if (value > STATION_MAX_TARGET_C) value = STATION_MAX_TARGET_C;
    s_max_target_c = value;
    if (s_control.target_c > (int16_t)value) s_control.target_c = (int16_t)value;
    if (s_preset_selected && s_control.target_c != (int16_t)s_presets_c[s_preset_index])
        s_preset_selected = false;
    if (s_control.active_setpoint_c > (int16_t)value)
        s_control.active_setpoint_c = (int16_t)value;
}
uint32_t control_sleep_delay_ms(void) { return s_sleep_delay_ms; }
void control_set_sleep_delay_ms(uint32_t value)
{
    if (value > 30000UL) value = 30000UL;
    s_sleep_delay_ms = value;
}
uint16_t control_sleep_time_s(void) { return s_sleep_time_s; }
void control_set_sleep_time_s(uint16_t value)
{
    if (value < 30U) value = 30U;
    if (value > 1800U) value = 1800U;
    s_sleep_time_s = value;
}
sleep_type_t control_sleep_type(void) { return s_sleep_type; }
void control_set_sleep_type(sleep_type_t value)
{
    if (value > SLEEP_TYPE_SCREEN_OFF) value = SLEEP_TYPE_SCREEN_OFF;
    s_sleep_type = value;
    if (value == SLEEP_TYPE_NONE) {
        clear_sleep_session(false);
        if (s_control.state == STATE_SLEEP) {
            s_control.active_setpoint_c = s_control.target_c;
            enter_state(STATE_HEAT_UP);
        }
    } else update_sleep_screen();
}
uint8_t control_boost_delta_c(void) { return s_boost_delta_c; }
void control_set_boost_delta_c(uint8_t value)
{
    s_boost_delta_c = value > 100U ? 100U : value;
    if (s_control.state == STATE_BOOST) {
        if (s_boost_delta_c == 0U) {
            s_control.active_setpoint_c = s_control.target_c;
            s_control.boost_seconds = 0U;
            enter_state(STATE_REGULATE);
        } else s_control.active_setpoint_c = boost_target_c();
    }
}
uint8_t control_boost_time_s(void) { return s_boost_time_s; }
void control_set_boost_time_s(uint8_t value)
{
    if (value < 15U) value = 15U;
    if (value > 120U) value = 120U;
    s_boost_time_s = (uint8_t)(15U + ((value - 15U + 2U) / 5U) * 5U);
}
const char *control_state_text(station_state_t s)
{
    static const char *const names[] = {"BOOT", "NO TIP", "IDENT", "IDLE", "HEAT", "READY", "BOOST", "SLEEP", "COOL", "FAULT"};
    return s <= STATE_FAULT_LATCHED ? names[s] : "?";
}
