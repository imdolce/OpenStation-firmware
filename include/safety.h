#ifndef SAFETY_H
#define SAFETY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FAULT_NONE = 0, FAULT_SENSOR_OPEN, FAULT_SENSOR_SHORT, FAULT_OVERTEMP,
    FAULT_OVERCURRENT, FAULT_OFF_CURRENT, FAULT_NO_CURRENT,
    FAULT_SUPPLY_RANGE, FAULT_ADC_STALE, FAULT_PULSE_TIMEOUT,
    FAULT_UNCHARACTERIZED_PROFILE, FAULT_SENSOR_UNSTABLE
} safety_fault_t;

typedef struct {
    safety_fault_t code;
    uint32_t timestamp_ms;
    uint16_t vin_raw, temp_raw, current_raw, duty_permille;
    uint32_t vin_mv;
    uint16_t current_ma;
    int16_t temperature_c;
    int16_t reference_temperature_c;
    uint16_t reference_temp_raw;
    uint8_t state;
} fault_snapshot_t;

void safety_init(void);
void safety_trip(safety_fault_t code, uint8_t state);
void safety_trip_isr(safety_fault_t code);
void safety_supervise(uint8_t state, bool heating_requested);
bool safety_acknowledge(void);
bool safety_ignore_faults(void);
void safety_set_ignore_faults(bool enabled);
bool safety_latched(void);
safety_fault_t safety_fault(void);
const char *safety_fault_text(safety_fault_t code);
const fault_snapshot_t *safety_snapshot(void);

#endif
