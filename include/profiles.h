#ifndef PROFILES_H
#define PROFILES_H

#include <stdbool.h>
#include <stdint.h>

#define PROFILE_CAL_POINT_COUNT 6U
#define PROFILE_CUSTOM_PER_TIP 10U
#define PROFILE_CUSTOM_NAME_MAX 12U

typedef struct {
    uint8_t id;
    char name[8];
    uint16_t revision;
    uint16_t resistance_min_mohm;
    uint16_t resistance_max_mohm;
    uint16_t nominal_resistance_mohm;
    uint16_t temp_uv_per_c;
    int16_t temp_offset_c;
    uint16_t kp_q8;
    uint16_t ki_q8;
    uint16_t kd_q8;
    uint16_t max_integral_permille;
    int16_t temp_cal_c[PROFILE_CAL_POINT_COUNT];
    uint32_t max_power_mw;
    uint16_t max_duty_permille;
    uint16_t rated_voltage_mv;
    uint16_t settle_us;
    uint16_t rise_timeout_ms;
    int16_t default_temp_c;
    int16_t sleep_temp_c;
    int16_t maximum_temp_c;
    uint8_t characterized;
} tip_profile_t;

uint8_t profile_count(void);
const tip_profile_t *profile_get(uint8_t index);
const tip_profile_t *profile_active(void);
uint8_t profile_active_index(void);
uint16_t profile_supply_min_mv(void);
uint16_t profile_supply_max_mv(void);
bool profile_supply_in_range(uint32_t vin_mv);
void profile_select(uint8_t index);
void profile_set_temp_uv_per_c(uint16_t value);
void profile_set_temp_offset_c(int16_t value);
void profile_set_max_power_mw(uint32_t value);
void profile_set_sleep_temp_c(int16_t value);
void profile_set_default_temp_c(int16_t value);
void profile_set_max_temp_c(int16_t value);
void profile_set_pid_q8(uint16_t kp, uint16_t ki, uint16_t kd);
void profile_set_max_integral_permille(uint16_t value);
void profile_set_calibration(uint8_t point, int16_t value);
bool profile_set_calibration_values(const int16_t values[PROFILE_CAL_POINT_COUNT]);
int16_t profile_calibrate_temperature(int16_t base_temperature_c);
void profiles_factory_reset(void);
void profiles_export(tip_profile_t *destination, uint8_t count);
void profiles_import(const tip_profile_t *source, uint8_t count, uint8_t active_index);
const char *profile_active_name(void);
uint8_t profile_active_selection(void);
bool profile_active_is_custom(void);
bool profile_custom_valid(uint8_t family, uint8_t slot);
const char *profile_custom_name(uint8_t family, uint8_t slot);
bool profile_custom_duplicate(const char *name, uint8_t except_family, uint8_t except_slot);
int8_t profile_custom_create(uint8_t family, const char *name);
bool profile_custom_select(uint8_t family, uint8_t slot);
bool profile_custom_rename(uint8_t family, uint8_t slot, const char *name);
bool profile_custom_remove(uint8_t family, uint8_t slot);
bool profile_custom_commit(void);
bool profiles_custom_clear_all(void);

#endif
