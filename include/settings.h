#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

void settings_init(void);
bool settings_save(void);
void settings_discard(void);
void settings_factory_defaults(void);
bool settings_loaded_from_flash(void);
bool settings_setup_complete(void);
void settings_set_setup_complete(bool complete);
uint8_t settings_room_temperature_c(void);
void settings_set_room_temperature_c(uint8_t room_c);
bool settings_tip_warning_enabled(void);
void settings_set_tip_warning_enabled(bool enabled);

#endif
