#include "board.h"
#include "app_config.h"
#include "control.h"
#include "encoder.h"

static volatile uint32_t s_ms;
static volatile uint8_t s_backlight = 75;
static volatile uint8_t s_backlight_override_percent;
static volatile bool s_backlight_override_enabled;
static uint8_t s_bl_accumulator;
static volatile uint16_t s_buzzer_on_ms;
static volatile uint16_t s_buzzer_gap_ms;
static volatile uint8_t s_buzzer_repeats;
static uint8_t s_buzzer_phase;
static bool s_buzzer_enabled = true;
static bool s_setpoint_beep = true;
static uint8_t s_sub_ms_ticks;

void TIM2_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void board_safe_boot(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Pin = PIN_HEATER_GATE;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    board_heater_gpio_off();
}

void board_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_TimeBaseInitTypeDef timer = {0};
    NVIC_InitTypeDef nvic = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    board_safe_boot();

    gpio.GPIO_Pin = PIN_ADC_VIN | PIN_ADC_TEMP | PIN_ADC_CURRENT;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = PIN_ENCODER_A | PIN_ENCODER_B | PIN_ENCODER_SW;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = PIN_STAND;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOC, &gpio);
    /* PC3 has an external pull-down; the optional sensor drives it high. */
    gpio.GPIO_Pin = PIN_VIB;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &gpio);

    gpio.GPIO_Pin = PIN_BUZZER;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    GPIOB->BCR = PIN_BUZZER;

    gpio.GPIO_Pin = PIN_BACKLIGHT;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOC, &gpio);

    TIM_Cmd(TIM2, DISABLE);
    timer.TIM_Prescaler = (uint16_t)((SystemCoreClock / 1000000UL) - 1UL);
    timer.TIM_Period = (uint16_t)((1000000UL / BUZZER_CARRIER_ISR_HZ) - 1UL);
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &timer);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel = TIM2_UP_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    TIM_Cmd(TIM2, ENABLE);
}

uint32_t board_millis(void) { return s_ms; }

void board_watchdog_start(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_256);
    IWDG_SetReload(350);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

void board_watchdog_feed(void) { IWDG_ReloadCounter(); }

void board_backlight_set(uint8_t percent) { s_backlight = percent > 100U ? 100U : percent; }
uint8_t board_backlight_get(void) { return s_backlight; }
void board_backlight_override(bool enabled, uint8_t percent)
{
    s_backlight_override_percent = percent > 100U ? 100U : percent;
    s_backlight_override_enabled = enabled;
}

void board_buzzer_pattern(uint8_t pattern)
{
    if (pattern == 0U || (!s_buzzer_enabled && pattern != 3U)) {
        s_buzzer_on_ms = 0; s_buzzer_gap_ms = 0; s_buzzer_repeats = 0;
        GPIOB->BCR = PIN_BUZZER;
        return;
    }
    s_buzzer_repeats = pattern == 3U ? 3U : (pattern == 2U ? 2U : 1U);
    s_buzzer_on_ms = pattern == 4U ? BUZZER_LONG_MS :
                     (pattern == 3U ? 160U : (pattern == 2U ? 100U : BUZZER_SHORT_MS));
    s_buzzer_gap_ms = 0;
    s_buzzer_phase = 0;
    GPIOB->BCR = PIN_BUZZER;
}

void board_buzzer_enable(bool enabled) { s_buzzer_enabled = enabled; }
bool board_buzzer_enabled(void) { return s_buzzer_enabled; }
void board_setpoint_beep_enable(bool enabled) { s_setpoint_beep = enabled; }
bool board_setpoint_beep_get(void) { return s_setpoint_beep; }

uint8_t board_stand_raw(void) { return GPIO_ReadInputDataBit(GPIOC, PIN_STAND); }
uint8_t board_vib_raw(void) { return GPIO_ReadInputDataBit(GPIOC, PIN_VIB); }
uint8_t board_encoder_a_raw(void) { return GPIO_ReadInputDataBit(GPIOA, PIN_ENCODER_A); }
uint8_t board_encoder_b_raw(void) { return GPIO_ReadInputDataBit(GPIOA, PIN_ENCODER_B); }
uint8_t board_button_raw(void) { return GPIO_ReadInputDataBit(GPIOA, PIN_ENCODER_SW); }

void board_tick_isr(void)
{
    /* TIM2 interrupts at 8 kHz. Toggling PB1 on every interrupt generates a
     * 4 kHz tone; running the rest of the scheduler every eighth tick keeps
     * its original 1 ms time base. */
    if (s_buzzer_on_ms != 0U) {
        s_buzzer_phase ^= 1U;
        if (s_buzzer_phase) GPIOB->BSHR = PIN_BUZZER;
        else GPIOB->BCR = PIN_BUZZER;
    } else {
        s_buzzer_phase = 0;
        GPIOB->BCR = PIN_BUZZER;
    }

    if (++s_sub_ms_ticks < (BUZZER_CARRIER_ISR_HZ / 1000UL)) return;
    s_sub_ms_ticks = 0;
    ++s_ms;
    uint8_t brightness = s_backlight_override_enabled ?
                         s_backlight_override_percent : s_backlight;
    s_bl_accumulator = (uint8_t)(s_bl_accumulator + brightness);
    if (s_bl_accumulator >= 100U) {
        s_bl_accumulator = (uint8_t)(s_bl_accumulator - 100U);
        GPIO_SetBits(GPIOC, PIN_BACKLIGHT);
    } else GPIO_ResetBits(GPIOC, PIN_BACKLIGHT);

    if (s_buzzer_on_ms != 0U) {
        if (--s_buzzer_on_ms == 0U) {
            GPIOB->BCR = PIN_BUZZER;
            if (s_buzzer_repeats > 1U) s_buzzer_gap_ms = 80U;
            else s_buzzer_repeats = 0U;
        }
    } else if (s_buzzer_gap_ms != 0U && --s_buzzer_gap_ms == 0U) {
        --s_buzzer_repeats;
        s_buzzer_on_ms = 70U;
        s_buzzer_phase = 0;
    }
    input_tick_isr();
    control_tick_1ms();
}

void TIM2_UP_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        board_tick_isr();
    }
}
