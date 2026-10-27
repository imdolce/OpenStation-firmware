#ifndef ENCODER_H
#define ENCODER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { BUTTON_EVENT_NONE = 0, BUTTON_EVENT_SHORT, BUTTON_EVENT_LONG, BUTTON_EVENT_DOUBLE } button_event_t;
void input_init(void);
void input_tick_isr(void);
int8_t input_take_rotation(void);
bool input_encoder_reversed(void);
void input_set_encoder_reversed(bool reversed);
button_event_t input_take_button_event(void);

#endif
