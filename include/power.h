#ifndef POWER_H
#define POWER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t requested_permille, limited_permille, pulse_ticks, pulse_us;
    uint16_t average_current_ma;
    uint32_t estimated_power_mw;
    bool output_armed, pulse_active, cycle_measurement_valid;
    uint32_t cycle_count;
} power_status_t;

void power_init(void);
void power_request(uint16_t duty_permille);
void power_record_cycle_measurement(uint16_t on_current_ma, uint32_t vin_mv,
                                    uint16_t on_ticks);
void power_emergency_stop_isr(void);
void power_force_off(void);
const power_status_t *power_get(void);
bool power_take_cycle_event(void);

#endif
