#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

void settings_init(void);
bool settings_save(void);
void settings_discard(void);
void settings_factory_defaults(void);
bool settings_loaded_from_flash(void);
bool settings_tip_warning_enabled(void);
void settings_set_tip_warning_enabled(bool enabled);

#endif
