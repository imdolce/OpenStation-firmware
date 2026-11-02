#ifndef TIP_SETUP_H
#define TIP_SETUP_H
#include <stdbool.h>
#include <stdint.h>
#include "st7735.h"
void tip_setup_init(ST7735_Object_t *lcd);
bool tip_setup_active(void);
void tip_setup_start_profile(void);
void tip_setup_rotate(int8_t steps);
void tip_setup_button(uint8_t event);
void tip_setup_sample(void);
void tip_setup_poll(void);
void tip_setup_render(void);
#endif
