#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    STATE_BOOT_SAFE = 0, STATE_NO_TIP, STATE_TIP_IDENTIFY, STATE_IDLE,
    STATE_HEAT_UP, STATE_REGULATE, STATE_BOOST, STATE_SLEEP,
    STATE_COOLING, STATE_FAULT_LATCHED
} station_state_t;

typedef enum {
    SLEEP_TYPE_NONE = 0, SLEEP_TYPE_SCREEN_ON,
    SLEEP_TYPE_SCREEN_DIM, SLEEP_TYPE_SCREEN_OFF
} sleep_type_t;

typedef struct {
    station_state_t state;
    int16_t target_c, active_setpoint_c;
    int32_t integral_q8;
    uint16_t requested_permille;
    uint32_t state_since_ms, stand_since_ms, sleep_since_ms, inactivity_ms;
    uint16_t boost_seconds;
    bool stand_active, vib_active, heating_enabled, sleep_timed_out;
} control_status_t;

void control_init(void);
void control_reconfigure(void);
void control_run_cycle(void);
void control_tick_1ms(void);
void control_rotate(int8_t detents);
void control_change_profile(int8_t detents);
void control_select_profile(uint8_t index);
bool control_select_custom_profile(uint8_t family, uint8_t slot);
void control_short_press(void);
void control_long_press(void);
void control_double_press(void);
void control_set_target_c(int16_t target_c);
uint16_t control_preset_c(uint8_t index);
int8_t control_selected_preset(void);
void control_set_preset_c(uint8_t index, uint16_t temperature_c);
uint8_t control_encoder_step_c(void);
void control_set_encoder_step_c(uint8_t value);
uint16_t control_max_target_c(void);
void control_set_max_target_c(uint16_t value);
uint32_t control_sleep_delay_ms(void);
void control_set_sleep_delay_ms(uint32_t value);
uint16_t control_sleep_time_s(void);
void control_set_sleep_time_s(uint16_t value);
sleep_type_t control_sleep_type(void);
void control_set_sleep_type(sleep_type_t value);
uint8_t control_boost_delta_c(void);
void control_set_boost_delta_c(uint8_t value);
uint8_t control_boost_time_s(void);
void control_set_boost_time_s(uint8_t value);
const control_status_t *control_get(void);
const char *control_state_text(station_state_t state);

#endif
