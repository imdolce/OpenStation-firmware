#include "profiles.h"
#include "board.h"
#include "power.h"
#include "ch32x035.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define CUSTOM_FLASH_BASE 0x0800D000UL
#define CUSTOM_PAGE_BYTES 1024U
#define CUSTOM_SLOT_BYTES 96U
#define CUSTOM_MAGIC 0x43505246UL
#define CUSTOM_NONE 0xFFU

typedef struct {
    uint32_t magic;
    uint8_t family;
    char name[PROFILE_CUSTOM_NAME_MAX + 1U];
    uint8_t reserved[2];
    tip_profile_t profile;
    uint32_t checksum;
    uint8_t unused[CUSTOM_SLOT_BYTES - 88U];
} custom_record_t;

_Static_assert(sizeof(tip_profile_t) == 64U, "update custom flash layout for profile size");
_Static_assert(sizeof(custom_record_t) == CUSTOM_SLOT_BYTES, "custom flash slot must be 96 bytes");
_Static_assert(PROFILE_CUSTOM_PER_TIP * CUSTOM_SLOT_BYTES <= CUSTOM_PAGE_BYTES,
               "custom profiles exceed one tip page");

/* Electrical and thermal values are deliberately conservative placeholders.
 * characterized=0 prevents a release build from claiming bench validation. */
#define P(ID, NAME, RMIN, RMAX, RNOM, UVPC, KP, KI, KD, PWR, DUTY, VRATED, TMAX) \
    {ID, NAME, 2, RMIN, RMAX, RNOM, UVPC, 25, KP, KI, KD, 800, \
     {100, 200, 300, 350, 400, 450}, PWR, DUTY, VRATED, \
     5000, 15000, 300, 150, TMAX, 0}

static const tip_profile_t s_factory_profiles[] = {
    P(1, "C115", 1200, 2400, 1800, 8200, 900, 560, 0, 400000, 600, 12000, 500),
    P(2, "C210", 1500, 3000, 2200, 8200, 1000, 640, 0, 400000, 650, 16000, 500),
    /* Effective C245 slope measured on this board: a 220 C indicated reading
     * corresponded to 320 C externally, with both referenced to 25 C. */
    P(3, "C245", 2000, 4000, 2500, 3500, 2048, 512, 128, 400000, 1000, 24000, 500),
    P(4, "C470", 4000, 10000, 8000, 8200, 1400, 900, 0, 400000, 700, 24000, 500),
    P(5, "T12", 6000, 10000, 8000, 8000, 1150, 720, 0, 400000, 750, 24000, 500),
    P(6, "TX1", 2500, 9000, 5000, 8000, 1100, 700, 0, 400000, 700, 24000, 500),
    P(7, "TX2", 2500, 9000, 5000, 8000, 1100, 700, 0, 400000, 700, 24000, 500),
    P(8, "GOOT", 2000, 10000, 5000, 8000, 1100, 700, 0, 400000, 700, 24000, 500),
    P(9, "APOLLO", 2000, 10000, 5000, 8000, 1200, 760, 0, 400000, 700, 24000, 500)
};

static tip_profile_t s_profiles[sizeof(s_factory_profiles) / sizeof(s_factory_profiles[0])];
static tip_profile_t s_custom_active;
static char s_custom_name[PROFILE_CUSTOM_NAME_MAX + 1U];
static uint8_t s_active = 2;
static uint8_t s_custom_slot = CUSTOM_NONE;
static bool s_initialized;
static uint32_t s_page_buffer[CUSTOM_PAGE_BYTES / sizeof(uint32_t)];

static uint32_t custom_hash(const custom_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < offsetof(custom_record_t, checksum); ++i) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

static const custom_record_t *custom_flash(uint8_t family, uint8_t slot)
{
    return (const custom_record_t *)(CUSTOM_FLASH_BASE +
           (uint32_t)family * CUSTOM_PAGE_BYTES +
           (uint32_t)slot * CUSTOM_SLOT_BYTES);
}

static bool custom_write(uint8_t family, uint8_t slot, const custom_record_t *record)
{
    if (family >= profile_count() || slot >= PROFILE_CUSTOM_PER_TIP) return false;
    uint32_t address = CUSTOM_FLASH_BASE + (uint32_t)family * CUSTOM_PAGE_BYTES;
    memcpy(s_page_buffer, (const void *)address, CUSTOM_PAGE_BYTES);
    memcpy((uint8_t *)s_page_buffer + (uint32_t)slot * CUSTOM_SLOT_BYTES,
           record, CUSTOM_SLOT_BYTES);
    power_force_off();
    __disable_irq();
    FLASH_Status erased = FLASH_ROM_ERASE(address, CUSTOM_PAGE_BYTES);
    FLASH_Status written = erased == FLASH_COMPLETE ?
        FLASH_ROM_WRITE(address, s_page_buffer, CUSTOM_PAGE_BYTES) : erased;
    __enable_irq();
    if (written != FLASH_COMPLETE) return false;
    return memcmp((const void *)address, s_page_buffer, CUSTOM_PAGE_BYTES) == 0;
}

static void ensure_initialized(void)
{
    if (s_initialized) return;
    memcpy(s_profiles, s_factory_profiles, sizeof(s_profiles));
    s_initialized = true;
}

uint8_t profile_count(void) { return (uint8_t)(sizeof(s_profiles) / sizeof(s_profiles[0])); }
const tip_profile_t *profile_get(uint8_t i) { ensure_initialized(); return &s_profiles[i < profile_count() ? i : 0]; }
const tip_profile_t *profile_active(void)
{
    ensure_initialized();
    return s_custom_slot == CUSTOM_NONE ? &s_profiles[s_active] : &s_custom_active;
}
static tip_profile_t *editable(void)
{
    ensure_initialized();
    return s_custom_slot == CUSTOM_NONE ? &s_profiles[s_active] : &s_custom_active;
}
uint8_t profile_active_index(void) { return s_active; }
uint16_t profile_supply_min_mv(void) { return s_active <= 1U ? 9000U : 12000U; }
uint16_t profile_supply_max_mv(void) { return s_active <= 1U ? 15000U : 28000U; }
bool profile_supply_in_range(uint32_t vin_mv)
{
    return vin_mv >= profile_supply_min_mv() && vin_mv <= profile_supply_max_mv();
}
void profile_select(uint8_t i)
{
    ensure_initialized();
    if (i < profile_count()) { s_active = i; s_custom_slot = CUSTOM_NONE; }
}
const char *profile_active_name(void)
{
    return s_custom_slot == CUSTOM_NONE ? profile_active()->name : s_custom_name;
}
uint8_t profile_active_selection(void)
{
    return s_custom_slot == CUSTOM_NONE ? s_active :
           (uint8_t)(profile_count() + s_active * PROFILE_CUSTOM_PER_TIP + s_custom_slot);
}
bool profile_active_is_custom(void) { return s_custom_slot != CUSTOM_NONE; }
void profile_set_temp_uv_per_c(uint16_t v)
{
    ensure_initialized();
    if (v < 1000U) v = 1000U;
    if (v > 12000U) v = 12000U;
    editable()->temp_uv_per_c = v;
}
void profile_set_temp_offset_c(int16_t v)
{
    ensure_initialized();
    if (v < -100) v = -100;
    if (v > 100) v = 100;
    editable()->temp_offset_c = v;
}
void profile_set_max_power_mw(uint32_t v)
{
    ensure_initialized();
    if (v < 5000UL) v = 5000UL;
    if (v > 400000UL) v = 400000UL;
    editable()->max_power_mw = v;
}
void profile_set_sleep_temp_c(int16_t v)
{
    ensure_initialized();
    if (v < 100) v = 100;
    if (v > 250) v = 250;
    editable()->sleep_temp_c = v;
}
void profile_set_default_temp_c(int16_t v)
{
    if (v < 150) v = 150;
    if (v > editable()->maximum_temp_c) v = editable()->maximum_temp_c;
    editable()->default_temp_c = v;
}
void profile_set_max_temp_c(int16_t v)
{
    if (v < 150) v = 150;
    if (v > 500) v = 500;
    tip_profile_t *p = editable();
    p->maximum_temp_c = v;
    if (p->default_temp_c > v) p->default_temp_c = v;
}

void profile_set_pid_q8(uint16_t kp, uint16_t ki, uint16_t kd)
{
    ensure_initialized();
    tip_profile_t *p = editable();
    p->kp_q8 = kp > 5120U ? 5120U : kp;
    p->ki_q8 = ki > 2560U ? 2560U : ki;
    p->kd_q8 = kd > 5120U ? 5120U : kd;
}

void profile_set_max_integral_permille(uint16_t value)
{
    ensure_initialized();
    if (value > 1000U) value = 1000U;
    editable()->max_integral_permille = value;
}

void profile_set_calibration(uint8_t point, int16_t value)
{
    ensure_initialized();
    if (point >= PROFILE_CAL_POINT_COUNT) return;
    tip_profile_t *p = editable();
    int16_t minimum = point == 0U ? 0 : (int16_t)(p->temp_cal_c[point - 1U] + 1);
    int16_t maximum = point + 1U == PROFILE_CAL_POINT_COUNT ? 550 :
                      (int16_t)(p->temp_cal_c[point + 1U] - 1);
    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    p->temp_cal_c[point] = value;
}

bool profile_set_calibration_values(const int16_t values[PROFILE_CAL_POINT_COUNT])
{
    ensure_initialized();
    for (uint8_t i = 0; i < PROFILE_CAL_POINT_COUNT; ++i) {
        if (values[i] < 0 || values[i] > 550 ||
            (i != 0U && values[i] <= values[i - 1U])) return false;
    }
    memcpy(editable()->temp_cal_c, values, sizeof(editable()->temp_cal_c));
    return true;
}

int16_t profile_calibrate_temperature(int16_t base)
{
    static const int16_t anchor[PROFILE_CAL_POINT_COUNT] = {100, 200, 300, 350, 400, 450};
    const tip_profile_t *p = profile_active();
    if (base <= anchor[0]) return (int16_t)(((int32_t)base * p->temp_cal_c[0]) / anchor[0]);
    for (uint8_t i = 1; i < PROFILE_CAL_POINT_COUNT; ++i) {
        if (base <= anchor[i]) {
            int32_t numerator = (int32_t)(base - anchor[i - 1U]) *
                                (p->temp_cal_c[i] - p->temp_cal_c[i - 1U]);
            return (int16_t)(p->temp_cal_c[i - 1U] + numerator /
                             (anchor[i] - anchor[i - 1U]));
        }
    }
    return (int16_t)(p->temp_cal_c[5] +
        ((int32_t)(base - anchor[5]) * (p->temp_cal_c[5] - p->temp_cal_c[4])) / 50);
}

void profiles_factory_reset(void)
{
    memcpy(s_profiles, s_factory_profiles, sizeof(s_profiles));
    s_active = 2U;
    s_custom_slot = CUSTOM_NONE;
    s_initialized = true;
}

void profiles_export(tip_profile_t *destination, uint8_t count)
{
    ensure_initialized();
    if (count > profile_count()) count = profile_count();
    memcpy(destination, s_profiles, (size_t)count * sizeof(tip_profile_t));
}

void profiles_import(const tip_profile_t *source, uint8_t count, uint8_t active_index)
{
    if (count != profile_count()) return;
    memcpy(s_profiles, source, sizeof(s_profiles));
    s_initialized = true;
    s_custom_slot = CUSTOM_NONE;
    if (active_index < profile_count()) s_active = active_index;
    else {
        uint8_t index = (uint8_t)(active_index - profile_count());
        uint8_t family = (uint8_t)(index / PROFILE_CUSTOM_PER_TIP);
        uint8_t slot = (uint8_t)(index % PROFILE_CUSTOM_PER_TIP);
        if (!profile_custom_select(family, slot)) s_active = 2U;
    }
}

bool profile_custom_valid(uint8_t family, uint8_t slot)
{
    if (family >= profile_count() || slot >= PROFILE_CUSTOM_PER_TIP) return false;
    const custom_record_t *r = custom_flash(family, slot);
    return r->magic == CUSTOM_MAGIC && r->family == family &&
           memchr(r->name, '\0', sizeof(r->name)) != NULL &&
           r->checksum == custom_hash(r);
}

const char *profile_custom_name(uint8_t family, uint8_t slot)
{
    return profile_custom_valid(family, slot) ? custom_flash(family, slot)->name : NULL;
}

bool profile_custom_duplicate(const char *name, uint8_t except_family, uint8_t except_slot)
{
    for (uint8_t family = 0; family < profile_count(); ++family)
        if (strcmp(name, profile_get(family)->name) == 0) return true;
    for (uint8_t family = 0; family < profile_count(); ++family) {
        for (uint8_t slot = 0; slot < PROFILE_CUSTOM_PER_TIP; ++slot) {
            if (family == except_family && slot == except_slot) continue;
            const char *other = profile_custom_name(family, slot);
            if (other != NULL && strcmp(name, other) == 0) return true;
        }
    }
    return false;
}

static bool name_legal(const char *name)
{
    size_t count = 0;
    while (count <= PROFILE_CUSTOM_NAME_MAX && name[count] != '\0') {
        char c = name[count++];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' '))
            return false;
    }
    return count > 0U && count <= PROFILE_CUSTOM_NAME_MAX;
}

static void make_record(custom_record_t *record, uint8_t family, const char *name,
                        const tip_profile_t *profile)
{
    memset(record, 0, sizeof(*record));
    record->magic = CUSTOM_MAGIC;
    record->family = family;
    strncpy(record->name, name, PROFILE_CUSTOM_NAME_MAX);
    record->profile = *profile;
    record->checksum = custom_hash(record);
}

int8_t profile_custom_create(uint8_t family, const char *name)
{
    if (family >= profile_count() || !name_legal(name) ||
        profile_custom_duplicate(name, 0xFFU, 0xFFU)) return -1;
    for (uint8_t slot = 0; slot < PROFILE_CUSTOM_PER_TIP; ++slot) {
        if (profile_custom_valid(family, slot)) continue;
        custom_record_t record;
        make_record(&record, family, name, profile_get(family));
        return custom_write(family, slot, &record) ? (int8_t)slot : -1;
    }
    return -1;
}

bool profile_custom_select(uint8_t family, uint8_t slot)
{
    if (!profile_custom_valid(family, slot)) return false;
    const custom_record_t *r = custom_flash(family, slot);
    s_custom_active = r->profile;
    memcpy(s_custom_name, r->name, sizeof(s_custom_name));
    s_active = family;
    s_custom_slot = slot;
    return true;
}

bool profile_custom_rename(uint8_t family, uint8_t slot, const char *name)
{
    if (!profile_custom_valid(family, slot) || !name_legal(name) ||
        profile_custom_duplicate(name, family, slot)) return false;
    custom_record_t record = *custom_flash(family, slot);
    memset(record.name, 0, sizeof(record.name));
    strncpy(record.name, name, PROFILE_CUSTOM_NAME_MAX);
    record.checksum = custom_hash(&record);
    if (!custom_write(family, slot, &record)) return false;
    if (family == s_active && slot == s_custom_slot)
        memcpy(s_custom_name, record.name, sizeof(s_custom_name));
    return true;
}

bool profile_custom_remove(uint8_t family, uint8_t slot)
{
    if (!profile_custom_valid(family, slot)) return false;
    custom_record_t empty;
    memset(&empty, 0xFF, sizeof(empty));
    if (!custom_write(family, slot, &empty)) return false;
    if (family == s_active && slot == s_custom_slot) profile_select(family);
    return true;
}

bool profile_custom_commit(void)
{
    if (s_custom_slot == CUSTOM_NONE) return true;
    if (!profile_custom_valid(s_active, s_custom_slot)) return false;
    const custom_record_t *saved = custom_flash(s_active, s_custom_slot);
    if (memcmp(&saved->profile, &s_custom_active, sizeof(s_custom_active)) == 0 &&
        strcmp(saved->name, s_custom_name) == 0) return true;
    custom_record_t record;
    make_record(&record, s_active, s_custom_name, &s_custom_active);
    return custom_write(s_active, s_custom_slot, &record);
}

bool profiles_custom_clear_all(void)
{
    power_force_off();
    for (uint8_t family = 0; family < profile_count(); ++family) {
        uint32_t address = CUSTOM_FLASH_BASE + (uint32_t)family * CUSTOM_PAGE_BYTES;
        __disable_irq();
        FLASH_Status result = FLASH_ROM_ERASE(address, CUSTOM_PAGE_BYTES);
        __enable_irq();
        if (result != FLASH_COMPLETE) return false;
        board_watchdog_feed();
    }
    s_custom_slot = CUSTOM_NONE;
    return true;
}
