#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "ch32x033_st7735.h"

void ui_init(ST7735_Object_t *lcd);
void ui_toggle_page(void);
bool ui_settings_active(void);
void ui_settings_rotate(int8_t detents);
void ui_settings_press(void);
void ui_settings_double_press(void);
bool ui_fault_popup_active(void);
void ui_dismiss_fault_popup(void);
bool ui_boot_warning_active(void);
void ui_dismiss_boot_warning(void);
void ui_render(void);
uint8_t ui_display_orientation(void);
bool ui_set_display_orientation(uint8_t orientation);

#endif
