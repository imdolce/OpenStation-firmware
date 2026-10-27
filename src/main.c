#include "debug.h"
#include "app_config.h"
#include "board.h"
#include "ch32x033_st7735.h"
#include "control.h"
#include "encoder.h"
#include "measure.h"
#include "power.h"
#include "safety.h"
#include "settings.h"
#include "ui.h"

static ST7735_Object_t s_lcd;

int main(void)
{
    uint32_t last_ui = 0;
    SystemCoreClockUpdate();
    Delay_Init();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

    board_safe_boot();
    if (CH32_ST7735_Init(&s_lcd, ST7735_ORIENTATION_PORTRAIT) != ST7735_OK) {
        while (1) { board_heater_gpio_off(); }
    }
    ST7735_ClearRawRAM(&s_lcd, COLOR_BLACK);

    board_init();
    input_init();
    power_init();
    settings_init();
    measure_init();
    safety_init();
    control_init();
    ui_init(&s_lcd);
    ui_render();
    board_buzzer_pattern(4);
    board_watchdog_start();

    while (1) {
        measure_process();

        int8_t rotation = input_take_rotation();
        if (rotation != 0) {
            board_buzzer_pattern(1);
            if (!ui_fault_popup_active() && !ui_boot_warning_active()) {
                if (ui_settings_active()) ui_settings_rotate(rotation);
                else control_rotate(rotation);
            }
        }
        button_event_t event = input_take_button_event();
        if (event != BUTTON_EVENT_NONE) board_buzzer_pattern(1);
        if (event == BUTTON_EVENT_SHORT) {
            if (ui_fault_popup_active()) ui_dismiss_fault_popup();
            else if (ui_boot_warning_active()) ui_dismiss_boot_warning();
            else if (ui_settings_active()) ui_settings_press();
            else control_short_press();
        }
        else if (event == BUTTON_EVENT_DOUBLE) {
            if (!ui_fault_popup_active() && !ui_boot_warning_active()) {
                if (ui_settings_active()) ui_settings_double_press();
                else control_double_press();
            }
        }
        else if (event == BUTTON_EVENT_LONG && !ui_boot_warning_active()) {
            control_long_press();
            ui_toggle_page();
        }

        bool new_sample = measure_take_new_quiet_sample();
        if (new_sample) control_run_cycle();
        if (power_take_cycle_event()) {
            const control_status_t *c = control_get();
            safety_supervise((uint8_t)c->state, c->heating_enabled);
        }

        uint32_t now = board_millis();
        if ((uint32_t)(now - last_ui) >= UI_REFRESH_MS) {
            last_ui = now;
            ui_render();
        }
        board_watchdog_feed();
    }
}
