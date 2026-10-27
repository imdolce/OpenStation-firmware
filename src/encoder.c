#include "encoder.h"
#include "board.h"

static volatile int8_t s_delta;
static volatile button_event_t s_button_event;
static uint8_t s_prev_ab;
static int8_t s_quadrature_sum;
static uint8_t s_button_integrator;
static uint16_t s_press_ms;
static uint16_t s_click_wait_ms;
static uint8_t s_click_pending;
static bool s_encoder_reversed;

void input_init(void)
{
    s_prev_ab = (uint8_t)((board_encoder_a_raw() << 1) | board_encoder_b_raw());
}

void input_tick_isr(void)
{
    static const int8_t transition[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
    uint8_t ab = (uint8_t)((board_encoder_a_raw() << 1) | board_encoder_b_raw());
    s_quadrature_sum += transition[(s_prev_ab << 2) | ab];
    s_prev_ab = ab;
    if (s_quadrature_sum >= 4) { if (s_delta < 100) ++s_delta; s_quadrature_sum = 0; }
    else if (s_quadrature_sum <= -4) { if (s_delta > -100) --s_delta; s_quadrature_sum = 0; }

    if (board_button_raw() == 0U) {
        if (s_button_integrator < 20U) ++s_button_integrator;
    } else if (s_button_integrator > 0U) --s_button_integrator;

    if (s_button_integrator == 20U) {
        if (s_press_ms < 2000U) ++s_press_ms;
        if (s_press_ms == 800U) { s_button_event = BUTTON_EVENT_LONG; s_click_pending = 0; }
    } else if (s_button_integrator == 0U && s_press_ms != 0U) {
        if (s_press_ms < 800U) {
            if (s_click_pending) { s_button_event = BUTTON_EVENT_DOUBLE; s_click_pending = 0; }
            else { s_click_pending = 1; s_click_wait_ms = 300U; }
        }
        s_press_ms = 0;
    }
    if (s_click_pending && s_click_wait_ms != 0U && --s_click_wait_ms == 0U) {
        s_button_event = BUTTON_EVENT_SHORT;
        s_click_pending = 0;
    }
}

int8_t input_take_rotation(void)
{
    int8_t v = s_delta;
    s_delta = 0;
    return s_encoder_reversed ? (int8_t)-v : v;
}
bool input_encoder_reversed(void) { return s_encoder_reversed; }
void input_set_encoder_reversed(bool reversed) { s_encoder_reversed = reversed; }
button_event_t input_take_button_event(void) { button_event_t e = s_button_event; s_button_event = BUTTON_EVENT_NONE; return e; }
