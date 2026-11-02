#include "tip_setup.h"
#include "app_config.h"
#include "board.h"
#include "ch32x033_st7735.h"
#include "control.h"
#include "encoder.h"
#include "measure.h"
#include "power.h"
#include "profiles.h"
#include "safety.h"
#include "settings.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

/* All temperatures are manual external-thermometer references.  There is no
 * cold-junction sensor on this board.  Nothing below modifies flash until Save. */
enum { OFF, WELCOME, ROUTE, FACTORY1, FACTORY2, TIP_SELECT,
       ROOM, SENSITIVITY, KNOWN_NOTICE, WARNING, HEAT, RESULT, ERROR };
static uint8_t s_state, s_choice, s_previous_selection;
static bool s_first_boot, s_capture_pending, s_vib_armed, s_dirty, s_baseline_valid;
static bool s_manual_sensitivity;
static int16_t s_room, s_external, s_estimated;
static int16_t s_previous_estimate;
static uint16_t s_baseline, s_staged_slope;
static uint16_t s_manual_tenths;
static uint32_t s_since;
static uint32_t s_previous_estimate_ms;
static uint16_t s_integral;
static const char *s_error;
static tip_profile_t s_original, s_candidate;
static ST7735_Object_t *s_lcd;
static const int16_t s_target_c = 200;
static const uint8_t s_tip_order[9] = {0, 1, 2, 3, 5, 6, 4, 8, 7};
#define BLUE 0x055EU
#define MUTED 0x4D6AU
#define GOLD 0xFF47U
#define PALE 0xBDF7U
#define SELECT 0x8E09U
#define SETUP_LINE_COUNT 16U
static struct { uint16_t y, color; char text[26]; } s_lines[SETUP_LINE_COUNT];
static uint8_t s_line_count;

static uint32_t uv(uint16_t raw) { return (uint32_t)(((uint64_t)raw * 3300000ULL) / 4095ULL); }
static uint16_t raw_from_uv(uint32_t value)
{ return (uint16_t)(((uint64_t)value * 4095ULL + 1650000ULL) / 3300000ULL); }
static int16_t clamp(int32_t v, int16_t lo, int16_t hi)
{ return (int16_t)(v < lo ? lo : (v > hi ? hi : v)); }
/* A completed Tip Setup profile carries its room reference and ADC-side
 * slope/offset. Reconstruct the room ADC instead of treating a hot retry as
 * room temperature. Integer offset storage makes this approximate. */
static bool saved_room_baseline(const tip_profile_t *p, uint16_t *baseline)
{
    if (p->revision < 3U || (p->room_reference_c & 0x3FU) > 50U ||
        p->temp_uv_per_c < 1000U || p->temp_uv_per_c > 12000U) return false;
    /* The saved ADC baseline belongs to this profile's calibration room,
     * even if the station's shared room reference has since changed. */
    int32_t value = (int32_t)((p->room_reference_c & 0x3FU) -
                              p->temp_offset_c) * p->temp_uv_per_c;
    if (value < 1000 || value >= 3300000) return false;
    *baseline = raw_from_uv((uint32_t)value);
    return *baseline >= 8U && *baseline < TIP_OPEN_RAW;
}
static void state(uint8_t next)
{
    power_force_off();
    s_state = next;
    s_since = board_millis();
    s_capture_pending = false;
    s_dirty = true;
    input_set_setup_mode(next == ROOM || next == SENSITIVITY ||
                         next == KNOWN_NOTICE ||
                         next == WARNING || next == HEAT);
}
static void leave(bool restore_selection)
{
    power_force_off();
    if (restore_selection && s_first_boot) {
        if (s_previous_selection < profile_count())
            control_select_profile(s_previous_selection);
        else {
            uint8_t encoded = s_previous_selection - profile_count();
            control_select_custom_profile(encoded / PROFILE_CUSTOM_PER_TIP,
                                          encoded % PROFILE_CUSTOM_PER_TIP);
        }
    }
    s_choice = 0U;
    state(s_first_boot ? ROUTE : OFF);
    ui_force_redraw();
}
static void fail(const char *message)
{
    s_error = message;
    state(ERROR);
}
static void finish_setup(void)
{
    state(OFF);
    if (ui_settings_active()) ui_toggle_page();
    if (ui_boot_warning_active()) ui_dismiss_boot_warning();
    ui_force_redraw();
}
static bool checks(void)
{
    const measurement_t *m = measure_get();
    if (safety_latched()) { s_error = "Clear fault first"; return false; }
    if (!m->quiet_sample_valid || board_millis() - m->sample_time_ms > CONTROL_STALE_MS) {
        s_error = "ADC not fresh"; return false;
    }
    if (!m->tip_present || m->temp_raw < 8U || m->temp_raw >= TIP_OPEN_RAW) {
        s_error = "Tip not connected"; return false;
    }
    if (!m->vin_valid || !profile_supply_in_range(m->vin_mv)) {
        s_error = "Supply out of range"; return false;
    }
    if (m->off_current_sample_valid && m->off_current_ma > 500U) {
        s_error = "Current while off"; return false;
    }
    if (board_stand_raw() == STATION_STAND_ACTIVE_LEVEL ||
        (s_vib_armed && board_vib_raw() == STATION_VIB_ACTIVE_LEVEL)) {
        s_error = "Remove tip from stand"; return false;
    }
    if (profile_active()->temp_uv_per_c < 1000U ||
        profile_active()->temp_uv_per_c > 12000U) {
        s_error = "Tip slope invalid"; return false;
    }
    return true;
}

void tip_setup_init(ST7735_Object_t *lcd)
{
    s_lcd = lcd;
    s_first_boot = !settings_setup_complete();
    s_previous_selection = profile_active_selection();
    s_state = s_first_boot ? WELCOME : OFF;
    s_dirty = true;
    input_set_setup_mode(false);
    if (s_first_boot) power_force_off();
}
bool tip_setup_active(void) { return s_state != OFF; }
void tip_setup_start_profile(void)
{
    control_long_press();
    s_first_boot = false;
    s_previous_selection = profile_active_selection();
    s_original = *profile_active();
    s_candidate = s_original;
    s_room = settings_room_temperature_c();
    s_baseline_valid = false;
    s_error = NULL;
    state(ROOM);
}
void tip_setup_rotate(int8_t steps)
{
    if (steps == 0) return;
    if (s_state == ROUTE || s_state == FACTORY1 || s_state == FACTORY2)
        s_choice = (uint8_t)((s_choice + (steps < 0 ? -steps : steps)) & 1U);
    else if (s_state == RESULT) {
        int16_t next = (int16_t)s_choice + steps;
        while (next < 0) next += 3;
        s_choice = (uint8_t)(next % 3);
    }
    else if (s_state == TIP_SELECT) {
        int16_t next = (int16_t)s_choice + steps;
        while (next < 0) next += 9;
        s_choice = (uint8_t)(next % 9);
    } else if (s_state == ROOM) s_room = clamp(s_room + steps, 0, 50);
    else if (s_state == SENSITIVITY) {
        s_manual_tenths = (uint16_t)clamp((int32_t)s_manual_tenths + steps, 80, 950);
        s_manual_sensitivity = true;
    }
    else if (s_state == HEAT) {
        s_external = clamp(s_external + steps, 30, 500);
        s_error = NULL;
    }
}
static void begin_heat(void)
{
    s_external = s_target_c;
    s_estimated = clamp(s_room +
        ((int32_t)uv(measure_get()->temp_raw) - (int32_t)uv(s_baseline)) /
        s_staged_slope, -20, 550);
    s_integral = 0;
    s_previous_estimate_ms = 0U;
    s_error = NULL;
    state(HEAT);
}
static bool fit(uint16_t raw, int16_t external_c)
{
    uint32_t base_uv = uv(s_baseline);
    int32_t delta_c = external_c - s_room;
    int32_t delta_uv = (int32_t)uv(raw) - (int32_t)base_uv;
    if (delta_c <= 0 || delta_uv <= 0 || raw <= s_baseline) return false;
    int32_t slope = (delta_uv + delta_c / 2) / delta_c;
    if (slope < 1008 || slope > 11970) return false; /* 8..95 uV/C before 126x */
    s_candidate = s_original;
    s_candidate.revision = 3U;
    s_candidate.room_reference_c = (uint8_t)s_room;
    s_candidate.temp_uv_per_c = (uint16_t)slope;
    int32_t offset = s_room - (int32_t)base_uv / slope;
    if (offset < -100 || offset > 100) return false;
    s_candidate.temp_offset_c = (int16_t)offset;
    /* One room reference and one heated reading establish only a linear
     * sensitivity. Old multi-point corrections no longer match that slope. */
    static const int16_t anchors[6] = {100, 200, 300, 350, 400, 450};
    for (uint8_t i = 0; i < 6U; ++i) s_candidate.temp_cal_c[i] = anchors[i];
    s_candidate.characterized = 1U;
    return true;
}

static bool prepare_known_sensitivity(void)
{
    if (!s_baseline_valid || s_staged_slope < 1008U || s_staged_slope > 11970U)
        return false;
    int32_t offset = s_room - (int32_t)uv(s_baseline) / s_staged_slope;
    if (offset < -100 || offset > 100) return false;
    s_candidate = s_original;
    s_candidate.revision = 3U;
    s_candidate.room_reference_c = (uint8_t)s_room;
    s_candidate.temp_uv_per_c = s_staged_slope;
    s_candidate.temp_offset_c = (int16_t)offset;
    static const int16_t anchors[6] = {100, 200, 300, 350, 400, 450};
    for (uint8_t i = 0; i < 6U; ++i) s_candidate.temp_cal_c[i] = anchors[i];
    /* A manually entered value is not a thermometer-checked calibration. */
    s_candidate.characterized = 0U;
    return true;
}

static void save_candidate(void)
{
    tip_profile_t old = *profile_active();
    uint8_t old_room = settings_room_temperature_c();
    if (!profile_apply_tip_setup(&s_candidate)) { fail("Invalid profile"); return; }
    settings_set_room_temperature_c((uint8_t)s_room);
    if (s_first_boot) settings_set_setup_complete(true);
    if (settings_save()) {
        measure_profile_changed(); control_reconfigure(); finish_setup();
    } else {
        if (s_first_boot) settings_set_setup_complete(false);
        settings_set_room_temperature_c(old_room);
        profile_apply_tip_setup(&old);
        profile_custom_commit();
        measure_profile_changed();
        fail("Flash save failed");
    }
}

void tip_setup_button(uint8_t event)
{
    if (event == BUTTON_EVENT_NONE || s_state == OFF) return;
    if (event == BUTTON_EVENT_SHORT) {
        if (s_state == WELCOME) { s_choice = 0U; state(ROUTE); }
        else if (s_state == ROUTE) {
            uint8_t route = s_choice;
            s_choice = 0U;
            state(route == 0U ? TIP_SELECT : FACTORY1);
        } else if (s_state == FACTORY1 || s_state == FACTORY2) {
            if (s_choice == 0U) { s_choice = 0U; state(ROUTE); }
            else if (s_state == FACTORY1) { s_choice = 0U; state(FACTORY2); }
            else {
                settings_set_setup_complete(true);
                if (settings_save()) finish_setup();
                else { settings_set_setup_complete(false); fail("Flash save failed"); }
            }
        } else if (s_state == TIP_SELECT) {
            control_select_profile(s_tip_order[s_choice]);
            s_original = *profile_active(); s_candidate = s_original;
            s_room = settings_room_temperature_c();
            s_baseline_valid = false;
            state(ROOM);
        } else if (s_state == ROOM) {
            s_error = NULL;
            if (checks() && power_get()->pulse_ticks == 0U) {
                uint16_t saved;
                if (saved_room_baseline(&s_original, &saved))
                    s_baseline = saved;
                else s_baseline = measure_get()->temp_filtered_raw;
                s_baseline_valid = true;
                s_staged_slope = s_original.temp_uv_per_c;
                s_manual_tenths = (uint16_t)clamp(
                    ((uint32_t)s_staged_slope * 10U + 63U) / 126U, 80, 950);
                s_manual_sensitivity = false;
                state(SENSITIVITY);
            }
        } else if (s_state == SENSITIVITY) {
            if (s_manual_sensitivity) {
                s_staged_slope = (uint16_t)(((uint32_t)s_manual_tenths * 126U + 5U) / 10U);
                if (!prepare_known_sensitivity()) { fail("Invalid sensitivity"); return; }
                state(KNOWN_NOTICE);
                return;
            }
            s_error = NULL;
            state(WARNING);
        } else if (s_state == KNOWN_NOTICE) {
            save_candidate();
        } else if (s_state == WARNING) {
            s_error = NULL;
            if (checks()) begin_heat();
        } else if (s_state == HEAT) {
            s_capture_pending = true;
            power_force_off();
        }
        else if (s_state == RESULT) {
            if (s_choice == 1U) {
                if (!s_baseline_valid) { fail("Room ADC missing"); return; }
                s_staged_slope = s_candidate.temp_uv_per_c;
                state(WARNING);
                return;
            }
            if (s_choice == 2U) { leave(true); return; }
            save_candidate();
        } else if (s_state == ERROR) leave(true);
    } else if (event == BUTTON_EVENT_LONG) {
        if (s_state == ROOM || s_state == SENSITIVITY || s_state == KNOWN_NOTICE ||
            s_state == WARNING ||
            s_state == HEAT || s_state == RESULT || !s_first_boot) leave(true);
    }
}

void tip_setup_sample(void)
{
    const measurement_t *m = measure_get();
    if (s_state != HEAT) return;
    if (!checks()) { fail(s_error); return; }
    if (s_staged_slope < 1000U) { fail("Tip slope invalid"); return; }
    if (board_millis() - s_since >= 600000U) { fail("Point timed out"); return; }
    uint32_t base_uv = uv(s_baseline);
    int32_t delta_uv = (int32_t)uv(m->temp_raw) - (int32_t)base_uv;
    s_estimated = clamp(s_room + delta_uv / s_staged_slope, -20, 550);
    if (s_previous_estimate_ms != 0U &&
        board_millis() - s_previous_estimate_ms <= 300U &&
        s_previous_estimate >= 130 &&
        s_previous_estimate - s_estimated > 100) {
        fail("Sensor signal dropped"); return;
    }
    s_previous_estimate = s_estimated;
    s_previous_estimate_ms = board_millis();
    int16_t difference = s_target_c - s_estimated;
    if (s_capture_pending) {
        s_capture_pending = false;
        power_force_off();
        if (fit(m->temp_raw, s_external)) { s_choice = 0U; state(RESULT); }
        else fail("Invalid sensitivity");
        return;
    }
    if (s_estimated > 505) { fail("Temperature too high"); return; }
    int32_t drive = difference * 8 + s_integral;
    if (drive < 0) drive = 0;
    if (drive > 200) drive = 200;
    if (difference > 0 && s_integral < 190U) s_integral += 2U;
    if (difference < 0 && s_integral > 1U) s_integral -= 2U;
    power_request_calibration((uint16_t)drive);
}
void tip_setup_poll(void)
{
    if (!tip_setup_active()) return;
    if (board_vib_raw() != STATION_VIB_ACTIVE_LEVEL) s_vib_armed = true;
    if (s_state == HEAT) {
        if (s_state == HEAT && board_millis() - s_since >= 600000U) { fail("Point timed out"); return; }
        if (!checks()) fail(s_error);
    }
}
static void line(uint16_t y, const char *text, uint16_t color)
{
    uint8_t i;
    for (i = 0U; i < s_line_count; ++i) if (s_lines[i].y == y) break;
    if (i == s_line_count) {
        if (s_line_count >= SETUP_LINE_COUNT) return;
        s_lines[i].y = y;
        s_lines[i].text[0] = '\0';
        s_lines[i].color = COLOR_BLACK;
        ++s_line_count;
    }
    if (s_lines[i].color == color && strncmp(s_lines[i].text, text, 25U) == 0) return;
    strncpy(s_lines[i].text, text, 25U);
    s_lines[i].text[25] = '\0';
    s_lines[i].color = color;
    ui_draw_setup_text(y, text, color);
}
void tip_setup_render(void)
{
    if (s_state == OFF) return;
    char b[32];
    if (s_dirty) {
        ST7735_FillRect(s_lcd, 0, 0, 160, 128, COLOR_BLACK);
        s_line_count = 0U;
    }
    line(4, "OpenStation", BLUE);
    if (s_dirty) ST7735_DrawHLine(s_lcd, 0, 16, 160, BLUE);
    if (s_state == WELCOME) {
        line(23, "Welcome to OpenStation!", GOLD);
        snprintf(b, sizeof(b), "Version %s Build %u%s",
                 STATION_FW_VERSION, STATION_FW_BUILD, STATION_FW_BUILD_SUFFIX);
        line(36, b, GOLD);
        line(55, "Firmware by dolce_st", COLOR_WHITE);
        line(67, "github.com/imdolce", COLOR_WHITE);
        line(83, "Hardware by toanthodien16", COLOR_WHITE);
        line(95, "shopee.vn/toanthodien16", COLOR_WHITE);
        line(112, "Click to continue...", MUTED);
    } else if (s_state == ROUTE) {
        line(21, "Setup wizard", GOLD);
        line(38, s_choice ? "  Calibrate your tip" : "> Calibrate your tip",
             s_choice ? COLOR_WHITE : SELECT);
        line(50, "Requires an external", PALE);
        line(61, "thermometer", PALE);
        line(80, s_choice ? "> Use factory defaults" : "  Use factory defaults",
             s_choice ? SELECT : COLOR_WHITE);
        line(92, "Provisional values only", PALE);
    } else if (s_state == FACTORY1 || s_state == FACTORY2) {
        line(21, s_state == FACTORY1 ? "Factory defaults?" : "Confirm again", GOLD);
        line(43, "Calibration advised", PALE);
        line(66, s_choice ? "  NO      > YES" : "> NO        YES", SELECT);
        line(104, s_error ? s_error : "Click to choose", s_error ? COLOR_RED : MUTED);
    } else if (s_state == TIP_SELECT) {
        line(19, "Tip setup", GOLD);
        for (uint8_t i = 0; i < 9U; ++i) {
            snprintf(b, sizeof(b), "%c %s", i == s_choice ? '>' : ' ',
                     profile_get(s_tip_order[i])->name);
            line(29U + 11U * i, b, i == s_choice ? SELECT : COLOR_WHITE);
        }
    } else if (s_state == ROOM) {
        line(19, "Room temperature", GOLD);
        snprintf(b, sizeof(b), "Reference: %dC", s_room); line(39, b, COLOR_WHITE);
        uint16_t saved;
        if (saved_room_baseline(&s_original, &saved)) {
            line(58, "Reuse saved room ADC", PALE);
            snprintf(b, sizeof(b), "Current est: %dC", measure_get()->temperature_c);
            line(69, b, PALE);
        } else {
            line(58, "Tip must be cool and", PALE);
            line(69, "at room temperature", PALE);
        }
        line(91, "Turn to set 0-50C", SELECT);
        line(102, "Click: sample", BLUE);
        line(113, s_error ? s_error : "Hold: stop", s_error ? COLOR_RED : MUTED);
    } else if (s_state == SENSITIVITY) {
        line(19, "Known thermocouple?", GOLD);
        line(35, "Thermocouple sensitivity", PALE);
        snprintf(b, sizeof(b), "%u.%u uV/C", s_manual_tenths / 10U,
                 s_manual_tenths % 10U);
        line(53, b, s_manual_sensitivity ? SELECT : COLOR_WHITE);
        line(71, "Turn to set value", PALE);
        line(91, s_manual_sensitivity ? "Click: use value" : "Click: skip to calibrate", BLUE);
        line(108, "Hold: stop", MUTED);
    } else if (s_state == KNOWN_NOTICE) {
        line(19, "Known sensitivity", GOLD);
        line(32, "You can adjust the tip", COLOR_WHITE);
        line(44, "sensitivity and perform", COLOR_WHITE);
        line(56, "3-point calibration later.", COLOR_WHITE);
        line(72, "These values are", PALE);
        line(84, "specific to each profile.", PALE);
        line(102, "Click: save profile", SELECT);
        line(113, "Hold: cancel", MUTED);
    } else if (s_state == WARNING) {
        line(19, "Prepare for calibration", GOLD);
        line(32, measure_get()->tip_present ? "Tip: connected" : "Tip: missing",
             measure_get()->tip_present ? SELECT : COLOR_RED);
        snprintf(b, sizeof(b), "VIN %lu.%02luV %s",
            (unsigned long)(measure_get()->vin_mv / 1000UL),
            (unsigned long)((measure_get()->vin_mv / 10UL) % 100UL),
            measure_get()->vin_valid ? "OK" : "BAD");
        line(45, b, measure_get()->vin_valid ? SELECT : COLOR_RED);
        line(58, "30W / 20% max", PALE);
        line(70, "Tip may overshoot!", GOLD);
        line(91, "Click to continue", SELECT);
        line(103, s_error ? s_error : "Hold: stop", s_error ? COLOR_RED : MUTED);
        if (s_dirty) ST7735_FillRect(s_lcd, 0U, 112U, 160U, 16U, COLOR_BLACK);
    } else if (s_state == HEAT) {
        line(19, "Tip setup", GOLD);
        snprintf(b, sizeof(b), "Sensitivity target %dC", s_target_c);
        line(31, b, COLOR_WHITE);
        snprintf(b, sizeof(b), "ADC %u  est %dC", measure_get()->temp_raw, s_estimated);
        line(45, b, SELECT);
        snprintf(b, sizeof(b), "Thermometer: %dC", s_external); line(59, b, COLOR_WHITE);
        snprintf(b, sizeof(b), "Time left: %lus", (unsigned long)((600000U -
            (board_millis() - s_since)) / 1000U)); line(73, b, PALE);
        line(88, s_error ? s_error : "Click: sample", s_error ? COLOR_RED : BLUE);
        line(101, "Hold: stop", MUTED);
    } else if (s_state == RESULT) {
        line(19, "Calibration finished", GOLD);
        snprintf(b, sizeof(b), "Sensitivity %u.%u uV/C",
                 (s_candidate.temp_uv_per_c * 10U / 126U) / 10U,
                 (s_candidate.temp_uv_per_c * 10U / 126U) % 10U);
        line(37, b, BLUE);
        line(83, profile_active_is_custom() ?
             (s_choice == 0U ? "> SAVE PROFILE" : "  SAVE PROFILE") :
             (s_choice == 0U ? "> SAVE PROFILE (DEFAULT)" :
                               "  SAVE PROFILE (DEFAULT)"),
             s_choice == 0U ? SELECT : COLOR_WHITE);
        line(97, s_choice == 1U ? "> RETRY" : "  RETRY",
             s_choice == 1U ? SELECT : COLOR_WHITE);
        line(111, s_choice == 2U ? "> CANCEL" : "  CANCEL",
             s_choice == 2U ? SELECT : COLOR_WHITE);
    } else if (s_state == ERROR) {
        line(19, "Tip setup stopped", COLOR_RED);
        line(46, s_error ? s_error : "Unknown error", COLOR_WHITE);
        line(100, "Click to return", MUTED);
    }
    s_dirty = false;
}
