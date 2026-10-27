#include "ui.h"
#include "app_config.h"
#include "board.h"
#include "control.h"
#include "encoder.h"
#include "font5x7.h"
#include "measure.h"
#include "power.h"
#include "profiles.h"
#include "safety.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

#define UI_FIELD_COUNT 16U
#define UI_TEXT_MAX    27U
#define UI_BOX_MAX_H   24U
#define UI_BOX_MAX_W   160U
#define DUTY_BAR_X     30U
#define DUTY_BAR_Y     101U
#define DUTY_BAR_W     71U
#define DUTY_BAR_H     9U

static ST7735_Object_t *s_lcd;
static uint8_t s_orientation;
static uint8_t s_page;
static bool s_static_dirty = true;
static bool s_display_asleep;
static uint8_t s_box_pixels[UI_BOX_MAX_W * UI_BOX_MAX_H * 2U];
static char s_cache[UI_FIELD_COUNT][UI_TEXT_MAX + 1U];
static uint16_t s_cache_color[UI_FIELD_COUNT];
static char s_status_cache[12];
static char s_status_detail_cache[8];
static uint16_t s_status_bg_cache;
static uint16_t s_status_fg_cache;
static bool s_set_boost_cache;
static int16_t s_set_boost_delta_cache = -1;
static uint16_t s_preset_value_cache[3];
static uint16_t s_preset_fg_cache[3];
static uint16_t s_preset_bg_cache[3];
static bool s_fault_popup_visible;
static bool s_fault_popup_dirty;
static bool s_boot_warning_visible;
static bool s_boot_warning_dirty;
static safety_fault_t s_popup_fault;
static uint8_t s_duty_bar_fill = UINT8_MAX;
static uint32_t s_session_start_ms;
static bool s_session_running;
static uint8_t s_settings_selected;
static uint8_t s_settings_scroll;
static uint8_t s_settings_group;
static uint8_t s_settings_level;
static uint8_t s_menu_frame_row = UINT8_MAX;
static bool s_settings_editing;
static uint32_t s_last_sleep_time_rotation_ms;
static bool s_calibration_active;
static bool s_calibration_warning_visible;
static bool s_calibration_warning_dirty;
static bool s_calibration_save_failed;
static const char *s_calibration_error;
static uint8_t s_calibration_point;
static int16_t s_calibration_values[PROFILE_CAL_POINT_COUNT];
static int16_t s_calibration_original[PROFILE_CAL_POINT_COUNT];
static int16_t s_calibration_previous_target;
static uint32_t s_calibration_stable_since_ms;
typedef enum {
    PROFILE_VIEW_ROOT, PROFILE_VIEW_CREATE_TIP, PROFILE_VIEW_NAME,
    PROFILE_VIEW_CREATE_FINISH, PROFILE_VIEW_MANAGE_TIP,
    PROFILE_VIEW_MANAGE_LIST, PROFILE_VIEW_ACTION, PROFILE_VIEW_REMOVE,
    PROFILE_VIEW_DETAILS, PROFILE_VIEW_CAL_VALUES
} profile_view_t;
static profile_view_t s_profile_view;
static uint8_t s_profile_family;
static uint8_t s_profile_slot;
static uint8_t s_name_cursor;
static bool s_name_renaming;
static const char *s_name_error;
static char s_name[PROFILE_CUSTOM_NAME_MAX + 1U];
static bool s_about_visible;

enum {
    SET_TIP_C115 = 0, SET_TIP_C210, SET_TIP_C245, SET_TIP_C470,
    SET_TIP_T12, SET_TIP_TX1, SET_TIP_TX2, SET_TIP_GOOT, SET_TIP_APOLLO,
    SET_PRESET_1, SET_PRESET_2, SET_PRESET_3,
    SET_TC_SCALE, SET_TC_OFFSET, SET_PROFILE_DEFAULT_TEMP,
    SET_PROFILE_MAX_TEMP, SET_MAX_POWER,
    SET_KP, SET_KI, SET_KD, SET_MAX_I,
    SET_CAL_FOLDER,
    SET_MAX_TEMP, SET_BOOST_TEMP, SET_BOOST_TIMER,
    SET_SLEEP_TEMP, SET_SLEEP_DELAY, SET_SLEEP_TIME,
    SET_SLEEP_TYPE, SET_ENCODER_STEP,
    SET_ENCODER_DIR, SET_BACKLIGHT, SET_ORIENTATION,
    SET_BUZZER, SET_READY_BEEP, SET_CAL_WIZARD, SET_SAVE, SET_DEFAULTS,
    SET_DISCARD, SET_IGNORE_FAULTS, SET_BOOT_TIP_WARNING,
    SET_ABOUT, SET_EXIT, SETTINGS_ITEM_COUNT
};
#define SETTINGS_VISIBLE_ROWS 9U

enum { GROUP_GENERAL, GROUP_PRESETS, GROUP_TIPS, GROUP_PROFILES, GROUP_DISPLAY,
       GROUP_SOUND, GROUP_SYSTEM, SETTINGS_GROUP_COUNT };
typedef struct {
    const char *name;
    const uint8_t *items;
    uint8_t count;
} settings_group_t;
static const uint8_t s_general_items[] = {
    SET_MAX_TEMP, SET_BOOST_TEMP, SET_BOOST_TIMER,
    SET_SLEEP_TEMP, SET_SLEEP_DELAY, SET_SLEEP_TIME,
    SET_SLEEP_TYPE, SET_ENCODER_STEP, SET_ENCODER_DIR
};
static const uint8_t s_preset_items[] = {SET_PRESET_1, SET_PRESET_2, SET_PRESET_3};
static const uint8_t s_tip_items[] = {
    SET_TIP_C115, SET_TIP_C210, SET_TIP_C245, SET_TIP_C470, SET_TIP_T12,
    SET_TIP_TX1, SET_TIP_TX2, SET_TIP_GOOT, SET_TIP_APOLLO
};
static const uint8_t s_profile_items[] = {
    SET_PROFILE_DEFAULT_TEMP, SET_PROFILE_MAX_TEMP, SET_SLEEP_TEMP,
    SET_TC_SCALE, SET_TC_OFFSET, SET_MAX_POWER, SET_KP, SET_KI,
    SET_KD, SET_MAX_I, SET_CAL_FOLDER, SET_CAL_WIZARD, SET_SAVE
};
static const uint8_t s_display_items[] = {SET_BACKLIGHT, SET_ORIENTATION};
static const uint8_t s_sound_items[] = {SET_BUZZER, SET_READY_BEEP};
static const uint8_t s_system_items[] = {
    SET_BOOT_TIP_WARNING, SET_IGNORE_FAULTS, SET_SAVE, SET_DEFAULTS,
    SET_DISCARD, SET_ABOUT, SET_EXIT
};
static const settings_group_t s_groups[SETTINGS_GROUP_COUNT] = {
    {"GENERAL", s_general_items, sizeof(s_general_items)},
    {"PRESETS", s_preset_items, sizeof(s_preset_items)},
    {"TIPS", s_tip_items, sizeof(s_tip_items)},
    {"PROFILES", s_profile_items, sizeof(s_profile_items)},
    {"DISPLAY", s_display_items, sizeof(s_display_items)},
    {"SOUND", s_sound_items, sizeof(s_sound_items)},
    {"SYSTEM", s_system_items, sizeof(s_system_items)}
};

static uint8_t selected_setting(void)
{
    const settings_group_t *group = &s_groups[s_settings_group];
    return s_settings_selected < group->count ? group->items[s_settings_selected] : SETTINGS_ITEM_COUNT;
}

static bool landscape(void) { return s_orientation >= ST7735_ORIENTATION_LANDSCAPE; }
static uint16_t screen_width(void) { return landscape() ? 160U : 128U; }
static uint16_t screen_height(void) { return landscape() ? 128U : 160U; }
static uint8_t visible_menu_rows(void) { return landscape() ? 7U : SETTINGS_VISIBLE_ROWS; }

uint8_t ui_display_orientation(void) { return s_orientation; }

bool ui_set_display_orientation(uint8_t orientation)
{
    if (orientation > ST7735_ORIENTATION_LANDSCAPE_ROT180) return false;
    if (s_lcd != NULL && ST7735_SetOrientation(s_lcd, orientation) != ST7735_OK) {
        ST7735_SetOrientation(s_lcd, s_orientation);
        return false;
    }
    s_orientation = orientation;
    s_static_dirty = true;
    s_menu_frame_row = UINT8_MAX;
    s_fault_popup_dirty = true;
    s_boot_warning_dirty = true;
    s_calibration_warning_dirty = true;
    return true;
}

static uint8_t profile_list_slot(uint8_t index)
{
    for (uint8_t slot = 0; slot < PROFILE_CUSTOM_PER_TIP; ++slot) {
        if (!profile_custom_valid(s_profile_family, slot)) continue;
        if (index-- == 0U) return slot;
    }
    return 0xFFU;
}

static uint8_t profile_view_count(void)
{
    switch (s_profile_view) {
    case PROFILE_VIEW_ROOT: return 5U;
    case PROFILE_VIEW_CREATE_TIP: case PROFILE_VIEW_MANAGE_TIP:
        return (uint8_t)(profile_count() + 1U);
    case PROFILE_VIEW_CREATE_FINISH: return 3U;
    case PROFILE_VIEW_MANAGE_LIST: {
        uint8_t count = 1U;
        for (uint8_t slot = 0; slot < PROFILE_CUSTOM_PER_TIP; ++slot)
            if (profile_custom_valid(s_profile_family, slot)) ++count;
        return count;
    }
    case PROFILE_VIEW_ACTION: return 4U;
    case PROFILE_VIEW_REMOVE: return 2U;
    case PROFILE_VIEW_CAL_VALUES: return PROFILE_CAL_POINT_COUNT + 1U;
    default: return 1U;
    }
}

static void profile_open(profile_view_t view)
{
    s_profile_view = view;
    s_settings_selected = 0U;
    s_settings_scroll = 0U;
    s_settings_editing = false;
    s_static_dirty = true;
}

static void profile_begin_name(bool renaming)
{
    s_name_renaming = renaming;
    s_name_error = NULL;
    memset(s_name, 0, sizeof(s_name));
    if (renaming) {
        const char *old = profile_custom_name(s_profile_family, s_profile_slot);
        if (old != NULL) strncpy(s_name, old, PROFILE_CUSTOM_NAME_MAX);
    } else {
        snprintf(s_name, sizeof(s_name), "%s TIP", profile_get(s_profile_family)->name);
    }
    size_t length = strlen(s_name);
    s_name_cursor = length < PROFILE_CUSTOM_NAME_MAX ? (uint8_t)length :
                    PROFILE_CUSTOM_NAME_MAX - 1U;
    profile_open(PROFILE_VIEW_NAME);
}

static void profile_finish_name(void)
{
    for (int8_t i = PROFILE_CUSTOM_NAME_MAX - 1; i >= 0 && s_name[i] == ' '; --i)
        s_name[i] = '\0';
    if (s_name[0] == '\0') { s_name_error = "NAME REQUIRED"; return; }
    if (profile_custom_duplicate(s_name,
                                 s_name_renaming ? s_profile_family : 0xFFU,
                                 s_name_renaming ? s_profile_slot : 0xFFU)) {
        s_name_error = "DUPLICATE PROFILE";
        return;
    }
    if (s_name_renaming) {
        if (profile_custom_rename(s_profile_family, s_profile_slot, s_name)) {
            board_buzzer_pattern(2U);
            profile_open(PROFILE_VIEW_MANAGE_LIST);
        } else s_name_error = "SAVE FAILED";
    } else profile_open(PROFILE_VIEW_CREATE_FINISH);
}

static void profile_press_menu(void)
{
    uint8_t selected = s_settings_selected;
    switch (s_profile_view) {
    case PROFILE_VIEW_ROOT:
        if (selected == 0U) {
            control_select_profile(profile_active_index());
            board_buzzer_pattern(settings_save() ? 2U : 3U);
        } else if (selected == 1U) profile_open(PROFILE_VIEW_CREATE_TIP);
        else if (selected == 2U) profile_open(PROFILE_VIEW_MANAGE_TIP);
        else if (selected == 3U) profile_open(PROFILE_VIEW_DETAILS);
        else {
            s_settings_level = 0U;
            s_settings_selected = GROUP_PROFILES;
            s_settings_scroll = 0U;
            s_static_dirty = true;
        }
        break;
    case PROFILE_VIEW_CREATE_TIP:
        if (selected < profile_count()) {
            s_profile_family = selected;
            profile_begin_name(false);
        } else profile_open(PROFILE_VIEW_ROOT);
        break;
    case PROFILE_VIEW_NAME:
        if (s_name_cursor + 1U < PROFILE_CUSTOM_NAME_MAX) {
            if (s_name[s_name_cursor] == '\0') s_name[s_name_cursor] = ' ';
            ++s_name_cursor;
            s_name_error = NULL;
        } else profile_finish_name();
        break;
    case PROFILE_VIEW_CREATE_FINISH:
        if (selected == 2U) { profile_open(PROFILE_VIEW_NAME); break; }
        {
            int8_t slot = profile_custom_create(s_profile_family, s_name);
            if (slot < 0 || !control_select_custom_profile(s_profile_family, (uint8_t)slot)) {
                s_name_error = "TIP FOLDER FULL";
                profile_open(PROFILE_VIEW_NAME);
                break;
            }
            board_buzzer_pattern(settings_save() ? 2U : 3U);
            profile_open(selected == 0U ? PROFILE_VIEW_ROOT : PROFILE_VIEW_DETAILS);
        }
        break;
    case PROFILE_VIEW_MANAGE_TIP:
        if (selected < profile_count()) {
            s_profile_family = selected;
            profile_open(PROFILE_VIEW_MANAGE_LIST);
        } else profile_open(PROFILE_VIEW_ROOT);
        break;
    case PROFILE_VIEW_MANAGE_LIST:
        s_profile_slot = profile_list_slot(selected);
        if (s_profile_slot == 0xFFU) profile_open(PROFILE_VIEW_MANAGE_TIP);
        else {
            bool loaded = control_select_custom_profile(s_profile_family, s_profile_slot);
            board_buzzer_pattern(loaded && settings_save() ? 2U : 3U);
        }
        break;
    case PROFILE_VIEW_ACTION:
        if (selected == 0U) profile_begin_name(true);
        else if (selected == 1U) {
            if (control_select_custom_profile(s_profile_family, s_profile_slot))
                profile_open(PROFILE_VIEW_DETAILS);
        } else if (selected == 2U) profile_open(PROFILE_VIEW_REMOVE);
        else profile_open(PROFILE_VIEW_MANAGE_LIST);
        break;
    case PROFILE_VIEW_REMOVE:
        if (selected == 0U) {
            bool removed = profile_custom_remove(s_profile_family, s_profile_slot);
            if (removed) {
                measure_profile_changed();
                control_reconfigure();
            }
            board_buzzer_pattern(removed && settings_save() ? 2U : 3U);
        }
        profile_open(PROFILE_VIEW_MANAGE_LIST);
        break;
    case PROFILE_VIEW_CAL_VALUES:
        if (selected >= PROFILE_CAL_POINT_COUNT) {
            profile_open(PROFILE_VIEW_DETAILS);
        } else {
            if (s_settings_editing) board_buzzer_pattern(settings_save() ? 2U : 3U);
            s_settings_editing = !s_settings_editing;
        }
        break;
    default: break;
    }
}

static const int16_t s_calibration_anchors[PROFILE_CAL_POINT_COUNT] =
    {100, 200, 300, 350, 400, 450};

static void calibration_target(void)
{
    control_set_target_c(profile_calibrate_temperature(
        s_calibration_anchors[s_calibration_point]));
}

static void calibration_abort(void)
{
    control_long_press();
    if (s_calibration_save_failed) {
        profile_set_calibration_values(s_calibration_original);
        measure_profile_changed();
    }
    if (s_calibration_active) control_set_target_c(s_calibration_previous_target);
    s_calibration_active = false;
    s_calibration_warning_visible = false;
    s_calibration_save_failed = false;
    s_calibration_error = NULL;
}

static void cache_invalidate(void)
{
    for (uint8_t i = 0; i < UI_FIELD_COUNT; ++i) s_cache[i][0] = '\0';
}

static void pixel_write(uint16_t width, uint16_t x, uint16_t y, uint16_t color)
{
    uint32_t i = ((uint32_t)y * width + x) * 2U;
    s_box_pixels[i] = (uint8_t)(color >> 8);
    s_box_pixels[i + 1U] = (uint8_t)color;
}

/* Render a complete text box in RAM and transfer it through one address window.
 * This replaces hundreds of per-pixel SPI transactions per field. */
static void textbox_advance(uint16_t x, uint16_t y, uint16_t width, const char *value,
                            uint16_t fg, uint16_t bg, uint8_t scale, uint8_t advance)
{
    uint16_t height = (uint16_t)(8U * scale);
    if (width > UI_BOX_MAX_W || height > UI_BOX_MAX_H ||
        x + width > screen_width() || y + height > screen_height()) return;
    uint32_t pixels = (uint32_t)width * height;
    for (uint32_t i = 0; i < pixels; ++i) {
        s_box_pixels[i * 2U] = (uint8_t)(bg >> 8);
        s_box_pixels[i * 2U + 1U] = (uint8_t)bg;
    }

    uint16_t cursor = 0;
    while (*value != '\0' && cursor + (uint16_t)(advance * scale) <= width) {
        uint8_t ch = (uint8_t)*value++;
        if (ch > 127U) ch = (uint8_t)'?';
        const unsigned char *glyph = &font[ch * 5U];
        for (uint8_t col = 0; col < 5U; ++col) {
            for (uint8_t row = 0; row < 8U; ++row) {
                if ((glyph[col] & (1U << row)) == 0U) continue;
                for (uint8_t sx = 0; sx < scale; ++sx)
                    for (uint8_t sy = 0; sy < scale; ++sy)
                        pixel_write(width, (uint16_t)(cursor + col * scale + sx),
                                    (uint16_t)(row * scale + sy), fg);
            }
        }
        cursor = (uint16_t)(cursor + advance * scale);
    }
    ST7735_FillRGBRect(s_lcd, x, y, s_box_pixels, width, height);
}

static void textbox(uint16_t x, uint16_t y, uint16_t width, const char *value,
                    uint16_t fg, uint16_t bg, uint8_t scale)
{
    textbox_advance(x, y, width, value, fg, bg, scale, 6U);
}

static void field(uint8_t id, uint16_t x, uint16_t y, uint16_t width,
                  const char *value, uint16_t color, uint8_t scale)
{
    if (id >= UI_FIELD_COUNT) return;
    if (strncmp(s_cache[id], value, UI_TEXT_MAX) == 0 && s_cache_color[id] == color) return;
    strncpy(s_cache[id], value, UI_TEXT_MAX);
    s_cache[id][UI_TEXT_MAX] = '\0';
    s_cache_color[id] = color;
    textbox(x, y, width, value, color, COLOR_BLACK, scale);
}

/* 1: stand delay, 2: remaining time at the sleep setpoint. */
static uint8_t sleep_countdown(const control_status_t *c, uint32_t *seconds_left)
{
    if (control_sleep_type() == SLEEP_TYPE_NONE) return 0U;
    if (c->sleep_since_ms != 0U &&
        (c->state == STATE_SLEEP || c->sleep_timed_out)) {
        uint32_t duration_ms = (uint32_t)control_sleep_time_s() * 1000UL;
        uint32_t elapsed_ms = (uint32_t)(board_millis() - c->sleep_since_ms);
        uint32_t remaining_ms = elapsed_ms >= duration_ms ? 0U : duration_ms - elapsed_ms;
        *seconds_left = (remaining_ms + 999U) / 1000U;
        return 2U;
    }
    if (c->heating_enabled && c->stand_active && c->stand_since_ms != 0U) {
        uint32_t delay_ms = control_sleep_delay_ms();
        uint32_t elapsed_ms = (uint32_t)(board_millis() - c->stand_since_ms);
        uint32_t remaining_ms = elapsed_ms >= delay_ms ? 0U : delay_ms - elapsed_ms;
        *seconds_left = (remaining_ms + 999U) / 1000U;
        return 1U;
    }
    return 0U;
}

static void degree_mark(uint16_t x, uint16_t y, uint16_t color)
{
    ST7735_DrawHLine(s_lcd, x + 1U, y, 2U, color);
    ST7735_DrawHLine(s_lcd, x + 1U, y + 3U, 2U, color);
    ST7735_DrawVLine(s_lcd, x, y + 1U, 2U, color);
    ST7735_DrawVLine(s_lcd, x + 3U, y + 1U, 2U, color);
}

static void set_boost_indicator_update(const control_status_t *c)
{
    bool boosted = c->state == STATE_BOOST;
    int16_t delta = boosted ? c->active_setpoint_c - c->target_c : 0;
    if (delta < 0) delta = 0;
    if (s_set_boost_cache == boosted && s_set_boost_delta_cache == delta) return;
    s_set_boost_cache = boosted;
    s_set_boost_delta_cache = delta;

    uint16_t label_y = landscape() ? 54U : 19U;
    ST7735_FillRect(s_lcd, 22U, label_y, 43U, 8U, COLOR_BLACK);
    if (boosted) {
        char prefix[8];
        snprintf(prefix, sizeof(prefix), "(+%d", delta);
        uint16_t prefix_width = (uint16_t)(strlen(prefix) * 5U);
        textbox_advance(22U, label_y, prefix_width, prefix,
                        COLOR_RED, COLOR_BLACK, 1U, 5U);
        degree_mark((uint16_t)(23U + prefix_width), label_y + 2U, COLOR_RED);
        textbox_advance((uint16_t)(29U + prefix_width), label_y, 10U,
                        "C)", COLOR_RED, COLOR_BLACK, 1U, 5U);
    }

    uint16_t set_color = boosted ? COLOR_RED :
                         (landscape() ? COLOR_GREEN : COLOR_WHITE);
    degree_mark(landscape() ? 42U : 58U,
                landscape() ? 69U : 34U, set_color);
    textbox(landscape() ? 48U : 63U,
            landscape() ? 67U : 35U, 12U, "C",
            set_color, COLOR_BLACK, 2U);
}

static void status_box_update(const char *label, const char *detail,
                              uint16_t fg, uint16_t bg)
{
    if (detail == NULL) detail = "";
    if (strcmp(s_status_cache, label) == 0 &&
        strcmp(s_status_detail_cache, detail) == 0 &&
        s_status_fg_cache == fg && s_status_bg_cache == bg) return;
    strncpy(s_status_cache, label, sizeof(s_status_cache) - 1U);
    s_status_cache[sizeof(s_status_cache) - 1U] = '\0';
    strncpy(s_status_detail_cache, detail, sizeof(s_status_detail_cache) - 1U);
    s_status_detail_cache[sizeof(s_status_detail_cache) - 1U] = '\0';
    s_status_bg_cache = bg;
    s_status_fg_cache = fg;
    const uint16_t box_x = landscape() ? 71U : 89U;
    const uint16_t box_y = landscape() ? 19U : 66U;
    const uint16_t box_w = landscape() ? 47U : 38U;
    const uint16_t box_h = landscape() ? 40U : 21U;
    ST7735_FillRect(s_lcd, box_x, box_y, box_w, box_h, bg);
    uint16_t text_width = (uint16_t)(strlen(label) * 6U);
    if (text_width > box_w) text_width = box_w;
    textbox((uint16_t)(box_x + (box_w - text_width) / 2U),
            detail[0] != '\0' ? (landscape() ? 30U : 67U) :
                                 (landscape() ? 35U : 72U),
            text_width, label, fg, bg, 1);
    if (detail[0] != '\0') {
        uint16_t detail_width = (uint16_t)(strlen(detail) * 6U);
        if (detail_width > box_w) detail_width = box_w;
        textbox((uint16_t)(box_x + (box_w - detail_width) / 2U),
                landscape() ? 43U : 78U,
                detail_width, detail, fg, bg, 1);
    }
}

static void main_status_update(const control_status_t *c, bool tip_present)
{
    const char *state = "Idle";
    if (safety_latched()) state = "Fault";
    else if (c->state == STATE_SLEEP || c->sleep_timed_out) state = "Sleep";
    else if (control_sleep_type() != SLEEP_TYPE_NONE &&
             c->stand_active && c->heating_enabled) state = "Stand";
    else if (c->state == STATE_BOOST) state = "Boost";
    else if (c->heating_enabled) state = "Active";
    else if (!tip_present) state = "No tip";
    else if (c->state == STATE_COOLING) state = "Cool";

    uint16_t bg = safety_latched() ? COLOR_RED :
                  (strcmp(state, "Sleep") == 0 ? COLOR_CYAN :
                   (strcmp(state, "Boost") == 0 ? COLOR_YELLOW :
                    (strcmp(state, "Active") == 0 ? COLOR_GREEN : COLOR_BLACK)));
    uint16_t fg = bg == COLOR_BLACK || bg == COLOR_RED ? COLOR_WHITE : COLOR_BLACK;
    char remaining[8] = "";
    if (c->state == STATE_BOOST && strcmp(state, "Boost") == 0)
        snprintf(remaining, sizeof(remaining), "%02u:%02u",
                 c->boost_seconds / 60U, c->boost_seconds % 60U);
    status_box_update(state, remaining, fg, bg);
}

static void preset_row_update(uint8_t row, uint16_t value, bool selected, bool disabled)
{
    uint16_t fg = selected ? COLOR_BLACK : (disabled ? COLOR_GRAY : COLOR_WHITE);
    uint16_t bg = selected ? COLOR_GREEN : COLOR_BLACK;
    if (s_preset_value_cache[row] == value && s_preset_fg_cache[row] == fg &&
        s_preset_bg_cache[row] == bg) return;
    s_preset_value_cache[row] = value;
    s_preset_fg_cache[row] = fg;
    s_preset_bg_cache[row] = bg;
    uint16_t y = (uint16_t)((landscape() ? 20U : 18U) + row * 13U);
    uint16_t x = landscape() ? 123U : 95U;
    ST7735_FillRect(s_lcd, x, y, landscape() ? 34U : 32U, 12, bg);
    char digits[6];
    snprintf(digits, sizeof(digits), "%3u", value);
    textbox(x + 1U, y + 2U, 18, digits, fg, bg, 1);
    degree_mark(x + 20U, y + 2U, fg);
    textbox(x + 25U, y + 2U, 6, "C", fg, bg, 1);
}

static void duty_bar_update(uint16_t duty_permille)
{
    const uint16_t bar_x = landscape() ? 71U : DUTY_BAR_X;
    const uint16_t bar_y = landscape() ? 64U : DUTY_BAR_Y;
    const uint16_t bar_w = landscape() ? 86U : DUTY_BAR_W;
    const uint16_t bar_h = landscape() ? 11U : DUTY_BAR_H;
    const uint8_t inner_w = (uint8_t)(bar_w - 2U);
    uint8_t fill = (uint8_t)(((uint32_t)duty_permille * inner_w + 500U) / 1000U);
    if (fill > inner_w) fill = inner_w;

    if (s_duty_bar_fill == UINT8_MAX) {
        ST7735_FillRect(s_lcd, bar_x + 1U, bar_y + 1U,
                        inner_w, bar_h - 2U, COLOR_BLACK);
        if (fill != 0U)
            ST7735_FillRect(s_lcd, bar_x + 1U, bar_y + 1U,
                            fill, bar_h - 2U, COLOR_WHITE);
    } else if (fill > s_duty_bar_fill) {
        ST7735_FillRect(s_lcd, bar_x + 1U + s_duty_bar_fill,
                        bar_y + 1U, fill - s_duty_bar_fill,
                        bar_h - 2U, COLOR_WHITE);
    } else if (fill < s_duty_bar_fill) {
        ST7735_FillRect(s_lcd, bar_x + 1U + fill,
                        bar_y + 1U, s_duty_bar_fill - fill,
                        bar_h - 2U, COLOR_BLACK);
    }
    s_duty_bar_fill = fill;
}

static void draw_static(void)
{
    ST7735_FillScreen(s_lcd, COLOR_BLACK);
    cache_invalidate();
    s_status_cache[0] = '\0';
    s_status_detail_cache[0] = '\0';
    s_set_boost_delta_cache = -1;
    for (uint8_t i = 0; i < 3U; ++i) s_preset_value_cache[i] = UINT16_MAX;
    if (s_page == 0U) {
        if (landscape()) {
            textbox(3, 3, 90, "OpenStation", COLOR_CYAN, COLOR_BLACK, 1);
            textbox(122, 3, 36, STATION_FW_VERSION, COLOR_CYAN, COLOR_BLACK, 1);
            ST7735_DrawHLine(s_lcd, 0, 16, 160, COLOR_CYAN);
            textbox(3, 21, 60, "Actual", COLOR_YELLOW, COLOR_BLACK, 1);
            textbox(3, 54, 60, "Set", COLOR_GREEN, COLOR_BLACK, 1);
            degree_mark(42, 36, COLOR_YELLOW);
            textbox(48, 34, 12, "C", COLOR_YELLOW, COLOR_BLACK, 2);
            degree_mark(42, 69, COLOR_GREEN);
            textbox(48, 67, 12, "C", COLOR_GREEN, COLOR_BLACK, 2);
            ST7735_DrawVLine(s_lcd, 67, 17, 70, COLOR_CYAN);
            ST7735_DrawHLine(s_lcd, 122, 19, 36, COLOR_WHITE);
            ST7735_DrawHLine(s_lcd, 122, 32, 36, COLOR_WHITE);
            ST7735_DrawHLine(s_lcd, 122, 45, 36, COLOR_WHITE);
            ST7735_DrawHLine(s_lcd, 122, 59, 36, COLOR_WHITE);
            ST7735_DrawVLine(s_lcd, 122, 19, 41, COLOR_WHITE);
            ST7735_DrawVLine(s_lcd, 157, 19, 41, COLOR_WHITE);
            ST7735_DrawHLine(s_lcd, 71, 64, 86, COLOR_WHITE);
            ST7735_DrawHLine(s_lcd, 71, 74, 86, COLOR_WHITE);
            ST7735_DrawVLine(s_lcd, 71, 64, 11, COLOR_WHITE);
            ST7735_DrawVLine(s_lcd, 156, 64, 11, COLOR_WHITE);
            textbox(71, 77, 30, "Duty", COLOR_WHITE, COLOR_BLACK, 1);
            ST7735_DrawHLine(s_lcd, 0, 87, 160, COLOR_CYAN);
            s_duty_bar_fill = UINT8_MAX;
        } else {
        textbox(2, 2, 90, "OpenStation", COLOR_CYAN, COLOR_BLACK, 1);
        textbox(92, 2, 36, STATION_FW_VERSION, COLOR_CYAN, COLOR_BLACK, 1);
        ST7735_DrawHLine(s_lcd, 0, 14, 128, COLOR_CYAN);

        textbox(3, 19, 70, "Set", COLOR_WHITE, COLOR_BLACK, 1);
        degree_mark(58, 34, COLOR_WHITE);
        textbox(63, 35, 12, "C", COLOR_WHITE, COLOR_BLACK, 2);
        textbox(3, 59, 70, "Actual temp", COLOR_YELLOW, COLOR_BLACK, 1);
        degree_mark(58, 74, COLOR_YELLOW);
        textbox(63, 75, 12, "C", COLOR_YELLOW, COLOR_BLACK, 2);

        ST7735_DrawHLine(s_lcd, 94, 17, 34, COLOR_WHITE);
        ST7735_DrawHLine(s_lcd, 94, 30, 34, COLOR_WHITE);
        ST7735_DrawHLine(s_lcd, 94, 43, 34, COLOR_WHITE);
        ST7735_DrawHLine(s_lcd, 94, 56, 34, COLOR_WHITE);
        ST7735_DrawVLine(s_lcd, 94, 17, 40, COLOR_WHITE);
        ST7735_DrawVLine(s_lcd, 127, 17, 40, COLOR_WHITE);

        ST7735_DrawHLine(s_lcd, 88, 65, 40, COLOR_WHITE);
        ST7735_DrawHLine(s_lcd, 88, 87, 40, COLOR_WHITE);
        ST7735_DrawVLine(s_lcd, 88, 65, 23, COLOR_WHITE);
        ST7735_DrawVLine(s_lcd, 127, 65, 23, COLOR_WHITE);

        textbox(3, 101, 25, "Duty", COLOR_WHITE, COLOR_BLACK, 1);
        ST7735_DrawHLine(s_lcd, DUTY_BAR_X, DUTY_BAR_Y, DUTY_BAR_W, COLOR_WHITE);
        ST7735_DrawHLine(s_lcd, DUTY_BAR_X, DUTY_BAR_Y + DUTY_BAR_H - 1U,
                         DUTY_BAR_W, COLOR_WHITE);
        ST7735_DrawVLine(s_lcd, DUTY_BAR_X, DUTY_BAR_Y, DUTY_BAR_H, COLOR_WHITE);
        ST7735_DrawVLine(s_lcd, DUTY_BAR_X + DUTY_BAR_W - 1U, DUTY_BAR_Y,
                         DUTY_BAR_H, COLOR_WHITE);
        s_duty_bar_fill = UINT8_MAX;
        }
    } else if (s_calibration_active) {
        ST7735_FillRect(s_lcd, 0, 0, screen_width(), 18, 0x1082);
        textbox(3, 5, 118, "TEMP CALIBRATION", COLOR_WHITE, 0x1082, 1);
    } else {
        ST7735_DrawHLine(s_lcd, 0, 17, screen_width(), COLOR_GREEN);
        const char *heading = s_settings_level == 0U ? "SETTINGS" :
                              s_groups[s_settings_group].name;
        if (s_settings_level != 0U && s_settings_group == GROUP_PROFILES) {
            switch (s_profile_view) {
            case PROFILE_VIEW_CREATE_TIP: heading = "CHOOSE TIP"; break;
            case PROFILE_VIEW_NAME: heading = "NAME PROFILE"; break;
            case PROFILE_VIEW_CREATE_FINISH: heading = "PROFILE VALUES"; break;
            case PROFILE_VIEW_MANAGE_TIP: heading = "MANAGE / TIPS"; break;
            case PROFILE_VIEW_MANAGE_LIST: heading = profile_get(s_profile_family)->name; break;
            case PROFILE_VIEW_ACTION: heading = "MANAGE PROFILE"; break;
            case PROFILE_VIEW_REMOVE: heading = "REMOVE PROFILE?"; break;
            case PROFILE_VIEW_DETAILS: heading = profile_active_name(); break;
            case PROFILE_VIEW_CAL_VALUES: heading = "6-POINT VALUES"; break;
            default: heading = "PROFILES"; break;
            }
        }
        textbox(3, 5, 118, heading, COLOR_GREEN, COLOR_BLACK, 1);
        if (s_settings_level != 0U && s_settings_group == GROUP_PROFILES &&
            s_profile_view == PROFILE_VIEW_ROOT) {
            ST7735_DrawHLine(s_lcd, 3, 21, screen_width() - 6U, COLOR_GREEN);
            ST7735_DrawHLine(s_lcd, 3, 55, screen_width() - 6U, COLOR_GREEN);
            ST7735_DrawVLine(s_lcd, 3, 21, 35, COLOR_GREEN);
            ST7735_DrawVLine(s_lcd, screen_width() - 4U, 21, 35, COLOR_GREEN);
            textbox(8, 25, 118, "CURRENT PROFILE", COLOR_CYAN, COLOR_BLACK, 1);
        }
        s_menu_frame_row = UINT8_MAX;
    }
    s_static_dirty = false;
}

void ui_init(ST7735_Object_t *lcd)
{
    s_lcd = lcd;
    ui_set_display_orientation(s_orientation);
    s_page = 0;
    s_settings_selected = 0;
    s_settings_scroll = 0;
    s_settings_group = GROUP_GENERAL;
    s_settings_level = 0;
    s_settings_editing = false;
    s_calibration_active = false;
    s_calibration_warning_visible = false;
    s_calibration_save_failed = false;
    s_calibration_error = NULL;
    s_profile_view = PROFILE_VIEW_ROOT;
    s_about_visible = false;
    s_boot_warning_visible = settings_tip_warning_enabled();
    s_boot_warning_dirty = s_boot_warning_visible;
    cache_invalidate();
}

void ui_toggle_page(void)
{
    if (s_calibration_active || s_calibration_warning_visible) calibration_abort();
    s_page ^= 1U;
    s_settings_editing = false;
    s_settings_level = 0;
    s_about_visible = false;
    s_settings_selected = s_settings_group;
    s_settings_scroll = 0;
    s_static_dirty = true;
}
bool ui_settings_active(void) { return s_page != 0U; }

void ui_settings_rotate(int8_t detents)
{
    if (detents == 0) return;
    if (s_about_visible) return;
    if (s_calibration_warning_visible) return;
    if (s_calibration_active) {
        uint8_t point = s_calibration_point;
        int32_t value = (int32_t)s_calibration_values[point] + detents;
        int16_t minimum = point == 0U ? 0 : s_calibration_values[point - 1U] + 1;
        int16_t maximum = (int16_t)(550 - (PROFILE_CAL_POINT_COUNT - point - 1U));
        if (value < minimum) value = minimum;
        if (value > maximum) value = maximum;
        s_calibration_values[point] = (int16_t)value;
        for (uint8_t i = point + 1U; i < PROFILE_CAL_POINT_COUNT; ++i)
            if (s_calibration_values[i] <= s_calibration_values[i - 1U])
                s_calibration_values[i] = s_calibration_values[i - 1U] + 1;
        s_calibration_error = NULL;
        return;
    }
    if (s_settings_level != 0U && s_settings_group == GROUP_PROFILES &&
        s_profile_view != PROFILE_VIEW_DETAILS) {
        if (s_profile_view == PROFILE_VIEW_NAME) {
            static const char glyphs[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            char current = s_name[s_name_cursor];
            uint8_t at = 0U;
            while (glyphs[at] != '\0' && glyphs[at] != current) ++at;
            if (glyphs[at] == '\0') at = 0U;
            int16_t next = (int16_t)at + detents;
            const int16_t count = (int16_t)(sizeof(glyphs) - 1U);
            while (next < 0) next += count;
            while (next >= count) next -= count;
            s_name[s_name_cursor] = glyphs[next];
            s_name_error = NULL;
            return;
        }
        if (s_profile_view == PROFILE_VIEW_CAL_VALUES && s_settings_editing) {
            const tip_profile_t *p = profile_active();
            uint8_t point = s_settings_selected;
            if (point < PROFILE_CAL_POINT_COUNT) {
                profile_set_calibration(point, (int16_t)(p->temp_cal_c[point] + detents));
                measure_profile_changed();
            }
            return;
        }
        uint8_t count = profile_view_count();
        int16_t selected = (int16_t)s_settings_selected + detents;
        while (selected < 0) selected += count;
        while (selected >= count) selected -= count;
        s_settings_selected = (uint8_t)selected;
        if (s_settings_selected < s_settings_scroll) s_settings_scroll = s_settings_selected;
        if (s_settings_selected >= s_settings_scroll + visible_menu_rows())
            s_settings_scroll = s_settings_selected - visible_menu_rows() + 1U;
        return;
    }
    if (!s_settings_editing) {
        uint8_t count = s_settings_level == 0U ? SETTINGS_GROUP_COUNT :
                        (uint8_t)(s_groups[s_settings_group].count + 1U);
        int16_t selected = (int16_t)s_settings_selected + detents;
        while (selected < 0) selected += count;
        while (selected >= count) selected -= count;
        s_settings_selected = (uint8_t)selected;
        if (s_settings_selected < s_settings_scroll) s_settings_scroll = s_settings_selected;
        if (s_settings_selected >= s_settings_scroll + visible_menu_rows())
            s_settings_scroll = s_settings_selected - visible_menu_rows() + 1U;
        return;
    }

    const tip_profile_t *p = profile_active();
    uint8_t item = selected_setting();
    int32_t value;
    switch (item) {
    case SET_PRESET_1: case SET_PRESET_2: case SET_PRESET_3:
        value = (int32_t)control_preset_c((uint8_t)(item - SET_PRESET_1)) +
                (int32_t)detents * 5;
        control_set_preset_c((uint8_t)(item - SET_PRESET_1),
                             (uint16_t)(value < 150 ? 150 : value));
        break;
    case SET_TC_SCALE:
        value = (int32_t)p->temp_uv_per_c + (int32_t)detents * 50;
        profile_set_temp_uv_per_c((uint16_t)(value < 0 ? 0 : value));
        measure_profile_changed();
        break;
    case SET_TC_OFFSET:
        value = (int32_t)p->temp_offset_c + detents;
        profile_set_temp_offset_c((int16_t)value);
        measure_profile_changed();
        break;
    case SET_PROFILE_DEFAULT_TEMP:
        value = (int32_t)p->default_temp_c + (int32_t)detents * 5;
        profile_set_default_temp_c((int16_t)value);
        control_reconfigure();
        break;
    case SET_PROFILE_MAX_TEMP:
        value = (int32_t)p->maximum_temp_c + (int32_t)detents * 5;
        profile_set_max_temp_c((int16_t)value);
        control_reconfigure();
        break;
    case SET_MAX_POWER:
        value = (int32_t)(p->max_power_mw / 1000UL) + (int32_t)detents * 5;
        profile_set_max_power_mw((uint32_t)(value < 0 ? 0 : value) * 1000UL);
        break;
    case SET_KP:
        value = (int32_t)p->kp_q8 + (int32_t)detents * 26;
        profile_set_pid_q8((uint16_t)(value < 0 ? 0 : value), p->ki_q8, p->kd_q8);
        break;
    case SET_KI:
        value = (int32_t)p->ki_q8 + (int32_t)detents * 26;
        profile_set_pid_q8(p->kp_q8, (uint16_t)(value < 0 ? 0 : value), p->kd_q8);
        break;
    case SET_KD:
        value = (int32_t)p->kd_q8 + (int32_t)detents * 26;
        profile_set_pid_q8(p->kp_q8, p->ki_q8, (uint16_t)(value < 0 ? 0 : value));
        break;
    case SET_MAX_I:
        value = (int32_t)p->max_integral_permille + (int32_t)detents * 10;
        profile_set_max_integral_permille((uint16_t)(value < 0 ? 0 : value));
        break;
    case SET_SLEEP_TEMP:
        value = (int32_t)p->sleep_temp_c + (int32_t)detents * 5;
        profile_set_sleep_temp_c((int16_t)value);
        break;
    case SET_MAX_TEMP:
        value = (int32_t)control_max_target_c() + (int32_t)detents * 5;
        control_set_max_target_c((uint16_t)(value < 150 ? 150 : value));
        break;
    case SET_BOOST_TEMP:
        value = (int32_t)control_boost_delta_c() + (int32_t)detents * 5;
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        control_set_boost_delta_c((uint8_t)value);
        break;
    case SET_BOOST_TIMER:
        value = (int32_t)control_boost_time_s() + (int32_t)detents * 5;
        if (value < 15) value = 15;
        if (value > 120) value = 120;
        control_set_boost_time_s((uint8_t)value);
        break;
    case SET_SLEEP_DELAY:
        value = (int32_t)(control_sleep_delay_ms() / 1000UL) + detents;
        control_set_sleep_delay_ms((uint32_t)(value < 0 ? 0 : value) * 1000UL);
        break;
    case SET_SLEEP_TIME: {
        uint32_t now = board_millis();
        uint32_t gap_ms = (uint32_t)(now - s_last_sleep_time_rotation_ms);
        uint8_t magnitude = detents < 0 ? (uint8_t)-detents : (uint8_t)detents;
        uint8_t step_s = magnitude >= 3U ||
                         (s_last_sleep_time_rotation_ms != 0U && gap_ms < 70U) ? 60U :
                         (magnitude >= 2U ||
                          (s_last_sleep_time_rotation_ms != 0U && gap_ms < 160U) ? 30U : 10U);
        value = (int32_t)control_sleep_time_s() + (int32_t)detents * step_s;
        control_set_sleep_time_s((uint16_t)(value < 30 ? 30 : value));
        s_last_sleep_time_rotation_ms = now;
        break;
    }
    case SET_SLEEP_TYPE: {
        int16_t next = (int16_t)control_sleep_type() + detents;
        while (next < 0) next += 4;
        while (next >= 4) next -= 4;
        control_set_sleep_type((sleep_type_t)next);
        break;
    }
    case SET_ENCODER_STEP: {
        uint8_t step = control_encoder_step_c();
        int16_t index = step == 1U ? 0 : (step == 5U ? 1 : 2);
        index += detents;
        while (index < 0) index += 3;
        while (index >= 3) index -= 3;
        control_set_encoder_step_c(index == 0 ? 1U : (index == 1 ? 5U : 10U));
        break;
    }
    case SET_BACKLIGHT:
        value = (int32_t)board_backlight_get() + (int32_t)detents * 5;
        if (value < 10) value = 10;
        if (value > 100) value = 100;
        board_backlight_set((uint8_t)value);
        break;
    case SET_ORIENTATION: {
        int16_t next = (int16_t)s_orientation + detents;
        while (next < 0) next += 4;
        while (next >= 4) next -= 4;
        ui_set_display_orientation((uint8_t)next);
        s_settings_scroll = 0U;
        break;
    }
    case SET_ENCODER_DIR:
        if (detents & 1) input_set_encoder_reversed(!input_encoder_reversed());
        break;
    case SET_IGNORE_FAULTS:
        if (detents & 1) {
            safety_set_ignore_faults(!safety_ignore_faults());
            control_reconfigure();
        }
        break;
    case SET_BOOT_TIP_WARNING:
        if (detents & 1) settings_set_tip_warning_enabled(
            !settings_tip_warning_enabled());
        break;
    case SET_BUZZER:
        if (detents & 1) board_buzzer_enable(!board_buzzer_enabled());
        break;
    case SET_READY_BEEP:
        if (detents & 1) board_setpoint_beep_enable(!board_setpoint_beep_get());
        break;
    default:
        break;
    }
}

void ui_settings_press(void)
{
    if (s_about_visible) {
        s_about_visible = false;
        s_static_dirty = true;
        return;
    }
    if (s_calibration_warning_visible) {
        const measurement_t *m = measure_get();
        const tip_profile_t *p = profile_active();
        if (!m->tip_present || !m->quiet_sample_valid) {
            s_calibration_error = "CONNECT TIP FIRST";
        } else if (safety_latched()) {
            s_calibration_error = "CLEAR FAULT FIRST";
        } else if (control_get()->stand_active) {
            s_calibration_error = "REMOVE FROM STAND";
        } else if (control_max_target_c() < 450U || p->maximum_temp_c < 450) {
            s_calibration_error = "SET MAX TO 450C";
        } else {
            memcpy(s_calibration_original, p->temp_cal_c, sizeof(s_calibration_original));
            memcpy(s_calibration_values, p->temp_cal_c, sizeof(s_calibration_values));
            s_calibration_previous_target = control_get()->target_c;
            s_calibration_warning_visible = false;
            s_calibration_active = true;
            s_calibration_save_failed = false;
            s_calibration_error = NULL;
            s_calibration_point = 0U;
            s_calibration_stable_since_ms = 0U;
            calibration_target();
            control_short_press();
            if (!control_get()->heating_enabled) {
                calibration_abort();
                s_calibration_warning_visible = true;
                s_calibration_error = "HEAT NOT ENABLED";
            }
        }
        s_calibration_warning_dirty = true;
        s_static_dirty = true;
        return;
    }
    if (s_calibration_active) {
        const measurement_t *m = measure_get();
        int16_t difference = m->temperature_base_c -
                             s_calibration_anchors[s_calibration_point];
        if (difference < 0) difference = -difference;
        if (!s_calibration_save_failed &&
            (!m->tip_present || !m->quiet_sample_valid ||
             !control_get()->heating_enabled || control_get()->stand_active ||
             safety_latched() || difference > 25 || m->temp_burst_span_raw > 50U ||
             s_calibration_stable_since_ms == 0U ||
             board_millis() - s_calibration_stable_since_ms < 2000U)) {
            s_calibration_error = "WAIT FOR STABLE TIP";
            return;
        }
        s_calibration_error = NULL;
        if (s_calibration_point + 1U < PROFILE_CAL_POINT_COUNT) {
            ++s_calibration_point;
            s_calibration_stable_since_ms = 0U;
            calibration_target();
        } else {
            control_long_press();
            if (!profile_set_calibration_values(s_calibration_values)) {
                s_calibration_error = "INVALID 6 VALUES";
            } else if (!settings_save()) {
                s_calibration_save_failed = true;
                s_calibration_error = "SAVE FAILED - RETRY";
            } else {
                s_calibration_save_failed = false;
                measure_profile_changed();
                control_set_target_c(s_calibration_previous_target);
                s_calibration_active = false;
                profile_open(PROFILE_VIEW_CAL_VALUES);
                board_buzzer_pattern(2U);
            }
        }
        s_static_dirty = true;
        return;
    }
    if (s_settings_level == 0U) {
        s_settings_group = s_settings_selected;
        s_settings_level = 1U;
        if (s_settings_group == GROUP_PROFILES) profile_open(PROFILE_VIEW_ROOT);
        else {
            s_settings_selected = 0U;
            s_settings_scroll = 0U;
            s_static_dirty = true;
        }
        return;
    }
    if (s_settings_group == GROUP_PROFILES && s_profile_view != PROFILE_VIEW_DETAILS) {
        profile_press_menu();
        return;
    }
    uint8_t item = selected_setting();
    if (item == SETTINGS_ITEM_COUNT) {
        if (s_settings_group == GROUP_PROFILES) {
            profile_open(PROFILE_VIEW_ROOT);
            return;
        }
        s_settings_level = 0U;
        s_settings_selected = s_settings_group;
        s_settings_scroll = 0U;
        s_settings_editing = false;
        s_static_dirty = true;
        return;
    }
    if (item >= SET_TIP_C115 && item <= SET_TIP_APOLLO) {
        control_select_profile((uint8_t)(item - SET_TIP_C115));
        s_settings_editing = false;
        return;
    }
    if (item == SET_EXIT) {
        ui_toggle_page();
        return;
    }
    if (item == SET_ABOUT) {
        s_about_visible = true;
        s_static_dirty = true;
        return;
    }
    if (item == SET_CAL_FOLDER) {
        profile_open(PROFILE_VIEW_CAL_VALUES);
        return;
    }
    if (item == SET_CAL_WIZARD) {
        control_long_press();
        s_calibration_warning_visible = true;
        s_calibration_warning_dirty = true;
        s_calibration_error = NULL;
        s_settings_editing = false;
        s_static_dirty = true;
        return;
    }
    if (item == SET_SAVE) {
        board_buzzer_pattern(settings_save() ? 2U : 3U);
        return;
    }
    if (item == SET_DEFAULTS) {
        settings_factory_defaults();
        measure_profile_changed();
        control_reconfigure();
        bool erased = profiles_custom_clear_all();
        board_buzzer_pattern(erased && settings_save() ? 2U : 3U);
        s_settings_editing = false;
        s_static_dirty = true;
        return;
    }
    if (item == SET_DISCARD) {
        settings_discard();
        measure_profile_changed();
        control_reconfigure();
        s_settings_editing = false;
        s_static_dirty = true;
        return;
    }
    s_settings_editing = !s_settings_editing;
    s_last_sleep_time_rotation_ms = 0U;
}
void ui_settings_double_press(void)
{
    if (s_settings_level == 0U || s_settings_group != GROUP_PROFILES) return;
    if (s_profile_view == PROFILE_VIEW_NAME) {
        if (s_name_cursor > 0U) {
            s_name[s_name_cursor] = '\0';
            --s_name_cursor;
            s_name[s_name_cursor] = '\0';
            s_name_error = NULL;
        }
    } else if (s_profile_view == PROFILE_VIEW_MANAGE_LIST) {
        uint8_t slot = profile_list_slot(s_settings_selected);
        if (slot != 0xFFU) {
            s_profile_slot = slot;
            profile_open(PROFILE_VIEW_ACTION);
        }
    }
}
bool ui_fault_popup_active(void) { return s_fault_popup_visible; }

void ui_dismiss_fault_popup(void)
{
    s_fault_popup_visible = false;
    if (s_boot_warning_visible) s_boot_warning_dirty = true;
    s_static_dirty = true;
}
bool ui_boot_warning_active(void) { return s_boot_warning_visible; }
void ui_dismiss_boot_warning(void)
{
    s_boot_warning_visible = false;
    s_static_dirty = true;
}

static void draw_boot_tip_warning(void)
{
    char b[UI_TEXT_MAX + 1U];
    ST7735_FillScreen(s_lcd, COLOR_BLACK);
    if (landscape()) {
        ST7735_DrawHLine(s_lcd, 4, 15, 152, COLOR_ORANGE);
        textbox(5, 4, 120, "CHECK TIP", COLOR_ORANGE, COLOR_BLACK, 1);
        snprintf(b, sizeof(b), "PROFILE: %s", profile_active_name());
        textbox(7, 29, 128, b, COLOR_YELLOW, COLOR_BLACK, 1);
        textbox(7, 48, 128, "VERIFY IRON TIP MATCH", COLOR_WHITE, COLOR_BLACK, 1);
        textbox(7, 62, 128, "WITH THIS PROFILE", COLOR_WHITE, COLOR_BLACK, 1);
        textbox(7, 83, 128, "CHANGE IN SETTINGS", COLOR_CYAN, COLOR_BLACK, 1);
        textbox(7, 110, 128, "CLICK TO CLOSE", COLOR_GREEN, COLOR_BLACK, 1);
        s_boot_warning_dirty = false;
        return;
    }
    ST7735_DrawHLine(s_lcd, 4, 22, 120, COLOR_ORANGE);
    ST7735_DrawHLine(s_lcd, 4, 145, 120, COLOR_ORANGE);
    ST7735_DrawVLine(s_lcd, 4, 22, 124, COLOR_ORANGE);
    ST7735_DrawVLine(s_lcd, 123, 22, 124, COLOR_ORANGE);
    textbox(10, 29, 108, "CHECK TIP", COLOR_ORANGE, COLOR_BLACK, 2);
    snprintf(b, sizeof(b), "TIP: %s", profile_active_name());
    textbox(10, 59, 108, b, COLOR_YELLOW, COLOR_BLACK, 1);
    textbox(10, 77, 108, "VERIFY IRON TIP", COLOR_WHITE, COLOR_BLACK, 1);
    textbox(10, 90, 108, "MATCHES PROFILE", COLOR_WHITE, COLOR_BLACK, 1);
    textbox(10, 111, 108, "CHANGE IN SETTINGS", COLOR_CYAN, COLOR_BLACK, 1);
    textbox(10, 134, 108, "CLICK TO CLOSE", COLOR_GREEN, COLOR_BLACK, 1);
    s_boot_warning_dirty = false;
}

static const char *fault_detail(safety_fault_t fault)
{
    switch (fault) {
    case FAULT_SENSOR_OPEN: return "CHECK IRON / TIP";
    case FAULT_SENSOR_SHORT: return "TEMP SIGNAL LOW";
    case FAULT_OVERTEMP: return "TEMP ABOVE LIMIT";
    case FAULT_OVERCURRENT: return "HEATER CURRENT HIGH";
    case FAULT_OFF_CURRENT: return "CURRENT WHILE OFF";
    case FAULT_NO_CURRENT: return "NO HEATER CURRENT";
    case FAULT_SUPPLY_RANGE: return "SUPPLY OUT OF RANGE";
    case FAULT_ADC_STALE: return "ADC DATA STOPPED";
    case FAULT_PULSE_TIMEOUT: return "HEATER PULSE TIMEOUT";
    case FAULT_UNCHARACTERIZED_PROFILE: return "PROFILE NOT ENABLED";
    case FAULT_SENSOR_UNSTABLE: return "PA1 SIGNAL COLLAPSED";
    default: return "UNKNOWN SAFETY EVENT";
    }
}

static void draw_fault_popup(void)
{
    char b[UI_TEXT_MAX + 1U];
    const fault_snapshot_t *snap = safety_snapshot();
    if (landscape()) {
        ST7735_FillScreen(s_lcd, COLOR_BLACK);
        ST7735_DrawHLine(s_lcd, 4, 15, 152, COLOR_RED);
        textbox(6, 3, 110, "FAULT", COLOR_RED, COLOR_BLACK, 1);
        textbox(6, 23, 128, safety_fault_text(s_popup_fault), COLOR_YELLOW, COLOR_BLACK, 1);
        textbox(6, 37, 128, fault_detail(s_popup_fault), COLOR_WHITE, COLOR_BLACK, 1);
        snprintf(b, sizeof(b), "V:%lu.%02lu T:%dC", (unsigned long)(snap->vin_mv / 1000UL),
                 (unsigned long)((snap->vin_mv / 10UL) % 100UL), snap->temperature_c);
        textbox(6, 60, 128, b, COLOR_CYAN, COLOR_BLACK, 1);
        snprintf(b, sizeof(b), "PA1:%u PA6:%u", snap->temp_raw, snap->current_raw);
        textbox(6, 75, 128, b, COLOR_CYAN, COLOR_BLACK, 1);
        if (s_popup_fault == FAULT_SUPPLY_RANGE) {
            snprintf(b, sizeof(b), "SAFE %u-%uV FOR TIP",
                     profile_supply_min_mv() / 1000U, profile_supply_max_mv() / 1000U);
            textbox(6, 90, 128, b, COLOR_WHITE, COLOR_BLACK, 1);
        }
        textbox(6, 111, 128, "CLICK TO CLOSE", COLOR_GREEN, COLOR_BLACK, 1);
        s_fault_popup_dirty = false;
        return;
    }
    ST7735_FillRect(s_lcd, 4, 22, 120, 124, COLOR_BLACK);
    ST7735_DrawHLine(s_lcd, 4, 22, 120, COLOR_RED);
    ST7735_DrawHLine(s_lcd, 4, 145, 120, COLOR_RED);
    ST7735_DrawVLine(s_lcd, 4, 22, 124, COLOR_RED);
    ST7735_DrawVLine(s_lcd, 123, 22, 124, COLOR_RED);
    textbox(10, 29, 108, "FAULT", COLOR_RED, COLOR_BLACK, 2);
    textbox(10, 49, 108, safety_fault_text(s_popup_fault), COLOR_YELLOW, COLOR_BLACK, 1);
    if (s_popup_fault == FAULT_SUPPLY_RANGE) {
        snprintf(b, sizeof(b), "SAFE %u-%uV FOR TIP",
                 profile_supply_min_mv() / 1000U, profile_supply_max_mv() / 1000U);
        textbox(10, 62, 108, b, COLOR_WHITE, COLOR_BLACK, 1);
    } else textbox(10, 62, 108, fault_detail(s_popup_fault),
                   COLOR_WHITE, COLOR_BLACK, 1);
    snprintf(b, sizeof(b), "VIN: %lu.%02lu V",
             (unsigned long)(snap->vin_mv / 1000UL),
             (unsigned long)((snap->vin_mv / 10UL) % 100UL));
    textbox(10, 80, 108, b, COLOR_CYAN, COLOR_BLACK, 1);
    if (s_popup_fault == FAULT_SENSOR_UNSTABLE)
        snprintf(b, sizeof(b), "FROM:%dC PA1:%u", snap->reference_temperature_c,
                 snap->reference_temp_raw);
    else
        snprintf(b, sizeof(b), "T:%dC PA1:%u", snap->temperature_c, snap->temp_raw);
    textbox(10, 93, 108, b, COLOR_CYAN, COLOR_BLACK, 1);
    if (s_popup_fault == FAULT_SENSOR_UNSTABLE)
        snprintf(b, sizeof(b), "NOW:%dC PA1:%u", snap->temperature_c, snap->temp_raw);
    else if (s_popup_fault == FAULT_OFF_CURRENT)
        snprintf(b, sizeof(b), "OFF:%u.%uA PA6:%u", snap->current_ma / 1000U,
                 (snap->current_ma / 100U) % 10U, snap->current_raw);
    else
        snprintf(b, sizeof(b), "CUR:%u.%uA DUT:%u.%u%%", snap->current_ma / 1000U,
                 (snap->current_ma / 100U) % 10U, snap->duty_permille / 10U,
                 snap->duty_permille % 10U);
    textbox(10, 106, 108, b, COLOR_CYAN, COLOR_BLACK, 1);
    snprintf(b, sizeof(b), "TIME:%lu.%01lus", (unsigned long)(snap->timestamp_ms / 1000UL),
             (unsigned long)((snap->timestamp_ms / 100UL) % 10UL));
    textbox(10, 119, 108, b, COLOR_GRAY, COLOR_BLACK, 1);
    textbox(10, 134, 108, "CLICK TO CLOSE", COLOR_GREEN, COLOR_BLACK, 1);
    s_fault_popup_dirty = false;
}

static void draw_about(void)
{
    char b[32];
    ST7735_FillScreen(s_lcd, COLOR_BLACK);
    if (landscape()) {
        textbox(6, 5, 110, "OpenStation", COLOR_CYAN, COLOR_BLACK, 1);
        ST7735_DrawHLine(s_lcd, 0, 18, 160, COLOR_CYAN);
        snprintf(b, sizeof(b), "Version %s Build %u",
                 STATION_FW_VERSION, STATION_FW_BUILD);
        textbox(6, 23, 145, b, COLOR_WHITE, COLOR_BLACK, 1);
        textbox(6, 36, 145, "Board hardware by", COLOR_WHITE, COLOR_BLACK, 1);
        textbox(6, 49, 145, "toanthodien16", COLOR_GREEN, COLOR_BLACK, 1);
        ST7735_DrawHLine(s_lcd, 0, 61, 160, COLOR_CYAN);
        textbox_advance(6, 66, 148, "Firmware brought to you by",
                        COLOR_WHITE, COLOR_BLACK, 1, 5U);
        textbox_advance(6, 79, 40, "dolce_st", COLOR_YELLOW, COLOR_BLACK, 1, 5U);
        textbox_advance(46, 79, 108, " with the help of",
                        COLOR_WHITE, COLOR_BLACK, 1, 5U);
        textbox_advance(6, 92, 148, "ChatGPT,github.com/imdolce",
                        COLOR_WHITE, COLOR_BLACK, 1, 5U);
        textbox(16, 112, 132, "Click or hold to exit",
                COLOR_YELLOW, COLOR_BLACK, 1);
        s_static_dirty = false;
        return;
    }
    textbox(6, 5, 118, "OpenStation", COLOR_CYAN, COLOR_BLACK, 1);
    ST7735_DrawHLine(s_lcd, 0, 18, 128, COLOR_CYAN);
    snprintf(b, sizeof(b), "Version %s", STATION_FW_VERSION);
    textbox(6, 24, 118, b, COLOR_WHITE, COLOR_BLACK, 1);
    snprintf(b, sizeof(b), "Build %u", STATION_FW_BUILD);
    textbox(6, 37, 118, b, COLOR_WHITE, COLOR_BLACK, 1);
    textbox(6, 52, 118, "Board hardware by", COLOR_WHITE, COLOR_BLACK, 1);
    textbox(6, 65, 118, "toanthodien16", COLOR_GREEN, COLOR_BLACK, 1);
    ST7735_DrawHLine(s_lcd, 0, 79, 128, COLOR_CYAN);
    textbox(6, 84, 118, "Firmware brought to", COLOR_WHITE, COLOR_BLACK, 1);
    textbox(6, 97, 42, "you by ", COLOR_WHITE, COLOR_BLACK, 1);
    textbox(48, 97, 48, "dolce_st", COLOR_YELLOW, COLOR_BLACK, 1);
    textbox(6, 110, 122, "with help of ChatGPT", COLOR_WHITE, COLOR_BLACK, 1);
    textbox(6, 123, 118, "github.com/imdolce", COLOR_WHITE, COLOR_BLACK, 1);
    textbox(6, 146, 118, "Click/hold to exit", COLOR_YELLOW, COLOR_BLACK, 1);
    s_static_dirty = false;
}

static void draw_calibration_warning(void)
{
    ST7735_FillScreen(s_lcd, COLOR_BLACK);
    if (landscape()) {
        textbox(5, 3, 125, "CALIBRATION WARNING", COLOR_RED, COLOR_BLACK, 1);
        ST7735_DrawHLine(s_lcd, 3, 15, 154, COLOR_RED);
        textbox(7, 23, 128, "TIP MAY GET HOTTER", COLOR_YELLOW, COLOR_BLACK, 1);
        textbox(7, 36, 128, "THAN REQUIRED TEMP", COLOR_YELLOW, COLOR_BLACK, 1);
        textbox(7, 52, 128, "THERMAL RUNAWAY", COLOR_RED, COLOR_BLACK, 1);
        textbox(7, 65, 128, "IS POSSIBLE", COLOR_RED, COLOR_BLACK, 1);
        textbox(7, 82, 128, "CUT POWER IF NEEDED", COLOR_WHITE, COLOR_BLACK, 1);
        textbox(7, 96, 128, "LONG: CUT AND EXIT", COLOR_WHITE, COLOR_BLACK, 1);
        textbox(7, 113, 128, s_calibration_error != NULL ? s_calibration_error :
                "CLICK TO PROCEED", s_calibration_error != NULL ? COLOR_RED : COLOR_GREEN,
                COLOR_BLACK, 1);
        s_calibration_warning_dirty = false;
        return;
    }
    ST7735_DrawHLine(s_lcd, 3, 16, 122, COLOR_RED);
    textbox(5, 5, 118, "CALIBRATION WARNING", COLOR_RED, COLOR_BLACK, 1);
    textbox(7, 29, 114, "TIP MAY GET HOTTER", COLOR_YELLOW, COLOR_BLACK, 1);
    textbox(7, 43, 114, "THAN REQUIRED TEMP", COLOR_YELLOW, COLOR_BLACK, 1);
    textbox(7, 61, 114, "THERMAL RUNAWAY", COLOR_RED, COLOR_BLACK, 1);
    textbox(7, 76, 114, "IS POSSIBLE", COLOR_RED, COLOR_BLACK, 1);
    textbox(7, 96, 114, "CUT POWER IF NEEDED", COLOR_WHITE, COLOR_BLACK, 1);
    textbox(7, 114, 114, "LONG: CUT AND EXIT", COLOR_WHITE, COLOR_BLACK, 1);
    textbox(7, 140, 114, s_calibration_error != NULL ? s_calibration_error :
            "CLICK TO PROCEED", s_calibration_error != NULL ? COLOR_RED : COLOR_GREEN,
            COLOR_BLACK, 1);
    s_calibration_warning_dirty = false;
}

static void render_profile_menu(void)
{
    char b[UI_TEXT_MAX + 1U];
    if (s_profile_view == PROFILE_VIEW_ROOT) {
        field(13, 8, 40, 118, profile_active_name(), COLOR_WHITE, 1);
    }
    if (s_profile_view == PROFILE_VIEW_NAME) {
        snprintf(b, sizeof(b), "TIP: %s", profile_get(s_profile_family)->name);
        field(0, 8, 29, 112, b, COLOR_CYAN, 1);
        for (uint8_t i = 0; i < PROFILE_CUSTOM_NAME_MAX; ++i)
            b[i] = s_name[i] == '\0' ? '_' : s_name[i];
        b[PROFILE_CUSTOM_NAME_MAX] = '\0';
        field(1, 8, 59, 112, b, COLOR_WHITE, 1);
        ST7735_FillRect(s_lcd, 8, 69, 72, 3, COLOR_BLACK);
        ST7735_FillRect(s_lcd, 8U + s_name_cursor * 6U, 69, 5, 2, COLOR_YELLOW);
        snprintf(b, sizeof(b), "LETTER %u/12", s_name_cursor + 1U);
        field(2, 8, 83, 112, b, COLOR_YELLOW, 1);
        field(3, 8, landscape() ? 97U : 103U, 112,
              "CLICK: NEXT / FINISH", COLOR_WHITE, 1);
        field(4, 8, landscape() ? 108U : 118U, 112,
              "DOUBLE: BACKSPACE", COLOR_WHITE, 1);
        field(5, 8, landscape() ? 119U : 139U, 112, s_name_error != NULL ? s_name_error :
              "LONG: EXIT", s_name_error != NULL ? COLOR_RED : COLOR_GRAY, 1);
        return;
    }
    uint8_t count = profile_view_count();
    uint8_t visible_rows = s_profile_view == PROFILE_VIEW_ROOT ? 5U : visible_menu_rows();
    for (uint8_t row = 0; row < visible_rows; ++row) {
        uint8_t index = (uint8_t)(s_settings_scroll + row);
        const char *label = NULL;
        b[0] = '\0';
        if (index < count) {
            switch (s_profile_view) {
            case PROFILE_VIEW_ROOT:
                if (index == 0U) snprintf(b, sizeof(b), "DEFAULT %s", profile_get(profile_active_index())->name);
                else {
                    static const char *const options[] = {
                        "", "CREATE NEW", "MANAGE PROFILES", "VIEW DETAILS", "BACK"
                    };
                    label = options[index];
                }
                break;
            case PROFILE_VIEW_CREATE_TIP: case PROFILE_VIEW_MANAGE_TIP:
                if (index < profile_count()) {
                    uint8_t family = index;
                    uint8_t used = 0U;
                    for (uint8_t slot = 0; slot < PROFILE_CUSTOM_PER_TIP; ++slot)
                        if (profile_custom_valid(family, slot)) ++used;
                    snprintf(b, sizeof(b), "%s  %u/10", profile_get(family)->name, used);
                } else label = "BACK";
                break;
            case PROFILE_VIEW_CREATE_FINISH:
                label = index == 0U ? "LOAD DEFAULT" :
                        (index == 1U ? "EDIT VALUES" : "BACK");
                break;
            case PROFILE_VIEW_MANAGE_LIST: {
                uint8_t slot = profile_list_slot(index);
                label = slot == 0xFFU ? "BACK" : profile_custom_name(s_profile_family, slot);
                break;
            }
            case PROFILE_VIEW_ACTION:
                label = index == 0U ? "RENAME" : (index == 1U ? "EDIT" :
                        (index == 2U ? "REMOVE" : "BACK"));
                break;
            case PROFILE_VIEW_REMOVE:
                label = index == 0U ? "YES, REMOVE" : "NO, BACK";
                break;
            case PROFILE_VIEW_CAL_VALUES:
                if (index < PROFILE_CAL_POINT_COUNT)
                    snprintf(b, sizeof(b), "%3dC  %3dC", s_calibration_anchors[index],
                             profile_active()->temp_cal_c[index]);
                else label = "BACK";
                break;
            default: break;
            }
        }
        if (label != NULL) snprintf(b, sizeof(b), "%s", label);
        char line[UI_TEXT_MAX + 1U];
        snprintf(line, sizeof(line), "%c %.20s", index == s_settings_selected ?
                 (s_settings_editing ? '*' : '>') : ' ', b);
        uint16_t menu_y = s_profile_view == PROFILE_VIEW_ROOT ? 62U : 24U;
        field(row, 1, (uint16_t)(menu_y + row * 14U), 126, line,
              index == s_settings_selected ?
              (s_settings_editing ? COLOR_ORANGE : COLOR_YELLOW) : COLOR_WHITE, 1);
    }
    uint8_t frame_row = (uint8_t)(s_settings_selected - s_settings_scroll);
    if (frame_row != s_menu_frame_row) {
        if (s_menu_frame_row < visible_rows) {
            uint16_t old = (uint16_t)((s_profile_view == PROFILE_VIEW_ROOT ? 60U : 22U) +
                                      s_menu_frame_row * 14U);
            ST7735_DrawHLine(s_lcd, 0, old, screen_width(), COLOR_BLACK);
            ST7735_DrawHLine(s_lcd, 0, old + 11U, screen_width(), COLOR_BLACK);
        }
        uint16_t y = (uint16_t)((s_profile_view == PROFILE_VIEW_ROOT ? 60U : 22U) +
                                frame_row * 14U);
        ST7735_DrawHLine(s_lcd, 0, y, screen_width(), COLOR_YELLOW);
        ST7735_DrawHLine(s_lcd, 0, y + 11U, screen_width(), COLOR_YELLOW);
        s_menu_frame_row = frame_row;
    }
}

void ui_render(void)
{
    char b[UI_TEXT_MAX + 1U];
    const measurement_t *m = measure_get();
    const power_status_t *pw = power_get();
    const control_status_t *c = control_get();
    const tip_profile_t *p = profile_active();

    bool screen_off = c->sleep_timed_out &&
                      control_sleep_type() == SLEEP_TYPE_SCREEN_OFF;
    if (screen_off) {
        if (!s_display_asleep) {
            ST7735_DisplayOff(s_lcd);
            s_display_asleep = true;
        }
        return;
    }
    if (s_display_asleep) {
        ST7735_DisplayOn(s_lcd);
        s_display_asleep = false;
        s_static_dirty = true;
    }

    if (c->heating_enabled) {
        if (!s_session_running) {
            s_session_start_ms = board_millis();
            s_session_running = true;
        }
    } else s_session_running = false;

    safety_fault_t fault = safety_fault();
    if (fault != FAULT_NONE && fault != s_popup_fault) {
        s_popup_fault = fault;
        s_fault_popup_visible = true;
        s_fault_popup_dirty = true;
    } else if (fault == FAULT_NONE) {
        s_popup_fault = FAULT_NONE;
    }
    if (s_fault_popup_visible) {
        if (s_fault_popup_dirty) draw_fault_popup();
        return;
    }
    if (s_boot_warning_visible) {
        if (s_boot_warning_dirty) draw_boot_tip_warning();
        return;
    }
    if (s_calibration_warning_visible) {
        if (s_calibration_warning_dirty) draw_calibration_warning();
        return;
    }
    if (s_about_visible) {
        if (s_static_dirty) draw_about();
        return;
    }
    if (s_static_dirty) draw_static();

    if (s_page != 0U && !s_calibration_active && s_settings_level != 0U &&
        s_settings_group == GROUP_PROFILES && s_profile_view != PROFILE_VIEW_DETAILS) {
        render_profile_menu();
        return;
    }

    if (s_page == 0U) {
        if (landscape()) {
            if (m->tip_present) {
                snprintf(b, sizeof(b), "%3d", m->temperature_c);
                field(0, 3, 33, 36, b,
                      m->temperature_compensated ? COLOR_ORANGE : COLOR_YELLOW, 2);
            } else field(0, 3, 33, 36, "---", COLOR_RED, 2);
            set_boost_indicator_update(c);
            snprintf(b, sizeof(b), "%3d",
                     c->state == STATE_BOOST ? c->active_setpoint_c : c->target_c);
            field(1, 3, 66, 36, b,
                  c->state == STATE_BOOST ? COLOR_RED : COLOR_GREEN, 2);
            for (uint8_t i = 0; i < 3U; ++i) {
                uint16_t preset = control_preset_c(i);
                bool disabled = preset > control_max_target_c() ||
                                preset > (uint16_t)p->maximum_temp_c;
                preset_row_update(i, preset,
                                  !disabled && control_selected_preset() == (int8_t)i,
                                  disabled);
            }
            main_status_update(c, m->tip_present);
            snprintf(b, sizeof(b), "Tip: %s - %s", p->name, profile_active_name());
            if (strlen(b) > 25U)
                snprintf(b, sizeof(b), "Tip:%s %s", p->name, profile_active_name());
            field(2, 3, 91, 154, b, COLOR_WHITE, 1);
            snprintf(b, sizeof(b), "%u%%", (pw->limited_permille + 5U) / 10U);
            field(3, 103, 77, 30, b, COLOR_WHITE, 1);
            duty_bar_update(pw->limited_permille);
            snprintf(b, sizeof(b), "V: %lu.%02lu V", (unsigned long)(m->vin_mv / 1000UL),
                     (unsigned long)((m->vin_mv / 10UL) % 100UL));
            field(4, 4, 103, 74, b, m->vin_valid ? COLOR_GREEN : COLOR_RED, 1);
            uint32_t remaining_s;
            uint8_t countdown_phase = sleep_countdown(c, &remaining_s);
            if (countdown_phase == 1U) {
                snprintf(b, sizeof(b), "SLEEP %lus", (unsigned long)remaining_s);
            } else if (countdown_phase == 2U) {
                snprintf(b, sizeof(b), "%02lu:%02lu:%02lu",
                         (unsigned long)(remaining_s / 3600UL),
                         (unsigned long)((remaining_s / 60UL) % 60UL),
                         (unsigned long)(remaining_s % 60UL));
            } else {
                uint32_t elapsed_s = s_session_running ?
                                     (uint32_t)(board_millis() - s_session_start_ms) / 1000UL : 0UL;
                if (elapsed_s > 359999UL) elapsed_s = 359999UL;
                snprintf(b, sizeof(b), "%02lu:%02lu:%02lu",
                         (unsigned long)(elapsed_s / 3600UL),
                         (unsigned long)((elapsed_s / 60UL) % 60UL),
                         (unsigned long)(elapsed_s % 60UL));
            }
            field(5, 91, 103, 60, b, COLOR_CYAN, 1);
            if (pw->pulse_ticks != 0U && !pw->cycle_measurement_valid)
                snprintf(b, sizeof(b), "I: -- A");
            else snprintf(b, sizeof(b), "I: %u.%02u A", pw->average_current_ma / 1000U,
                          (pw->average_current_ma / 10U) % 100U);
            field(6, 4, 116, 74, b, COLOR_WHITE, 1);
            if (pw->pulse_ticks != 0U && !pw->cycle_measurement_valid)
                snprintf(b, sizeof(b), "P: -- W");
            else snprintf(b, sizeof(b), "P: %lu.%02lu W",
                          (unsigned long)(pw->estimated_power_mw / 1000UL),
                          (unsigned long)((pw->estimated_power_mw / 10UL) % 100UL));
            field(7, 84, 116, 74, b, COLOR_WHITE, 1);
            return;
        }
        set_boost_indicator_update(c);
        snprintf(b, sizeof(b), "%3d",
                 c->state == STATE_BOOST ? c->active_setpoint_c : c->target_c);
        field(0, 2, 30, 54, b,
              c->state == STATE_BOOST ? COLOR_RED : COLOR_WHITE, 3);
        if (m->tip_present) {
            snprintf(b, sizeof(b), "%3d", m->temperature_c);
            field(1, 2, 70, 54, b,
                  m->temperature_compensated ? COLOR_ORANGE : COLOR_YELLOW, 3);
        } else {
            field(1, 2, 70, 54, "---", COLOR_RED, 3);
        }
        field(11, 77, 78, 8, m->temperature_compensated ? "+" : "",
              COLOR_ORANGE, 1);
        for (uint8_t i = 0; i < 3U; ++i) {
            uint16_t preset = control_preset_c(i);
            bool disabled = preset > control_max_target_c() ||
                            preset > (uint16_t)p->maximum_temp_c;
            preset_row_update(i, preset, !disabled && control_selected_preset() == (int8_t)i,
                              disabled);
        }
        main_status_update(c, m->tip_present);
        uint32_t remaining_s;
        if (sleep_countdown(c, &remaining_s) != 0U) {
            snprintf(b, sizeof(b), "%02lu:%02lu",
                     (unsigned long)(remaining_s / 60UL),
                     (unsigned long)(remaining_s % 60UL));
            field(12, 89, 91, 39, b, COLOR_CYAN, 1);
        } else field(12, 89, 91, 39, safety_ignore_faults() ? "DBG" : "",
                     COLOR_ORANGE, 1);
        snprintf(b, sizeof(b), "%3u%%", (pw->limited_permille + 5U) / 10U);
        field(6, 103, 101, 25, b, COLOR_WHITE, 1);
        duty_bar_update(pw->limited_permille);
        snprintf(b, sizeof(b), "Tip: %s", profile_active_name());
        field(7, 3, 114, 125, b, COLOR_WHITE, 1);
        snprintf(b, sizeof(b), "Voltage: %lu.%02lu V", (unsigned long)(m->vin_mv / 1000UL),
                 (unsigned long)((m->vin_mv / 10UL) % 100UL));
        field(8, 3, 125, 125, b, m->vin_valid ? COLOR_WHITE : COLOR_RED, 1);
        if (pw->pulse_ticks != 0U && !pw->cycle_measurement_valid)
            snprintf(b, sizeof(b), "Current: -- A");
        else snprintf(b, sizeof(b), "Current: %u.%02u A", pw->average_current_ma / 1000U,
                      (pw->average_current_ma / 10U) % 100U);
        field(9, 3, 136, 125, b, COLOR_WHITE, 1);
        if (pw->pulse_ticks != 0U && !pw->cycle_measurement_valid)
            snprintf(b, sizeof(b), "Power: -- W");
        else snprintf(b, sizeof(b), "Power: %lu.%02lu W",
                      (unsigned long)(pw->estimated_power_mw / 1000UL),
                      (unsigned long)((pw->estimated_power_mw / 10UL) % 100UL));
        field(10, 3, 147, 125, b, COLOR_WHITE, 1);
    } else if (s_calibration_active) {
        uint8_t point = s_calibration_point;
        int16_t difference = m->temperature_base_c - s_calibration_anchors[point];
        if (difference < 0) difference = -difference;
        if (m->tip_present && m->quiet_sample_valid && c->heating_enabled &&
            !c->stand_active && !safety_latched() && difference <= 25 &&
            m->temp_burst_span_raw <= 50U) {
            if (s_calibration_stable_since_ms == 0U)
                s_calibration_stable_since_ms = board_millis();
        } else s_calibration_stable_since_ms = 0U;
        const char *quality = m->temp_burst_span_raw <= 12U ? "GOOD" :
                              (m->temp_burst_span_raw <= 40U ? "FAIR" : "NOISY");
        uint16_t quality_color = m->temp_burst_span_raw <= 12U ? COLOR_GREEN :
                                 (m->temp_burst_span_raw <= 40U ? COLOR_YELLOW : COLOR_RED);
        if (s_calibration_stable_since_ms != 0U &&
            board_millis() - s_calibration_stable_since_ms >= 2000U) {
            quality = "READY";
            quality_color = COLOR_GREEN;
        }
        snprintf(b, sizeof(b), "POINT %dC  %u/6", s_calibration_anchors[point], point + 1U);
        field(0, 4, landscape() ? 21U : 24U, 120, b, COLOR_YELLOW, 1);
        snprintf(b, sizeof(b), "HEAT TO %4dC", c->target_c);
        field(1, 4, landscape() ? 35U : 42U, 120, b, COLOR_WHITE, 1);
        snprintf(b, sizeof(b), "BASE TEMP %4dC", m->temperature_base_c);
        field(2, 4, landscape() ? 49U : 56U, 120, b, COLOR_CYAN, 1);
        snprintf(b, sizeof(b), "EXTERNAL %4dC", s_calibration_values[point]);
        field(3, 4, landscape() ? 63U : 72U, 120, b, COLOR_ORANGE, 1);
        snprintf(b, sizeof(b), "STATION  %4dC", m->temperature_c);
        field(4, 4, landscape() ? 77U : 88U, 120, b, COLOR_WHITE, 1);
        snprintf(b, sizeof(b), "SPAN %3u  %s", m->temp_burst_span_raw, quality);
        field(5, 4, landscape() ? 91U : 104U, 120, b, quality_color, 1);
        field(6, 4, landscape() ? 105U : 120U, 120, s_calibration_error != NULL ? s_calibration_error :
              "TURN=THERMOMETER", s_calibration_error != NULL ? COLOR_RED : COLOR_GREEN, 1);
        field(7, 4, landscape() ? 119U : 139U, 120, "CLICK=NEXT LONG=OFF", COLOR_GRAY, 1);
    } else {
        const char marker = s_settings_editing ? '*' : '>';
        for (uint8_t row = 0; row < visible_menu_rows(); ++row) {
            uint8_t index = s_settings_scroll + row;
            char lead = index == s_settings_selected ? marker : ' ';
            if (s_settings_level == 0U) {
                if (index < SETTINGS_GROUP_COUNT)
                    snprintf(b, sizeof(b), "%c %s", lead, s_groups[index].name);
                else b[0] = '\0';
                field(row, 1, (uint16_t)(24U + row * 14U), 126, b,
                      index == s_settings_selected ? COLOR_YELLOW : COLOR_WHITE, 1);
                continue;
            }
            const settings_group_t *group = &s_groups[s_settings_group];
            if (index > group->count) {
                b[0] = '\0';
                field(row, 1, (uint16_t)(24U + row * 14U), 126, b, COLOR_WHITE, 1);
                continue;
            }
            if (index == group->count) {
                snprintf(b, sizeof(b), "%c BACK", lead);
                field(row, 1, (uint16_t)(24U + row * 14U), 126, b,
                      index == s_settings_selected ? COLOR_YELLOW : COLOR_WHITE, 1);
                continue;
            }
            uint8_t i = group->items[index];
            switch (i) {
            case SET_TIP_C115: case SET_TIP_C210: case SET_TIP_C245:
            case SET_TIP_C470: case SET_TIP_T12: case SET_TIP_TX1:
            case SET_TIP_TX2: case SET_TIP_GOOT: case SET_TIP_APOLLO: {
                uint8_t tip_index = (uint8_t)(i - SET_TIP_C115);
                snprintf(b, sizeof(b), "%c %-7s %s", lead,
                         profile_get(tip_index)->name,
                         tip_index == profile_active_index() ? "ACTIVE" : "");
                break;
            }
            case SET_PRESET_1: case SET_PRESET_2: case SET_PRESET_3:
                snprintf(b, sizeof(b), "%c PRESET %u  %3uC", lead,
                         i - SET_PRESET_1 + 1U, control_preset_c((uint8_t)(i - SET_PRESET_1)));
                break;
            case SET_TC_SCALE: snprintf(b, sizeof(b), "%c TC SCALE %5u", lead, p->temp_uv_per_c); break;
            case SET_TC_OFFSET: snprintf(b, sizeof(b), "%c TC OFF   %+4dC", lead, p->temp_offset_c); break;
            case SET_PROFILE_DEFAULT_TEMP: snprintf(b, sizeof(b), "%c DEFAULT  %3dC", lead,
                                                    p->default_temp_c); break;
            case SET_PROFILE_MAX_TEMP: snprintf(b, sizeof(b), "%c TIP MAX  %3dC", lead,
                                                p->maximum_temp_c); break;
            case SET_MAX_POWER: snprintf(b, sizeof(b), "%c MAX PWR  %3luW", lead,
                             (unsigned long)(p->max_power_mw / 1000UL)); break;
            case SET_KP: snprintf(b, sizeof(b), "%c KP       %2u.%u", lead,
                p->kp_q8 / 256U, ((p->kp_q8 % 256U) * 10U) / 256U); break;
            case SET_KI: snprintf(b, sizeof(b), "%c KI       %2u.%u", lead,
                p->ki_q8 / 256U, ((p->ki_q8 % 256U) * 10U) / 256U); break;
            case SET_KD: snprintf(b, sizeof(b), "%c KD       %2u.%u", lead,
                p->kd_q8 / 256U, ((p->kd_q8 % 256U) * 10U) / 256U); break;
            case SET_MAX_I: snprintf(b, sizeof(b), "%c MAX I    %4u", lead,
                                     p->max_integral_permille); break;
            case SET_CAL_FOLDER: snprintf(b, sizeof(b), "%c 6-POINT VALUES >", lead); break;
            case SET_SLEEP_TEMP: snprintf(b, sizeof(b), "%c SLEEP    %3dC", lead, p->sleep_temp_c); break;
            case SET_MAX_TEMP: snprintf(b, sizeof(b), "%c MAX TEMP %3uC", lead,
                                        control_max_target_c()); break;
            case SET_BOOST_TEMP:
                if (control_boost_delta_c() == 0U)
                    snprintf(b, sizeof(b), "%cBOOST TEMP OFF", lead);
                else snprintf(b, sizeof(b), "%cBOOST TEMP +%3uC", lead,
                              control_boost_delta_c());
                break;
            case SET_BOOST_TIMER:
                snprintf(b, sizeof(b), "%cBOOST TIMER %3us", lead,
                         control_boost_time_s());
                break;
            case SET_SLEEP_DELAY:
                if (control_sleep_delay_ms() == 0U)
                    snprintf(b, sizeof(b), "%cSLEEP DELAY INSTANT", lead);
                else snprintf(b, sizeof(b), "%cSLEEP DELAY %2lus", lead,
                              (unsigned long)(control_sleep_delay_ms() / 1000UL));
                break;
            case SET_SLEEP_TIME: snprintf(b, sizeof(b), "%cSLEEP TIME %4us", lead,
                                          control_sleep_time_s()); break;
            case SET_SLEEP_TYPE: {
                static const char *const names[] = {
                    "NO SLEEP", "SCREEN ON", "SCREEN DIM", "SCREEN OFF"
                };
                snprintf(b, sizeof(b), "%cSLEEP: %s", lead,
                         names[control_sleep_type()]);
                break;
            }
            case SET_ENCODER_STEP: snprintf(b, sizeof(b), "%cENCODER STEP %2uC", lead,
                             control_encoder_step_c()); break;
            case SET_ENCODER_DIR: snprintf(b, sizeof(b), "%cENCODER DIR %s", lead,
                             input_encoder_reversed() ? "REVERSE" : "NORMAL"); break;
            case SET_BACKLIGHT: snprintf(b, sizeof(b), "%c LIGHT     %3u%%", lead,
                             board_backlight_get()); break;
            case SET_ORIENTATION: {
                static const char *const names[] = {
                    "PORTRAIT", "PORTRAIT 180", "LANDSCAPE", "LANDSCAPE 180"
                };
                snprintf(b, sizeof(b), "%c ORIENT %s", lead, names[s_orientation]);
                break;
            }
            case SET_BUZZER: snprintf(b, sizeof(b), "%c BUZZER     %s", lead,
                                      board_buzzer_enabled() ? "ON" : "OFF"); break;
            case SET_READY_BEEP: snprintf(b, sizeof(b), "%c READY BEEP %s", lead,
                                          board_setpoint_beep_get() ? "ON" : "OFF"); break;
            case SET_CAL_WIZARD: snprintf(b, sizeof(b), "%c CAL WIZARD", lead); break;
            case SET_SAVE: snprintf(b, sizeof(b), "%c SAVE TO FLASH", lead); break;
            case SET_DEFAULTS: snprintf(b, sizeof(b), "%c FACTORY RESET", lead); break;
            case SET_DISCARD: snprintf(b, sizeof(b), "%c DISCARD CHANGES", lead); break;
            case SET_IGNORE_FAULTS: snprintf(b, sizeof(b), "%c IGNORE FLTS %s", lead,
                             safety_ignore_faults() ? "ON" : "OFF"); break;
            case SET_BOOT_TIP_WARNING: snprintf(b, sizeof(b), "%c BOOT TIP WARN %s", lead,
                             settings_tip_warning_enabled() ? "ON" : "OFF"); break;
            case SET_ABOUT: snprintf(b, sizeof(b), "%c ABOUT", lead); break;
            default: snprintf(b, sizeof(b), "%c EXIT", lead); break;
            }
            uint16_t color = index == s_settings_selected ?
                (s_settings_editing ? COLOR_ORANGE : COLOR_YELLOW) :
                (i >= SET_TIP_C115 && i <= SET_TIP_APOLLO &&
                 i - SET_TIP_C115 == profile_active_index() ? COLOR_GREEN : COLOR_WHITE);
            field(row, 1, (uint16_t)(24U + row * 14U), 126, b, color, 1);
        }
        uint8_t frame_row = (uint8_t)(s_settings_selected - s_settings_scroll);
        if (frame_row != s_menu_frame_row) {
            if (s_menu_frame_row < visible_menu_rows()) {
                uint16_t old_y = (uint16_t)(22U + s_menu_frame_row * 14U);
                ST7735_DrawHLine(s_lcd, 0, old_y, screen_width(), COLOR_BLACK);
                ST7735_DrawHLine(s_lcd, 0, old_y + 11U, screen_width(), COLOR_BLACK);
                ST7735_DrawVLine(s_lcd, 0, old_y, 12, COLOR_BLACK);
                ST7735_DrawVLine(s_lcd, screen_width() - 1U, old_y, 12, COLOR_BLACK);
            }
            uint16_t y = (uint16_t)(22U + frame_row * 14U);
            ST7735_DrawHLine(s_lcd, 0, y, screen_width(), COLOR_YELLOW);
            ST7735_DrawHLine(s_lcd, 0, y + 11U, screen_width(), COLOR_YELLOW);
            ST7735_DrawVLine(s_lcd, 0, y, 12, COLOR_YELLOW);
            ST7735_DrawVLine(s_lcd, screen_width() - 1U, y, 12, COLOR_YELLOW);
            s_menu_frame_row = frame_row;
        }
    }
}
