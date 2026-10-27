#include "power.h"
#include "app_config.h"
#include "board.h"
#include "measure.h"
#include "profiles.h"
#include "safety.h"
#include "ch32x035.h"

static volatile power_status_t s_status;
static volatile bool s_cycle_event;

void TIM1_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM1_CC_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

static void output_disable(void)
{
    TIM_SetCompare2(TIM1, 0);
    board_heater_gpio_off();
    s_status.pulse_active = false;
}

void power_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_TimeBaseInitTypeDef base = {0};
    TIM_OCInitTypeDef oc = {0};
    NVIC_InitTypeDef nvic = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO, ENABLE);
    board_heater_gpio_off();

    gpio.GPIO_Pin = PIN_HEATER_GATE;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    board_heater_gpio_off();

    base.TIM_Prescaler = (uint16_t)((SystemCoreClock / HEATER_TIMER_HZ) - 1UL);
    base.TIM_Period = HEATER_PERIOD_TICKS - 1U;
    base.TIM_CounterMode = TIM_CounterMode_Up;
    base.TIM_ClockDivision = TIM_CKD_DIV1;
    base.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &base);

    /* Channel 2 is an internal timing compare. The update ISR drives PB7 HIGH
     * and this compare's ISR drives it LOW. This avoids complementary-output
     * polarity ambiguity while retaining hardware-timed 20 Hz pulse widths. */
    oc.TIM_OCMode = TIM_OCMode_Timing;
    oc.TIM_OutputState = TIM_OutputState_Disable;
    oc.TIM_OutputNState = TIM_OutputNState_Disable;
    oc.TIM_Pulse = 0;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_OCNPolarity = TIM_OCNPolarity_High;
    oc.TIM_OCIdleState = TIM_OCIdleState_Reset;
    oc.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    TIM_OC2Init(TIM1, &oc);
    /* Direct CCR2 writes at update time avoid a one-cycle stale pulse. */
    TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Disable);

    /* Internal compare events trigger the ADC without occupying pins. */
    oc.TIM_OCMode = TIM_OCMode_Timing;
    oc.TIM_OutputNState = TIM_OutputNState_Disable;
    oc.TIM_Pulse = HEATER_CURRENT_SAMPLE_TICKS;
    TIM_OC1Init(TIM1, &oc);
    oc.TIM_Pulse = HEATER_SENSOR_SETTLE_TICKS;
    TIM_OC3Init(TIM1, &oc);

    TIM_ClearITPendingBit(TIM1, TIM_IT_Update | TIM_IT_CC1 | TIM_IT_CC2 | TIM_IT_CC3);
    TIM_ITConfig(TIM1, TIM_IT_Update | TIM_IT_CC1 | TIM_IT_CC2 | TIM_IT_CC3, ENABLE);
    nvic.NVIC_IRQChannel = TIM1_UP_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    nvic.NVIC_IRQChannel = TIM1_CC_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_Init(&nvic);

    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_Cmd(TIM1, ENABLE);
    output_disable();
}

void power_request(uint16_t duty_permille)
{
    const tip_profile_t *p = profile_active();
    const measurement_t *m = measure_get();
    uint32_t limited = duty_permille > 1000U ? 1000U : duty_permille;
    if (limited > p->max_duty_permille) limited = p->max_duty_permille;

    if (!m->quiet_sample_valid || !m->vin_valid || !m->tip_present || safety_latched())
        limited = 0;
    if (m->vin_mv > 0U && p->nominal_resistance_mohm > 0U) {
        uint64_t vin2 = (uint64_t)m->vin_mv * m->vin_mv;
        uint32_t power_duty = (uint32_t)(((uint64_t)p->max_power_mw * 1000ULL *
                                          p->nominal_resistance_mohm) / vin2);
        uint32_t voltage_duty = (uint32_t)(((uint64_t)p->rated_voltage_mv *
                                            p->rated_voltage_mv * 1000ULL) / vin2);
        if (limited > power_duty) limited = power_duty;
        if (limited > voltage_duty) limited = voltage_duty;
    } else {
        limited = 0;
    }

    s_status.requested_permille = duty_permille;
    s_status.pulse_ticks = (uint16_t)((limited * HEATER_PERIOD_TICKS) / 1000UL);
    if (s_status.pulse_ticks > HEATER_MAX_ON_TICKS) s_status.pulse_ticks = HEATER_MAX_ON_TICKS;
    /* Report the duty actually scheduled, including the mandatory quiet window. */
    s_status.limited_permille = (uint16_t)((uint32_t)s_status.pulse_ticks * 1000UL / HEATER_PERIOD_TICKS);
    if (s_status.pulse_ticks == 0U) {
        s_status.average_current_ma = 0U;
        s_status.estimated_power_mw = 0U;
        s_status.cycle_measurement_valid = false;
    }
    s_status.pulse_us = (uint16_t)(s_status.pulse_ticks * 10U);
#if STATION_HEATER_OUTPUT_ENABLE
    s_status.output_armed = (p->characterized != 0U) || STATION_SERVICE_ALLOW_UNCHARACTERIZED;
#else
    s_status.output_armed = false;
#endif
}

void power_record_cycle_measurement(uint16_t on_current_ma, uint32_t vin_mv,
                                    uint16_t on_ticks)
{
    if (on_ticks == 0U || s_status.pulse_ticks == 0U || !s_status.output_armed) {
        s_status.average_current_ma = 0U;
        s_status.estimated_power_mw = 0U;
        s_status.cycle_measurement_valid = false;
        return;
    }
    /* PA6 samples the pulse current; only the pulse fraction contributes to
     * current and power averaged over the complete 50 ms heater cycle. */
    uint32_t average_ma = ((uint32_t)on_current_ma * on_ticks +
                           HEATER_PERIOD_TICKS / 2U) / HEATER_PERIOD_TICKS;
    s_status.average_current_ma = (uint16_t)average_ma;
    s_status.estimated_power_mw =
        (uint32_t)(((uint64_t)vin_mv * average_ma) / 1000ULL);
    s_status.cycle_measurement_valid = true;
}

void power_emergency_stop_isr(void)
{
    s_status.pulse_ticks = 0;
    s_status.average_current_ma = 0;
    s_status.estimated_power_mw = 0;
    s_status.cycle_measurement_valid = false;
    output_disable();
}
void power_force_off(void)
{
    s_status.requested_permille = 0;
    s_status.limited_permille = 0;
    s_status.pulse_ticks = 0;
    s_status.average_current_ma = 0;
    s_status.estimated_power_mw = 0;
    s_status.cycle_measurement_valid = false;
    output_disable();
}
const power_status_t *power_get(void) { return (const power_status_t *)&s_status; }
bool power_take_cycle_event(void) { bool v = s_cycle_event; s_cycle_event = false; return v; }

void TIM1_UP_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
        if (s_status.pulse_active) {
            output_disable();
            safety_trip_isr(FAULT_PULSE_TIMEOUT);
        }
        ++s_status.cycle_count;
        s_cycle_event = true;
        bool run_pulse = s_status.output_armed && s_status.pulse_ticks != 0U &&
                         !safety_latched();
        uint16_t actual_pulse_ticks = run_pulse ? s_status.pulse_ticks : 0U;
        uint16_t quiet_ticks = (uint16_t)(actual_pulse_ticks + HEATER_SENSOR_SETTLE_TICKS);
        if (quiet_ticks >= HEATER_PERIOD_TICKS)
            quiet_ticks = HEATER_PERIOD_TICKS - 1U;
        TIM_SetCompare3(TIM1, quiet_ticks);

        if (run_pulse) {
            TIM_SetCompare2(TIM1, actual_pulse_ticks);
#if STATION_HEATER_OUTPUT_ENABLE
            board_heater_gpio_on();
#endif
            s_status.pulse_active = true;
        } else output_disable();
    }
}

void TIM1_CC_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM1, TIM_IT_CC1) != RESET) {
        TIM_ClearITPendingBit(TIM1, TIM_IT_CC1);
        measure_trigger_current_isr();
    }
    if (TIM_GetITStatus(TIM1, TIM_IT_CC2) != RESET) {
        TIM_ClearITPendingBit(TIM1, TIM_IT_CC2);
        output_disable();
    }
    if (TIM_GetITStatus(TIM1, TIM_IT_CC3) != RESET) {
        TIM_ClearITPendingBit(TIM1, TIM_IT_CC3);
        measure_trigger_quiet_isr();
    }
}
