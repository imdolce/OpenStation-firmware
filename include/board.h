#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>
#include "ch32x035.h"

#define PIN_ADC_VIN       GPIO_Pin_0
#define PIN_ADC_TEMP      GPIO_Pin_1
#define PIN_ADC_CURRENT   GPIO_Pin_6
#define PIN_BUZZER        GPIO_Pin_1
#define PIN_HEATER_GATE   GPIO_Pin_7
#define PIN_VIB           GPIO_Pin_3
#define PIN_BACKLIGHT     GPIO_Pin_16
#define PIN_STAND         GPIO_Pin_17
#define PIN_ENCODER_A     GPIO_Pin_9
#define PIN_ENCODER_B     GPIO_Pin_10
#define PIN_ENCODER_SW    GPIO_Pin_11

void board_safe_boot(void);
void board_init(void);
uint32_t board_millis(void);
void board_watchdog_start(void);
void board_watchdog_feed(void);
void board_backlight_set(uint8_t percent);
uint8_t board_backlight_get(void);
void board_backlight_override(bool enabled, uint8_t percent);
void board_buzzer_pattern(uint8_t pattern);
void board_buzzer_enable(bool enabled);
bool board_buzzer_enabled(void);
void board_setpoint_beep_enable(bool enabled);
bool board_setpoint_beep_get(void);
uint8_t board_stand_raw(void);
uint8_t board_vib_raw(void);
uint8_t board_encoder_a_raw(void);
uint8_t board_encoder_b_raw(void);
uint8_t board_button_raw(void);
void board_tick_isr(void);

static inline void board_heater_gpio_off(void) { GPIOB->BCR = PIN_HEATER_GATE; }
static inline void board_heater_gpio_on(void) { GPIOB->BSHR = PIN_HEATER_GATE; }

#endif
