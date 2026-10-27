#include "ch32x033_st7735.h"
#include "debug.h"
#include "font5x7.h"

#define LCD_CS_LOW()    (GPIOA->BCR = GPIO_Pin_4)
#define LCD_CS_HIGH()   (GPIOA->BSHR = GPIO_Pin_4)
#define LCD_DC_LOW()    (GPIOA->BCR = GPIO_Pin_3)
#define LCD_DC_HIGH()   (GPIOA->BSHR = GPIO_Pin_3)
#define LCD_RST_LOW()   (GPIOA->BCR = GPIO_Pin_2)
#define LCD_RST_HIGH()  (GPIOA->BSHR = GPIO_Pin_2)

/* PC16 Backlight: Use GPIO_SetBits/ResetBits to ensure BSXR is used for Pin 16 */
static inline void LCD_BL_HIGH(void)
{
    GPIO_SetBits(GPIOC, ST7735_PIN_BL);
}

static inline void LCD_BL_LOW(void)
{
    GPIO_ResetBits(GPIOC, ST7735_PIN_BL);
}

static inline int32_t SPI1_SendByte(uint8_t byte)
{
    uint32_t timeout = 100000;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET)
    {
        if (--timeout == 0) return ST7735_ERROR;
    }
    SPI_I2S_SendData(SPI1, byte);
    timeout = 100000;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET)
    {
        if (--timeout == 0) return ST7735_ERROR;
    }
    return ST7735_OK;
}

static int32_t SPI1_SendBuffer(const uint8_t *data, uint32_t length)
{
    if (data == NULL || length == 0U) return ST7735_OK;
    for (uint32_t i = 0; i < length; ++i)
    {
        uint32_t timeout = 100000U;
        while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET)
        {
            if (--timeout == 0U) return ST7735_ERROR;
        }
        SPI_I2S_SendData(SPI1, data[i]);
    }
    uint32_t timeout = 100000U;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET)
    {
        if (--timeout == 0U) return ST7735_ERROR;
    }
    return ST7735_OK;
}

static int32_t CH32_ST7735_IO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    SPI_InitTypeDef  spi  = {0};
    TIM_TimeBaseInitTypeDef tim = {0};

    /* Enable peripheral clocks */
    RCC_APB2PeriphClockCmd(
        RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
        RCC_APB2Periph_GPIOC | RCC_APB2Periph_SPI1,
        ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* -------------------------------------------------------------
     * HARDWARE SAFETY FIRST:
     * PB7 drives Q1(5551) which pulls Q2(80P06 PMOS) gate LOW.
     * High = Heater ON; Low = Heater OFF.
     * Configure PB7 as Output and force LOW IMMEDIATELY.
     * ------------------------------------------------------------- */
    gpio.GPIO_Pin   = SAFE_HEATER_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, SAFE_HEATER_PIN);

    /* Backlight: PC16 Output Push-Pull, High = ON */
    gpio.GPIO_Pin   = ST7735_PIN_BL;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gpio);
    LCD_BL_HIGH();

    /* ST7735 Control Pins: PA2=RST, PA3=DC, PA4=CS */
    gpio.GPIO_Pin   = ST7735_PIN_RST | ST7735_PIN_DC | ST7735_PIN_CS;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    LCD_CS_HIGH();
    LCD_DC_HIGH();
    LCD_RST_HIGH();

    /* SPI1 Pins: PA5=SCK, PA7=MOSI (Alternate Function Push-Pull).
     * NOTE: PA6 is tip-current analog input (ADC1 Ch6) and MUST NOT
     * be configured as SPI MISO. */
    gpio.GPIO_Pin   = ST7735_PIN_SCK | ST7735_PIN_MOSI;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* SPI1 Peripheral: Master, 1-Line Tx, 8-bit, Mode 0 (CPOL=0, CPHA=0)
     * Clock: 48 MHz / 4 = 12 MHz (faster refresh, ST7735 supports up to 15 MHz) */
    spi.SPI_Direction         = SPI_Direction_1Line_Tx;
    spi.SPI_Mode              = SPI_Mode_Master;
    spi.SPI_DataSize          = SPI_DataSize_8b;
    spi.SPI_CPOL              = SPI_CPOL_Low;
    spi.SPI_CPHA              = SPI_CPHA_1Edge;
    spi.SPI_NSS               = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    spi.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);

    /* Hardware Reset Pulse on PA2 */
    LCD_RST_HIGH();
    Delay_Ms(10);
    LCD_RST_LOW();
    Delay_Ms(50);
    LCD_RST_HIGH();
    Delay_Ms(120);

    /* TIM2 as a 1 kHz free-running millisecond counter */
    tim.TIM_Prescaler     = (SystemCoreClock / 1000) - 1; /* 48000 - 1 */
    tim.TIM_Period        = 0xFFFF;
    tim.TIM_CounterMode   = TIM_CounterMode_Up;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &tim);
    TIM_Cmd(TIM2, ENABLE);

    return ST7735_OK;
}

static int32_t CH32_ST7735_IO_DeInit(void)
{
    LCD_BL_LOW();
    SPI_Cmd(SPI1, DISABLE);
    return ST7735_OK;
}

static int32_t CH32_ST7735_IO_WriteReg(uint8_t Reg, uint8_t *pData, uint32_t Length)
{
    LCD_CS_LOW();
    LCD_DC_LOW();
    if (SPI1_SendByte(Reg) != ST7735_OK)
    {
        LCD_CS_HIGH();
        return ST7735_ERROR;
    }

    if (Length > 0 && pData != NULL)
    {
        LCD_DC_HIGH();
        if (SPI1_SendBuffer(pData, Length) != ST7735_OK)
        {
            LCD_CS_HIGH();
            return ST7735_ERROR;
        }
    }
    LCD_CS_HIGH();

    return ST7735_OK;
}

static int32_t CH32_ST7735_IO_SendData(uint8_t *pData, uint32_t Length)
{
    if (Length == 0 || pData == NULL) return ST7735_OK;

    LCD_CS_LOW();
    LCD_DC_HIGH();
    if (SPI1_SendBuffer(pData, Length) != ST7735_OK)
    {
        LCD_CS_HIGH();
        return ST7735_ERROR;
    }
    LCD_CS_HIGH();

    return ST7735_OK;
}

static int32_t CH32_ST7735_IO_ReadReg(uint8_t Reg, uint8_t *pData)
{
    (void)Reg;
    (void)pData;
    /* Write-only interface: PA6 is reserved for current sense analog input */
    return ST7735_ERROR;
}

static int32_t CH32_ST7735_IO_RecvData(uint8_t *pData, uint32_t Length)
{
    (void)pData;
    (void)Length;
    return ST7735_ERROR;
}

uint32_t CH32_ST7735_GetTick(void)
{
    static uint32_t ms_ticks = 0;
    static uint16_t last_cnt = 0;
    uint16_t cur_cnt = TIM2->CNT;
    ms_ticks += (uint16_t)(cur_cnt - last_cnt);
    last_cnt = cur_cnt;
    return ms_ticks;
}

static ST7735_IO_t ch32_io = {
    .Init     = CH32_ST7735_IO_Init,
    .DeInit   = CH32_ST7735_IO_DeInit,
    .Address  = 0,
    .WriteReg = CH32_ST7735_IO_WriteReg,
    .ReadReg  = CH32_ST7735_IO_ReadReg,
    .SendData = CH32_ST7735_IO_SendData,
    .RecvData = CH32_ST7735_IO_RecvData,
    .GetTick  = CH32_ST7735_GetTick
};

int32_t CH32_ST7735_Init(ST7735_Object_t *pObj, uint32_t orientation)
{
    if (pObj == NULL) return ST7735_ERROR;

    if (ST7735_RegisterBusIO(pObj, &ch32_io) != ST7735_OK)
    {
        return ST7735_ERROR;
    }

    return ST7735_Init(pObj, ST7735_FORMAT_RBG565, orientation);
}

void CH32_ST7735_Backlight(uint8_t enable)
{
    if (enable) LCD_BL_HIGH();
    else LCD_BL_LOW();
}

void ST7735_FillScreen(ST7735_Object_t *pObj, uint16_t color)
{
    uint32_t width = 0, height = 0;
    ST7735_GetXSize(pObj, &width);
    ST7735_GetYSize(pObj, &height);
    ST7735_FillRect(pObj, 0, 0, width, height, color);
}

void ST7735_DrawBorder(ST7735_Object_t *pObj, uint16_t color)
{
    uint32_t width = 0, height = 0;
    ST7735_GetXSize(pObj, &width);
    ST7735_GetYSize(pObj, &height);

    if (width == 0 || height == 0) return;

    /* Top and bottom horizontal edges */
    ST7735_DrawHLine(pObj, 0, 0, width, color);
    ST7735_DrawHLine(pObj, 0, height - 1, width, color);

    /* Left and right vertical edges */
    ST7735_DrawVLine(pObj, 0, 0, height, color);
    ST7735_DrawVLine(pObj, width - 1, 0, height, color);
}

void ST7735_DrawChar(ST7735_Object_t *pObj, uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t size)
{
    uint32_t width = 0, height = 0;
    ST7735_GetXSize(pObj, &width);
    ST7735_GetYSize(pObj, &height);

    if (x >= width || y >= height) return;
    if ((uint8_t)c > 127) c = '?';

    const unsigned char *glyph = &font[((uint8_t)c) * 5];

    for (uint8_t col = 0; col < 6; col++)
    {
        uint8_t line = (col < 5) ? glyph[col] : 0x00;
        for (uint8_t r = 0; r < 8; r++)
        {
            uint16_t color = (line & (1 << r)) ? fg : bg;
            if (color != bg || bg != fg)
            {
                if (size == 1)
                {
                    ST7735_SetPixel(pObj, x + col, y + r, color);
                }
                else
                {
                    ST7735_FillRect(pObj, x + col * size, y + r * size, size, size, color);
                }
            }
        }
    }
}

void ST7735_DrawString(ST7735_Object_t *pObj, uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t size)
{
    uint16_t cur_x = x;
    uint16_t cur_y = y;

    while (*str)
    {
        if (*str == '\n')
        {
            cur_x = x;
            cur_y += 8 * size + 2;
        }
        else if (*str == '\r')
        {
            /* Carriage return ignored */
        }
        else
        {
            ST7735_DrawChar(pObj, cur_x, cur_y, *str, fg, bg, size);
            cur_x += 6 * size;
        }
        str++;
    }
}
