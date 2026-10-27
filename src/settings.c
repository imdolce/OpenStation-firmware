#include "settings.h"
#include "app_config.h"
#include "board.h"
#include "control.h"
#include "encoder.h"
#include "measure.h"
#include "power.h"
#include "profiles.h"
#include "safety.h"
#include "ch32x035.h"
#include "ui.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SETTINGS_MAGIC        0x534C4452UL
#define SETTINGS_VERSION      9U
#define SETTINGS_FLASH_ADDR   0x0800F400UL
#define SETTINGS_FLASH_BYTES  1024U
#define SETTINGS_PROFILE_CAP  9U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t active_profile;
    uint8_t encoder_step_c;
    uint8_t backlight_percent;
    uint8_t buzzer_enabled;
    uint8_t setpoint_beep;
    uint8_t reserved[3];
    uint32_t sleep_delay_ms;
    tip_profile_t profiles[SETTINGS_PROFILE_CAP];
    uint32_t checksum;
} settings_record_v2_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t active_profile;
    uint8_t encoder_step_c;
    uint8_t backlight_percent;
    uint8_t buzzer_enabled;
    uint8_t notification_flags;
    uint8_t encoder_reversed;
    uint8_t ignore_faults;
    uint8_t drop_compensation_enabled;
    uint32_t sleep_delay_ms;
    tip_profile_t profiles[SETTINGS_PROFILE_CAP];
    uint16_t presets_c[3];
    uint16_t max_target_c;
    uint32_t checksum;
} settings_record_v7_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t active_profile;
    uint8_t encoder_step_c;
    uint8_t backlight_percent;
    uint8_t buzzer_enabled;
    uint8_t notification_flags;
    uint8_t encoder_reversed;
    uint8_t ignore_faults;
    uint8_t drop_compensation_enabled;
    uint32_t sleep_delay_ms;
    tip_profile_t profiles[SETTINGS_PROFILE_CAP];
    uint16_t presets_c[3];
    uint16_t max_target_c;
    uint16_t sleep_time_s;
    uint8_t sleep_type;
    uint8_t reserved;
    uint32_t checksum;
} settings_record_v8_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t active_profile;
    uint8_t encoder_step_c;
    uint8_t backlight_percent;
    uint8_t buzzer_enabled;
    uint8_t notification_flags;
    uint8_t encoder_reversed;
    uint8_t ignore_faults;
    uint8_t drop_compensation_enabled;
    uint32_t sleep_delay_ms;
    tip_profile_t profiles[SETTINGS_PROFILE_CAP];
    uint16_t presets_c[3];
    uint16_t max_target_c;
    uint16_t sleep_time_s;
    uint8_t sleep_type;
    uint8_t reserved;
    uint8_t boost_delta_c;
    uint8_t boost_time_s;
    uint32_t checksum;
} settings_record_t;

_Static_assert(sizeof(settings_record_t) <= SETTINGS_FLASH_BYTES,
               "settings record exceeds reserved flash page");
_Static_assert(sizeof(settings_record_v7_t) == 608U,
               "legacy settings flash layout changed");
_Static_assert(sizeof(settings_record_v8_t) == 612U,
               "legacy settings flash layout changed");
_Static_assert(sizeof(settings_record_t) == 616U,
               "settings flash layout changed; add a migration before release");

static settings_record_t s_saved;
static bool s_loaded;
static bool s_tip_warning_enabled = true;

static uint32_t checksum_bytes(const void *record, size_t count)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < count; ++i) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

static uint32_t checksum_record(const settings_record_t *record)
{
    return checksum_bytes(record, offsetof(settings_record_t, checksum));
}

static void capture(settings_record_t *record)
{
    memset(record, 0, sizeof(*record));
    record->magic = SETTINGS_MAGIC;
    record->version = SETTINGS_VERSION;
    record->size = sizeof(*record);
    record->active_profile = profile_active_selection();
    record->encoder_step_c = control_encoder_step_c();
    record->backlight_percent = board_backlight_get();
    record->buzzer_enabled = board_buzzer_enabled();
    record->notification_flags = (board_setpoint_beep_get() ? 1U : 0U) |
                                 (s_tip_warning_enabled ? 2U : 0U) |
                                 ((ui_display_orientation() & 3U) << 2U);
    record->encoder_reversed = input_encoder_reversed();
    record->ignore_faults = safety_ignore_faults();
    /* Retain the flash layout, but retire the old +300 C workaround. */
    record->drop_compensation_enabled = 0U;
    record->max_target_c = control_max_target_c();
    record->sleep_delay_ms = control_sleep_delay_ms();
    record->sleep_time_s = control_sleep_time_s();
    record->sleep_type = (uint8_t)control_sleep_type();
    record->boost_delta_c = control_boost_delta_c();
    record->boost_time_s = control_boost_time_s();
    profiles_export(record->profiles, SETTINGS_PROFILE_CAP);
    for (uint8_t i = 0; i < 3U; ++i) record->presets_c[i] = control_preset_c(i);
    record->checksum = checksum_record(record);
}

static void apply(const settings_record_t *record)
{
    profiles_import(record->profiles, SETTINGS_PROFILE_CAP, record->active_profile);
    control_set_max_target_c(record->version >= 4U ? record->max_target_c : STATION_MAX_TARGET_C);
    control_set_encoder_step_c(record->encoder_step_c);
    input_set_encoder_reversed(record->version >= 4U && record->encoder_reversed != 0U);
    safety_set_ignore_faults(record->version >= 4U && record->ignore_faults != 0U);
    measure_set_drop_compensation_enabled(false);
    control_set_sleep_delay_ms(record->sleep_delay_ms);
    control_set_sleep_time_s(record->version >= 8U ? record->sleep_time_s : STATION_SLEEP_TIME_S);
    control_set_sleep_type(record->version >= 8U ?
                           (sleep_type_t)record->sleep_type : SLEEP_TYPE_SCREEN_ON);
    control_set_boost_delta_c(record->version >= 9U ? record->boost_delta_c : 50U);
    control_set_boost_time_s(record->version >= 9U ? record->boost_time_s : 30U);
    board_backlight_set(record->backlight_percent);
    board_buzzer_enable(record->buzzer_enabled != 0U);
    board_setpoint_beep_enable((record->notification_flags & 1U) != 0U);
    s_tip_warning_enabled = record->version >= 7U ?
                            (record->notification_flags & 2U) != 0U : true;
    ui_set_display_orientation(record->version >= 7U ?
                               (record->notification_flags >> 2U) & 3U : 0U);
    for (uint8_t i = 0; i < 3U; ++i) control_set_preset_c(i, record->presets_c[i]);
}

void settings_factory_defaults(void)
{
    profiles_factory_reset();
    control_set_max_target_c(STATION_MAX_TARGET_C);
    control_set_encoder_step_c(5U);
    input_set_encoder_reversed(false);
    safety_set_ignore_faults(false);
    measure_set_drop_compensation_enabled(false);
    control_set_sleep_delay_ms(STATION_SLEEP_DELAY_MS);
    control_set_sleep_time_s(STATION_SLEEP_TIME_S);
    control_set_sleep_type(SLEEP_TYPE_SCREEN_ON);
    control_set_boost_delta_c(50U);
    control_set_boost_time_s(30U);
    board_backlight_set(75U);
    board_buzzer_enable(true);
    board_setpoint_beep_enable(true);
    s_tip_warning_enabled = true;
    ui_set_display_orientation(0U);
    control_set_preset_c(0, 300U);
    control_set_preset_c(1, 350U);
    control_set_preset_c(2, 400U);
}

void settings_init(void)
{
    const settings_record_t *flash = (const settings_record_t *)SETTINGS_FLASH_ADDR;
    if (flash->magic == SETTINGS_MAGIC && flash->version == SETTINGS_VERSION &&
        flash->size == sizeof(settings_record_t) && flash->checksum == checksum_record(flash)) {
        memcpy(&s_saved, flash, sizeof(s_saved));
        apply(&s_saved);
        s_loaded = true;
        return;
    }
    /* Factory values are needed only for an empty/invalid page or to fill
     * fields that did not exist in an older saved record. */
    settings_factory_defaults();
    if (flash->magic == SETTINGS_MAGIC && flash->version == 8U &&
        flash->size == sizeof(settings_record_v8_t)) {
        const settings_record_v8_t *old = (const settings_record_v8_t *)flash;
        if (old->checksum == checksum_bytes(old, offsetof(settings_record_v8_t, checksum))) {
            settings_record_t migrated = {0};
            memcpy(&migrated, old, offsetof(settings_record_v8_t, checksum));
            apply(&migrated);
        }
        capture(&s_saved);
        s_loaded = false;
    } else if (flash->magic == SETTINGS_MAGIC &&
        (flash->version == 3U || flash->version == 4U ||
         flash->version == 5U || flash->version == 6U ||
         flash->version == 7U) &&
               flash->size == sizeof(settings_record_v7_t)) {
        const settings_record_v7_t *old = (const settings_record_v7_t *)flash;
        if (old->checksum == checksum_bytes(old, offsetof(settings_record_v7_t, checksum))) {
            settings_record_t migrated = {0};
            memcpy(&migrated, old, offsetof(settings_record_v7_t, checksum));
            apply(&migrated);
            /* The old +300 C option worked around the ADC arithmetic wrap.
             * Start the corrected firmware with that workaround disabled. */
            if (old->version == 5U) measure_set_drop_compensation_enabled(false);
            capture(&s_saved);
            s_loaded = false;
        } else {
            capture(&s_saved);
            s_loaded = false;
        }
    } else if (flash->magic == SETTINGS_MAGIC && flash->version == 2U &&
               flash->size == sizeof(settings_record_v2_t)) {
        const settings_record_v2_t *old = (const settings_record_v2_t *)flash;
        uint32_t old_hash = checksum_bytes(old, offsetof(settings_record_v2_t, checksum));
        if (old->checksum == old_hash) {
            /* Version 2 contains the same settings prefix and profile table.
             * Keep them, then add the three factory preset temperatures. */
            profiles_import(old->profiles, SETTINGS_PROFILE_CAP, old->active_profile);
            control_set_encoder_step_c(old->encoder_step_c);
            control_set_sleep_delay_ms(old->sleep_delay_ms);
            board_backlight_set(old->backlight_percent);
            board_buzzer_enable(old->buzzer_enabled != 0U);
            board_setpoint_beep_enable(old->setpoint_beep != 0U);
        }
        capture(&s_saved);
        s_loaded = false;
    } else {
        capture(&s_saved);
        s_loaded = false;
    }
}

bool settings_save(void)
{
    static uint32_t page[SETTINGS_FLASH_BYTES / sizeof(uint32_t)];
    settings_record_t current;
    if (!profile_custom_commit()) return false;
    capture(&current);
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &current, sizeof(current));
    power_force_off();
    __disable_irq();
    FLASH_Status erased = FLASH_ROM_ERASE(SETTINGS_FLASH_ADDR, SETTINGS_FLASH_BYTES);
    FLASH_Status written = erased == FLASH_COMPLETE ?
        FLASH_ROM_WRITE(SETTINGS_FLASH_ADDR, page, SETTINGS_FLASH_BYTES) : erased;
    __enable_irq();
    measure_requalify_quiet_sample();
    if (written != FLASH_COMPLETE) return false;
    const settings_record_t *verify = (const settings_record_t *)SETTINGS_FLASH_ADDR;
    if (verify->checksum != current.checksum || checksum_record(verify) != verify->checksum) return false;
    memcpy(&s_saved, &current, sizeof(s_saved));
    s_loaded = true;
    return true;
}

void settings_discard(void) { apply(&s_saved); }
bool settings_loaded_from_flash(void) { return s_loaded; }
bool settings_tip_warning_enabled(void) { return s_tip_warning_enabled; }
void settings_set_tip_warning_enabled(bool enabled) { s_tip_warning_enabled = enabled; }
