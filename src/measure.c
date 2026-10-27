#include "measure.h"
#include "app_config.h"
#include "board.h"
#include "control.h"
#include "power.h"
#include "profiles.h"
#include "safety.h"
#include "ch32x035.h"

static measurement_t s_data;
static volatile uint16_t s_isr_current;
static volatile uint16_t s_isr_current_pulse_ticks;
static volatile bool s_isr_current_pending;
static volatile uint16_t s_isr_cycle_current;
static volatile uint16_t s_isr_cycle_pulse_ticks;
static volatile bool s_isr_cycle_current_valid;
static volatile uint16_t s_isr_temp;
static volatile uint16_t s_isr_vin;
static volatile uint16_t s_isr_off_current;
static volatile uint16_t s_isr_temp_min;
static volatile uint16_t s_isr_temp_max;
static volatile uint32_t s_isr_quiet_sequence;
typedef enum {
    ADC_PHASE_IDLE = 0,
    ADC_PHASE_CURRENT,
    ADC_PHASE_TEMP_DISCARD,
    ADC_PHASE_TEMP_COLLECT,
    ADC_PHASE_VIN_DISCARD,
    ADC_PHASE_VIN,
    ADC_PHASE_OFF_CURRENT_DISCARD,
    ADC_PHASE_OFF_CURRENT
} adc_phase_t;
static volatile adc_phase_t s_adc_phase;
static volatile uint16_t s_quiet_temp_staging;
static uint8_t s_temp_burst_count;
static uint32_t s_temp_burst_sum;
static uint16_t s_temp_burst_min;
static uint16_t s_temp_burst_max;
static uint32_t s_processed_sequence;
static bool s_new_quiet;
static uint16_t s_temp_history[3];
static uint16_t s_vin_history[3];
static uint8_t s_history_index;
static bool s_history_primed;
static bool s_drop_compensation_enabled;
static bool s_drop_active;
static int16_t s_drop_peak_c;
static int16_t s_drop_reference_c;
static uint32_t s_drop_peak_ms;

#define TEMP_DROP_COMPENSATION_C 300
#define TEMP_DROP_WINDOW_MS 400UL
#define TEMP_DROP_MIN_C 230
#define TEMP_DROP_MAX_C 370

void ADC1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

static uint16_t median3(uint16_t a, uint16_t b, uint16_t c)
{
    if (a > b) { uint16_t t = a; a = b; b = t; }
    if (b > c) { uint16_t t = b; b = c; c = t; }
    if (a > b) b = a;
    return b;
}

static uint32_t adc_raw_to_uv(uint16_t raw)
{
    /* 4095 * 3,300,000 exceeds uint32_t. Promote before multiplying so
     * the temperature conversion cannot wrap near ADC count 1302. */
    return (uint32_t)(((uint64_t)raw * STATION_ADC_VREF_MV * 1000ULL) /
                      STATION_ADC_FULL_SCALE);
}

static uint16_t current_raw_to_ma(uint16_t raw)
{
    if (raw <= s_data.current_zero_raw) return 0;
    uint32_t delta_uv = adc_raw_to_uv((uint16_t)(raw - s_data.current_zero_raw));
    /* Convert using the U2.1/shunt transfer coefficient from app_config.h. */
    uint32_t current_ma = delta_uv / CURRENT_SENSE_UV_PER_MA;
    return current_ma > 65535UL ? 65535U : (uint16_t)current_ma;
}

static int16_t temperature_base_from_raw(uint16_t raw, const tip_profile_t *p)
{
    uint32_t temp_uv = adc_raw_to_uv(raw);
    int32_t temp = p->temp_uv_per_c ?
        (int32_t)(temp_uv / p->temp_uv_per_c) + p->temp_offset_c : 0;
    if (temp < -20) temp = -20;
    if (temp > 550) temp = 550;
    return (int16_t)temp;
}

void measure_init(void)
{
    ADC_InitTypeDef adc = {0};
    NVIC_InitTypeDef nvic = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    ADC_DeInit(ADC1);
    ADC_CLKConfig(ADC1, ADC_CLK_Div8);

    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;
    adc.ADC_OutputBuffer = 0;
    adc.ADC_Pga = 0;
    ADC_Init(ADC1, &adc);

    ADC_RegularChannelConfig(ADC1, ADC_Channel_6, 1, ADC_SampleTime_11Cycles);
    ADC_AnalogWatchdogCmd(ADC1, ADC_AnalogWatchdog_None);
    ADC_ITConfig(ADC1, ADC_IT_EOC, ENABLE);

    nvic.NVIC_IRQChannel = ADC1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    ADC_Cmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC1, DISABLE);
    s_adc_phase = ADC_PHASE_IDLE;
    s_data.current_zero_raw = 16;
    s_data.sample_time_ms = board_millis();
}

void measure_trigger_current_isr(void)
{
    /* CC1 fires every cycle. A reading here is meaningful only if the
     * heater pulse is actually active. Off-current is sampled separately. */
    if (s_adc_phase != ADC_PHASE_IDLE || !power_get()->pulse_active) return;
    ADC_RegularChannelConfig(ADC1, ADC_Channel_6, 1, ADC_SampleTime_11Cycles);
    s_adc_phase = ADC_PHASE_CURRENT;
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

void measure_trigger_quiet_isr(void)
{
    if (s_adc_phase != ADC_PHASE_IDLE) return;
    /* A shared heater/thermocouple contact is meaningful only with the
     * high-side switch off. A missed shutoff yields a stale-sample fault. */
    if (power_get()->pulse_active) return;
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 1, ADC_SampleTime_11Cycles);
    s_temp_burst_count = 0;
    s_temp_burst_sum = 0;
    s_temp_burst_min = UINT16_MAX;
    s_temp_burst_max = 0;
    s_adc_phase = ADC_PHASE_TEMP_DISCARD;
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

void measure_profile_changed(void)
{
    measure_requalify_quiet_sample();
    s_history_primed = false;
    s_data.temperature_c = 0;
    s_drop_active = false;
    s_drop_peak_ms = 0;
    s_data.temperature_compensated = false;
}

void measure_requalify_quiet_sample(void)
{
    /* A profile change or flash write invalidates the old conversion, not
     * the ADC itself. Keep the heater off until a new quiet sample arrives. */
    power_force_off();
    s_data.quiet_sample_valid = false;
    s_data.sample_time_ms = board_millis();
    s_new_quiet = false;
}

bool measure_drop_compensation_enabled(void) { return s_drop_compensation_enabled; }
void measure_set_drop_compensation_enabled(bool enabled)
{
    s_drop_compensation_enabled = enabled;
    s_drop_active = false;
    s_drop_peak_ms = 0;
    s_data.temperature_compensated = false;
}

void measure_current_eoc_isr(uint16_t raw)
{
    s_isr_current = raw;
    s_isr_current_pulse_ticks = power_get()->pulse_ticks;
    s_isr_current_pending = power_get()->pulse_active;
}

void measure_quiet_eoc_isr(uint16_t temp_raw, uint16_t vin_raw, uint16_t off_current_raw)
{
    s_isr_temp = temp_raw;
    s_isr_vin = vin_raw;
    s_isr_off_current = off_current_raw;
    s_isr_cycle_current = s_isr_current;
    s_isr_cycle_pulse_ticks = s_isr_current_pulse_ticks;
    s_isr_cycle_current_valid = s_isr_current_pending;
    s_isr_current_pending = false;
    ++s_isr_quiet_sequence;
}

void measure_process(void)
{
    const tip_profile_t *p = profile_active();
    uint32_t seq;
    uint16_t temp_raw, vin_raw, off_current_raw;
    uint16_t temp_min, temp_max, cycle_current, cycle_ticks;
    bool cycle_current_valid;
    __disable_irq();
    seq = s_isr_quiet_sequence;
    temp_raw = s_isr_temp;
    vin_raw = s_isr_vin;
    off_current_raw = s_isr_off_current;
    temp_min = s_isr_temp_min;
    temp_max = s_isr_temp_max;
    cycle_current = s_isr_cycle_current;
    cycle_ticks = s_isr_cycle_pulse_ticks;
    cycle_current_valid = s_isr_cycle_current_valid;
    __enable_irq();
    if (seq == s_processed_sequence) return;
    s_processed_sequence = seq;
    s_data.current_sample_valid = cycle_current_valid;
    s_data.current_sample_heater_on = cycle_current_valid;
    s_data.current_raw = cycle_current_valid ? cycle_current : 0U;
    s_data.current_ma = cycle_current_valid ? current_raw_to_ma(cycle_current) : 0U;
    if (!s_history_primed) {
        s_temp_history[0] = s_temp_history[1] = s_temp_history[2] = temp_raw;
        s_vin_history[0] = s_vin_history[1] = s_vin_history[2] = vin_raw;
        s_history_primed = true;
    }
    s_temp_history[s_history_index] = temp_raw;
    s_vin_history[s_history_index] = vin_raw;
    s_history_index = (uint8_t)((s_history_index + 1U) % 3U);

    s_data.temp_raw = temp_raw;
    s_data.temp_burst_min_raw = temp_min;
    s_data.temp_burst_max_raw = temp_max;
    s_data.temp_burst_span_raw = temp_max >= temp_min ? temp_max - temp_min : 0U;
    s_data.vin_raw = vin_raw;
    s_data.off_current_raw = off_current_raw;
    /* Track the op-amp zero only near its current baseline. A real off-state
     * leakage current must not be learned away as a new zero. */
    if (s_data.off_current_raw < (uint32_t)s_data.current_zero_raw + 40U)
        s_data.current_zero_raw = (uint16_t)(((uint32_t)s_data.current_zero_raw * 31UL +
                                              s_data.off_current_raw) / 32UL);
    s_data.off_current_ma = current_raw_to_ma(s_data.off_current_raw);
    s_data.off_current_sample_valid = true;
    s_data.temp_filtered_raw = median3(s_temp_history[0], s_temp_history[1], s_temp_history[2]);
    s_data.temperature_base_c = temperature_base_from_raw(s_data.temp_filtered_raw, p);
    s_data.temperature_uncorrected_c = profile_calibrate_temperature(s_data.temperature_base_c);
    s_data.vin_filtered_raw = median3(s_vin_history[0], s_vin_history[1], s_vin_history[2]);

    /* Calibrated board divider ratio. */
    uint32_t pin_mv = ((uint32_t)s_data.vin_filtered_raw * STATION_ADC_VREF_MV) / STATION_ADC_FULL_SCALE;
    s_data.vin_mv = pin_mv * STATION_VIN_DIVIDER_RATIO;
    s_data.adc_pin_mv = (uint16_t)(((uint32_t)s_data.temp_filtered_raw * STATION_ADC_VREF_MV) / STATION_ADC_FULL_SCALE);
    s_data.vin_valid = profile_supply_in_range(s_data.vin_mv);
    power_record_cycle_measurement(s_data.current_ma, s_data.vin_mv,
        cycle_current_valid ? cycle_ticks : 0U);

    if (s_data.temp_raw >= TIP_OPEN_RAW) s_data.tip_present = false;
    else if (s_data.temp_raw <= TIP_PRESENT_RAW) s_data.tip_present = true;

    /* Experimental correction for the repeatable ~300 C PA1 collapse. Only
     * arm after a hot reading drops by roughly that amount within 400 ms;
     * never infer a temperature from an open or near-zero sensor signal. */
    uint32_t now = board_millis();
    int16_t uncorrected = s_data.temperature_uncorrected_c;
    bool sensor_usable = s_data.tip_present && s_data.temp_raw >= 8U;
    if (!s_drop_compensation_enabled || !sensor_usable) {
        s_drop_active = false;
        s_drop_peak_ms = 0;
    } else if (!s_drop_active) {
        if (s_drop_peak_ms != 0U && now - s_drop_peak_ms <= TEMP_DROP_WINDOW_MS &&
            control_get()->heating_enabled && s_drop_peak_c >= 250 &&
            s_drop_peak_c - uncorrected >= TEMP_DROP_MIN_C &&
            s_drop_peak_c - uncorrected <= TEMP_DROP_MAX_C) {
            s_drop_active = true;
            s_drop_reference_c = s_drop_peak_c;
        } else if (s_drop_peak_ms == 0U || now - s_drop_peak_ms > TEMP_DROP_WINDOW_MS ||
                   uncorrected > s_drop_peak_c) {
            s_drop_peak_c = uncorrected;
            s_drop_peak_ms = now;
        }
    } else if (uncorrected >= s_drop_reference_c - 80) {
        s_drop_active = false;
        s_drop_peak_c = uncorrected;
        s_drop_peak_ms = now;
    }
    s_data.temperature_compensated = s_drop_active;
    s_data.temperature_c = (int16_t)(uncorrected +
        (s_drop_active ? TEMP_DROP_COMPENSATION_C : 0));

    s_data.resistance_mohm = s_data.current_ma > 50U ? (s_data.vin_mv * 1000UL) / s_data.current_ma : 0;
    s_data.sample_time_ms = now;
    s_data.sample_sequence = seq;
    s_data.quiet_sample_valid = true;
    s_new_quiet = true;
}

const measurement_t *measure_get(void) { return &s_data; }
bool measure_take_new_quiet_sample(void) { bool v = s_new_quiet; s_new_quiet = false; return v; }

void ADC1_IRQHandler(void)
{
    if (ADC_GetITStatus(ADC1, ADC_IT_EOC) != RESET) {
        uint16_t raw = ADC_GetConversionValue(ADC1);
        ADC_ClearITPendingBit(ADC1, ADC_IT_EOC);
        ADC_ClearFlag(ADC1, ADC_FLAG_STRT);
        if (s_adc_phase == ADC_PHASE_CURRENT) {
            measure_current_eoc_isr(raw);
            s_adc_phase = ADC_PHASE_IDLE;
        } else if (s_adc_phase == ADC_PHASE_TEMP_DISCARD) {
            /* Discard the first PA1 result after switching the ADC mux away
             * from PA6. This lets the sample-and-hold capacitor settle. */
            s_adc_phase = ADC_PHASE_TEMP_COLLECT;
            ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        } else if (s_adc_phase == ADC_PHASE_TEMP_COLLECT) {
            s_temp_burst_sum += raw;
            if (raw < s_temp_burst_min) s_temp_burst_min = raw;
            if (raw > s_temp_burst_max) s_temp_burst_max = raw;
            if (++s_temp_burst_count < TEMP_ADC_BURST_SAMPLES) {
                ADC_SoftwareStartConvCmd(ADC1, ENABLE);
                return;
            }
            /* Average the burst without its highest and lowest readings.
             * The three-cycle median in measure_process remains in place. */
            s_quiet_temp_staging = (uint16_t)((s_temp_burst_sum -
                s_temp_burst_min - s_temp_burst_max) /
                (TEMP_ADC_BURST_SAMPLES - 2U));
            s_isr_temp_min = s_temp_burst_min;
            s_isr_temp_max = s_temp_burst_max;
            ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_11Cycles);
            s_adc_phase = ADC_PHASE_VIN_DISCARD;
            ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        } else if (s_adc_phase == ADC_PHASE_VIN_DISCARD) {
            /* PA0 has a different source voltage, so discard its first result
             * after the PA1 burst for the same mux-settling reason. */
            s_adc_phase = ADC_PHASE_VIN;
            ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        } else if (s_adc_phase == ADC_PHASE_VIN) {
            s_isr_vin = raw;
            ADC_RegularChannelConfig(ADC1, ADC_Channel_6, 1, ADC_SampleTime_11Cycles);
            s_adc_phase = ADC_PHASE_OFF_CURRENT_DISCARD;
            ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        } else if (s_adc_phase == ADC_PHASE_OFF_CURRENT_DISCARD) {
            s_adc_phase = ADC_PHASE_OFF_CURRENT;
            ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        } else if (s_adc_phase == ADC_PHASE_OFF_CURRENT) {
            /* If a delayed ADC sequence reached the next pulse, discard it
             * rather than label an on-current reading as leakage. */
            if (!power_get()->pulse_active)
                measure_quiet_eoc_isr(s_quiet_temp_staging, s_isr_vin, raw);
            s_adc_phase = ADC_PHASE_IDLE;
        } else {
            s_adc_phase = ADC_PHASE_IDLE;
        }
    }
}
