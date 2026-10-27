#ifndef MEASURE_H
#define MEASURE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t vin_raw, temp_raw, current_raw, off_current_raw;
    uint16_t vin_filtered_raw, temp_filtered_raw, current_zero_raw;
    uint16_t temp_burst_min_raw, temp_burst_max_raw, temp_burst_span_raw;
    uint16_t adc_pin_mv;
    uint32_t vin_mv;
    uint16_t current_ma, off_current_ma;
    int16_t temperature_c, temperature_base_c, temperature_uncorrected_c;
    uint32_t resistance_mohm;
    uint32_t sample_time_ms, sample_sequence;
    bool tip_present, vin_valid, quiet_sample_valid, current_sample_valid;
    bool off_current_sample_valid;
    bool current_sample_heater_on;
    bool temperature_compensated;
} measurement_t;

void measure_init(void);
void measure_process(void);
void measure_profile_changed(void);
void measure_requalify_quiet_sample(void);
bool measure_drop_compensation_enabled(void);
void measure_set_drop_compensation_enabled(bool enabled);
const measurement_t *measure_get(void);
bool measure_take_new_quiet_sample(void);
void measure_current_eoc_isr(uint16_t raw);
void measure_quiet_eoc_isr(uint16_t temp_raw, uint16_t vin_raw, uint16_t off_current_raw);
void measure_trigger_current_isr(void);
void measure_trigger_quiet_isr(void);

#endif
