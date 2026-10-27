#ifndef CH32X033_ST7735_H
#define CH32X033_ST7735_H

#include <stdint.h>
#include "ch32x035.h"
#include "st7735.h"

/* Standard RGB565 16-bit Color Definitions */
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_ORANGE    0xFD20
#define COLOR_GRAY      0x8410
#define COLOR_DARKGRAY  0x39E7

/* Display Resolution (128x160 Portrait) */
#define DISPLAY_WIDTH   ST7735_WIDTH   /* 128 */
#define DISPLAY_HEIGHT  ST7735_HEIGHT  /* 160 */

/* Pin Definitions according to CH32X033_PIN_FUNCTIONS.md */
#define ST7735_PIN_SCK     GPIO_Pin_5   /* PA5: SPI1 SCK (AF_PP) */
#define ST7735_PIN_MOSI    GPIO_Pin_7   /* PA7: SPI1 MOSI (AF_PP) */
#define ST7735_PIN_CS      GPIO_Pin_4   /* PA4: CS (Out_PP, Active Low) */
#define ST7735_PIN_DC      GPIO_Pin_3   /* PA3: D/C or A0 (Out_PP, 0=Cmd, 1=Data) */
#define ST7735_PIN_RST     GPIO_Pin_2   /* PA2: RESET (Out_PP, Active Low) */
#define ST7735_PIN_BL      GPIO_Pin_16  /* PC16: Backlight (Out_PP, 1=ON) */
#define SAFE_HEATER_PIN    GPIO_Pin_7   /* PB7: Heater Gate (Out_PP, 0=FORCED OFF) */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize CH32X033 hardware (SPI1, GPIOs, Timer) and initialize ST7735 panel.
 *        Guarantees that PB7 (heater) is locked LOW for hardware safety.
 * @param pObj Pointer to ST7735 component object
 * @param orientation Display orientation (ST7735_ORIENTATION_PORTRAIT, etc.)
 * @return ST7735_OK on success, ST7735_ERROR on failure
 */
int32_t CH32_ST7735_Init(ST7735_Object_t *pObj, uint32_t orientation);

/**
 * @brief Control ST7735 LED backlight
 * @param enable 1 to turn ON backlight, 0 to turn OFF
 */
void CH32_ST7735_Backlight(uint8_t enable);

/**
 * @brief Returns free-running millisecond tick count
 */
uint32_t CH32_ST7735_GetTick(void);

/**
 * @brief Fill entire 128x160 display with a single color
 */
void ST7735_FillScreen(ST7735_Object_t *pObj, uint16_t color);

/**
 * @brief Draw a 1-pixel border around the outer edges of the screen
 *        to verify physical panel alignment and active bounds.
 */
void ST7735_DrawBorder(ST7735_Object_t *pObj, uint16_t color);

/**
 * @brief Draw single ASCII character using 5x7 font
 */
void ST7735_DrawChar(ST7735_Object_t *pObj, uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t size);

/**
 * @brief Draw string using 5x7 font with optional background
 */
void ST7735_DrawString(ST7735_Object_t *pObj, uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t size);

#ifdef __cplusplus
}
#endif

#endif /* CH32X033_ST7735_H */
