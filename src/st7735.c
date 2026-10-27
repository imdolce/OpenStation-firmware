/**
  ******************************************************************************
  * @file    st7735.c
  * @author  MCD Application Team / Ported and Corrected for CH32X033
  * @brief   ST7735 LCD component driver with corrected command framing,
  *          full CASET/RASET address windows, and robust bounds checking.
  ******************************************************************************
  */

#include "st7735.h"

typedef struct
{
  uint8_t X_Offset;
  uint8_t Y_Offset;
} ST7735_OrientationOffset_t;

/* Orientation-specific hardware display offsets:
 * ST7735 internal RAM is 132x162, visible glass is 128x160.
 * In Portrait (128x160, MADCTL 0xC0): unused columns=4 (offset 2), unused rows=2 (offset 1).
 * In Landscape (160x128, MADCTL 0x60): unused columns=2 (offset 1), unused rows=4 (offset 2). */
static ST7735_OrientationOffset_t OrientationOffsets[4] =
{
  {2, 1},  /* ST7735_ORIENTATION_PORTRAIT (128x160) */
  {2, 1},  /* ST7735_ORIENTATION_PORTRAIT_ROT180 (128x160) */
  {1, 2},  /* ST7735_ORIENTATION_LANDSCAPE (160x128) */
  {1, 2}   /* ST7735_ORIENTATION_LANDSCAPE_ROT180 (160x128) */
};

typedef struct
{
  uint32_t        Width;
  uint32_t        Height;
  uint32_t        Orientation;
  uint8_t         X_Offset;
  uint8_t         Y_Offset;
} ST7735_Ctx_t;

static ST7735_Ctx_t ST7735Ctx = {
  .Width       = ST7735_WIDTH,
  .Height      = ST7735_HEIGHT,
  .Orientation = ST7735_ORIENTATION_PORTRAIT,
  .X_Offset    = 2,
  .Y_Offset    = 1
};

/* MADCTL values: [Orientation][1] holds standard scan order */
static uint32_t OrientationTab[4][2] =
{
  {0x40U, 0xC0U},  /* Portrait (128x160) */
  {0x80U, 0x00U},  /* Portrait rot180 (128x160) */
  {0x20U, 0x60U},  /* Landscape (160x128) */
  {0xE0U, 0xA0U}   /* Landscape rot180 (160x128) */
};

/* Forward Declarations */
static int32_t ST7735_ReadRegWrap(void *Handle, uint8_t Reg, uint8_t *pData);
static int32_t ST7735_WriteRegWrap(void *Handle, uint8_t Reg, uint8_t *pData, uint32_t Length);
static int32_t ST7735_SendDataWrap(void *Handle, uint8_t *pData, uint32_t Length);
static int32_t ST7735_RecvDataWrap(void *Handle, uint8_t *pData, uint32_t Length);
static int32_t ST7735_IO_Delay(ST7735_Object_t *pObj, uint32_t Delay);

ST7735_LCD_Drv_t ST7735_LCD_Driver =
{
  ST7735_Init,
  ST7735_DeInit,
  ST7735_ReadID,
  ST7735_DisplayOn,
  ST7735_DisplayOff,
  ST7735_SetBrightness,
  ST7735_GetBrightness,
  ST7735_SetOrientation,
  ST7735_GetOrientation,
  ST7735_SetCursor,
  ST7735_DrawBitmap,
  ST7735_FillRGBRect,
  ST7735_DrawHLine,
  ST7735_DrawVLine,
  ST7735_FillRect,
  ST7735_GetPixel,
  ST7735_SetPixel,
  ST7735_GetXSize,
  ST7735_GetYSize,
};

int32_t ST7735_RegisterBusIO(ST7735_Object_t *pObj, ST7735_IO_t *pIO)
{
  if (pObj == NULL || pIO == NULL)
  {
    return ST7735_ERROR;
  }

  pObj->IO.Init      = pIO->Init;
  pObj->IO.DeInit    = pIO->DeInit;
  pObj->IO.Address   = pIO->Address;
  pObj->IO.WriteReg  = pIO->WriteReg;
  pObj->IO.ReadReg   = pIO->ReadReg;
  pObj->IO.SendData  = pIO->SendData;
  pObj->IO.RecvData  = pIO->RecvData;
  pObj->IO.GetTick   = pIO->GetTick;

  pObj->Ctx.ReadReg   = ST7735_ReadRegWrap;
  pObj->Ctx.WriteReg  = ST7735_WriteRegWrap;
  pObj->Ctx.SendData  = ST7735_SendDataWrap;
  pObj->Ctx.RecvData  = ST7735_RecvDataWrap;
  pObj->Ctx.handle    = pObj;

  if (pObj->IO.Init != NULL)
  {
    return pObj->IO.Init();
  }

  return ST7735_ERROR;
}

int32_t ST7735_Init(ST7735_Object_t *pObj, uint32_t ColorCoding, uint32_t Orientation)
{
  if (pObj == NULL) return ST7735_ERROR;

  /* 1. SLPOUT: Exit Sleep Mode (0 parameters, no dummy byte) */
  if (st7735_write_reg(&pObj->Ctx, ST7735_SLEEP_OUT, NULL, 0) != ST7735_OK) return ST7735_ERROR;
  (void)ST7735_IO_Delay(pObj, 120); /* Booster circuit stabilization delay */

  /* 2. FRMCTR1: Normal mode frame rate (3 params) */
  static const uint8_t frmctr1_data[] = { 0x01, 0x2C, 0x2D };
  if (st7735_write_reg(&pObj->Ctx, ST7735_FRAME_RATE_CTRL1, (uint8_t *)frmctr1_data, 3) != ST7735_OK) return ST7735_ERROR;

  /* 3. FRMCTR2: Idle mode frame rate (3 params) */
  static const uint8_t frmctr2_data[] = { 0x01, 0x2C, 0x2D };
  if (st7735_write_reg(&pObj->Ctx, ST7735_FRAME_RATE_CTRL2, (uint8_t *)frmctr2_data, 3) != ST7735_OK) return ST7735_ERROR;

  /* 4. FRMCTR3: Partial mode frame rate (6 params) */
  static const uint8_t frmctr3_data[] = { 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D };
  if (st7735_write_reg(&pObj->Ctx, ST7735_FRAME_RATE_CTRL3, (uint8_t *)frmctr3_data, 6) != ST7735_OK) return ST7735_ERROR;

  /* 5. INVCTR: Display Inversion Control (1 param) */
  static const uint8_t invctr_data[] = { 0x07 };
  if (st7735_write_reg(&pObj->Ctx, ST7735_FRAME_INVERSION_CTRL, (uint8_t *)invctr_data, 1) != ST7735_OK) return ST7735_ERROR;

  /* 6. PWCTR1: Power control 1 (3 params) */
  static const uint8_t pwctr1_data[] = { 0xA2, 0x02, 0x84 };
  if (st7735_write_reg(&pObj->Ctx, ST7735_PWR_CTRL1, (uint8_t *)pwctr1_data, 3) != ST7735_OK) return ST7735_ERROR;

  /* 7. PWCTR2: Power control 2 (1 param) */
  static const uint8_t pwctr2_data[] = { 0xC5 };
  if (st7735_write_reg(&pObj->Ctx, ST7735_PWR_CTRL2, (uint8_t *)pwctr2_data, 1) != ST7735_OK) return ST7735_ERROR;

  /* 8. PWCTR3: Power control 3 (2 params) */
  static const uint8_t pwctr3_data[] = { 0x0A, 0x00 };
  if (st7735_write_reg(&pObj->Ctx, ST7735_PWR_CTRL3, (uint8_t *)pwctr3_data, 2) != ST7735_OK) return ST7735_ERROR;

  /* 9. PWCTR4: Power control 4 (2 params) */
  static const uint8_t pwctr4_data[] = { 0x8A, 0x2A };
  if (st7735_write_reg(&pObj->Ctx, ST7735_PWR_CTRL4, (uint8_t *)pwctr4_data, 2) != ST7735_OK) return ST7735_ERROR;

  /* 10. PWCTR5: Power control 5 (2 params) */
  static const uint8_t pwctr5_data[] = { 0x8A, 0xEE };
  if (st7735_write_reg(&pObj->Ctx, ST7735_PWR_CTRL5, (uint8_t *)pwctr5_data, 2) != ST7735_OK) return ST7735_ERROR;

  /* 11. VMCTR1: VCOM control (1 param) */
  static const uint8_t vmctr1_data[] = { 0x0E };
  if (st7735_write_reg(&pObj->Ctx, ST7735_VCOMH_VCOML_CTRL1, (uint8_t *)vmctr1_data, 1) != ST7735_OK) return ST7735_ERROR;

  /* 12. INVOFF: Inversion off (0 params, no dummy byte) */
  if (st7735_write_reg(&pObj->Ctx, ST7735_DISPLAY_INVERSION_OFF, NULL, 0) != ST7735_OK) return ST7735_ERROR;

  /* 13. COLMOD: Interface pixel format (1 param) */
  uint8_t colmod = (uint8_t)ColorCoding;
  if (st7735_write_reg(&pObj->Ctx, ST7735_COLOR_MODE, &colmod, 1) != ST7735_OK) return ST7735_ERROR;

  /* 14. GAMCTRP1: Positive Gamma correction (16 params) */
  static const uint8_t gamma_p[] = {
    0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
    0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10
  };
  if (st7735_write_reg(&pObj->Ctx, ST7735_PV_GAMMA_CTRL, (uint8_t *)gamma_p, 16) != ST7735_OK) return ST7735_ERROR;

  /* 15. GAMCTRN1: Negative Gamma correction (16 params) */
  static const uint8_t gamma_n[] = {
    0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
    0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10
  };
  if (st7735_write_reg(&pObj->Ctx, ST7735_NV_GAMMA_CTRL, (uint8_t *)gamma_n, 16) != ST7735_OK) return ST7735_ERROR;

  /* 16. NORON: Normal Display Mode On (0 params) */
  if (st7735_write_reg(&pObj->Ctx, ST7735_NORMAL_DISPLAY_OFF, NULL, 0) != ST7735_OK) return ST7735_ERROR;
  (void)ST7735_IO_Delay(pObj, 10);

  /* 17. DISPON: Display On (0 params) */
  if (st7735_write_reg(&pObj->Ctx, ST7735_DISPLAY_ON, NULL, 0) != ST7735_OK) return ST7735_ERROR;
  (void)ST7735_IO_Delay(pObj, 100);

  /* 18. Set Orientation */
  if (ST7735_SetOrientation(pObj, Orientation) != ST7735_OK) return ST7735_ERROR;

  pObj->IsInitialized = 1;
  return ST7735_OK;
}

int32_t ST7735_DeInit(ST7735_Object_t *pObj)
{
  if (pObj == NULL) return ST7735_ERROR;

  if (pObj->IsInitialized != 0)
  {
    (void)ST7735_DisplayOff(pObj);
    pObj->IsInitialized = 0;
    if (pObj->IO.DeInit != NULL)
    {
      return pObj->IO.DeInit();
    }
  }

  return ST7735_OK;
}

int32_t ST7735_ReadID(ST7735_Object_t *pObj, uint32_t *Id)
{
  (void)pObj;
  (void)Id;
  return ST7735_ERROR;
}

int32_t ST7735_DisplayOn(ST7735_Object_t *pObj)
{
  if (pObj == NULL) return ST7735_ERROR;
  return st7735_write_reg(&pObj->Ctx, ST7735_DISPLAY_ON, NULL, 0);
}

int32_t ST7735_DisplayOff(ST7735_Object_t *pObj)
{
  if (pObj == NULL) return ST7735_ERROR;
  return st7735_write_reg(&pObj->Ctx, ST7735_DISPLAY_OFF, NULL, 0);
}

int32_t ST7735_SetBrightness(ST7735_Object_t *pObj, uint32_t Brightness)
{
  (void)pObj;
  (void)Brightness;
  return ST7735_ERROR;
}

int32_t ST7735_GetBrightness(ST7735_Object_t *pObj, uint32_t *Brightness)
{
  (void)pObj;
  (void)Brightness;
  return ST7735_ERROR;
}

int32_t ST7735_SetOrientation(ST7735_Object_t *pObj, uint32_t Orientation)
{
  if (pObj == NULL) return ST7735_ERROR;
  if (Orientation > ST7735_ORIENTATION_LANDSCAPE_ROT180) return ST7735_ERROR;

  if ((Orientation == ST7735_ORIENTATION_PORTRAIT) || (Orientation == ST7735_ORIENTATION_PORTRAIT_ROT180))
  {
    ST7735Ctx.Width  = ST7735_WIDTH;
    ST7735Ctx.Height = ST7735_HEIGHT;
  }
  else
  {
    ST7735Ctx.Width  = ST7735_HEIGHT;
    ST7735Ctx.Height = ST7735_WIDTH;
  }

  uint8_t madctl = (uint8_t)OrientationTab[Orientation][1];
  if (st7735_write_reg(&pObj->Ctx, ST7735_MADCTL, &madctl, 1) != ST7735_OK) return ST7735_ERROR;

  ST7735Ctx.Orientation = Orientation;
  ST7735Ctx.X_Offset    = OrientationOffsets[Orientation].X_Offset;
  ST7735Ctx.Y_Offset    = OrientationOffsets[Orientation].Y_Offset;
  return ST7735_OK;
}

int32_t ST7735_GetOrientation(ST7735_Object_t *pObj, uint32_t *Orientation)
{
  (void)pObj;
  if (Orientation == NULL) return ST7735_ERROR;
  *Orientation = ST7735Ctx.Orientation;
  return ST7735_OK;
}

int32_t ST7735_ClearRawRAM(ST7735_Object_t *pObj, uint16_t color)
{
  if (pObj == NULL) return ST7735_ERROR;

  /* Address the complete 132 x 162 ST7735 controller frame memory */
  uint8_t col_params[4] = { 0x00, 0x00, 0x00, 131 };
  uint8_t row_params[4] = { 0x00, 0x00, 0x00, 161 };

  if (st7735_write_reg(&pObj->Ctx, ST7735_CASET, col_params, 4) != ST7735_OK) return ST7735_ERROR;
  if (st7735_write_reg(&pObj->Ctx, ST7735_RASET, row_params, 4) != ST7735_OK) return ST7735_ERROR;
  if (st7735_write_reg(&pObj->Ctx, ST7735_WRITE_RAM, NULL, 0) != ST7735_OK) return ST7735_ERROR;

  uint8_t pixel[2] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
  uint8_t buf[128];
  for (uint32_t i = 0; i < 64; i++)
  {
    buf[i * 2]     = pixel[0];
    buf[i * 2 + 1] = pixel[1];
  }

  uint32_t total = 132 * 162;
  while (total > 0)
  {
    uint32_t chunk = (total > 64) ? 64 : total;
    if (st7735_send_data(&pObj->Ctx, buf, chunk * 2) != ST7735_OK) return ST7735_ERROR;
    total -= chunk;
  }

  return ST7735_OK;
}

void ST7735_SetOffsets(ST7735_Object_t *pObj, uint8_t X_Offset, uint8_t Y_Offset)
{
  (void)pObj;
  ST7735Ctx.X_Offset = X_Offset;
  ST7735Ctx.Y_Offset = Y_Offset;
  if (ST7735Ctx.Orientation <= ST7735_ORIENTATION_LANDSCAPE_ROT180)
  {
    OrientationOffsets[ST7735Ctx.Orientation].X_Offset = X_Offset;
    OrientationOffsets[ST7735Ctx.Orientation].Y_Offset = Y_Offset;
  }
}

void ST7735_GetOffsets(ST7735_Object_t *pObj, uint8_t *X_Offset, uint8_t *Y_Offset)
{
  (void)pObj;
  if (X_Offset != NULL) *X_Offset = ST7735Ctx.X_Offset;
  if (Y_Offset != NULL) *Y_Offset = ST7735Ctx.Y_Offset;
}

int32_t ST7735_SetDisplayWindow(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t Width, uint32_t Height)
{
  if (pObj == NULL || Width == 0 || Height == 0) return ST7735_ERROR;
  if (Xpos >= ST7735Ctx.Width || Ypos >= ST7735Ctx.Height) return ST7735_ERROR;
  if (Width > (ST7735Ctx.Width - Xpos) || Height > (ST7735Ctx.Height - Ypos)) return ST7735_ERROR;

  uint32_t x0 = Xpos + ST7735Ctx.X_Offset;
  uint32_t x1 = Xpos + Width - 1 + ST7735Ctx.X_Offset;
  uint32_t y0 = Ypos + ST7735Ctx.Y_Offset;
  uint32_t y1 = Ypos + Height - 1 + ST7735Ctx.Y_Offset;

  uint8_t col_params[4] = {
    (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
    (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
  };
  uint8_t row_params[4] = {
    (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
    (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)
  };

  /* Send CASET with all 4 bytes */
  if (st7735_write_reg(&pObj->Ctx, ST7735_CASET, col_params, 4) != ST7735_OK) return ST7735_ERROR;
  /* Send RASET with all 4 bytes */
  if (st7735_write_reg(&pObj->Ctx, ST7735_RASET, row_params, 4) != ST7735_OK) return ST7735_ERROR;
  /* Send RAMWR to begin writing pixel RAM */
  if (st7735_write_reg(&pObj->Ctx, ST7735_WRITE_RAM, NULL, 0) != ST7735_OK) return ST7735_ERROR;

  return ST7735_OK;
}

int32_t ST7735_SetCursor(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos)
{
  return ST7735_SetDisplayWindow(pObj, Xpos, Ypos, 1, 1);
}

int32_t ST7735_SetPixel(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t Color)
{
  if (pObj == NULL) return ST7735_ERROR;
  if (ST7735_SetDisplayWindow(pObj, Xpos, Ypos, 1, 1) != ST7735_OK) return ST7735_ERROR;

  uint8_t pixel[2] = { (uint8_t)(Color >> 8), (uint8_t)(Color & 0xFF) };
  return st7735_send_data(&pObj->Ctx, pixel, 2);
}

int32_t ST7735_GetPixel(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t *Color)
{
  (void)pObj;
  (void)Xpos;
  (void)Ypos;
  (void)Color;
  return ST7735_ERROR;
}

int32_t ST7735_DrawHLine(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t Length, uint32_t Color)
{
  if (pObj == NULL || Length == 0) return ST7735_ERROR;
  if (Xpos >= ST7735Ctx.Width || Ypos >= ST7735Ctx.Height) return ST7735_ERROR;
  if (Length > (ST7735Ctx.Width - Xpos)) return ST7735_ERROR;

  if (ST7735_SetDisplayWindow(pObj, Xpos, Ypos, Length, 1) != ST7735_OK) return ST7735_ERROR;

  uint8_t pixel[2] = { (uint8_t)(Color >> 8), (uint8_t)(Color & 0xFF) };
  uint8_t line_buf[64];
  for (uint32_t i = 0; i < 32; i++)
  {
    line_buf[i * 2]     = pixel[0];
    line_buf[i * 2 + 1] = pixel[1];
  }

  uint32_t remaining = Length;
  while (remaining > 0)
  {
    uint32_t chunk = (remaining > 32) ? 32 : remaining;
    if (st7735_send_data(&pObj->Ctx, line_buf, chunk * 2) != ST7735_OK) return ST7735_ERROR;
    remaining -= chunk;
  }

  return ST7735_OK;
}

int32_t ST7735_DrawVLine(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t Length, uint32_t Color)
{
  if (pObj == NULL || Length == 0) return ST7735_ERROR;
  if (Xpos >= ST7735Ctx.Width || Ypos >= ST7735Ctx.Height) return ST7735_ERROR;
  if (Length > (ST7735Ctx.Height - Ypos)) return ST7735_ERROR;

  if (ST7735_SetDisplayWindow(pObj, Xpos, Ypos, 1, Length) != ST7735_OK) return ST7735_ERROR;

  uint8_t pixel[2] = { (uint8_t)(Color >> 8), (uint8_t)(Color & 0xFF) };
  uint8_t line_buf[64];
  for (uint32_t i = 0; i < 32; i++)
  {
    line_buf[i * 2]     = pixel[0];
    line_buf[i * 2 + 1] = pixel[1];
  }

  uint32_t remaining = Length;
  while (remaining > 0)
  {
    uint32_t chunk = (remaining > 32) ? 32 : remaining;
    if (st7735_send_data(&pObj->Ctx, line_buf, chunk * 2) != ST7735_OK) return ST7735_ERROR;
    remaining -= chunk;
  }

  return ST7735_OK;
}

int32_t ST7735_FillRect(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t Width, uint32_t Height, uint32_t Color)
{
  if (pObj == NULL || Width == 0 || Height == 0) return ST7735_ERROR;
  if (Xpos >= ST7735Ctx.Width || Ypos >= ST7735Ctx.Height) return ST7735_ERROR;
  if (Width > (ST7735Ctx.Width - Xpos) || Height > (ST7735Ctx.Height - Ypos)) return ST7735_ERROR;

  if (ST7735_SetDisplayWindow(pObj, Xpos, Ypos, Width, Height) != ST7735_OK) return ST7735_ERROR;

  uint8_t pixel[2] = { (uint8_t)(Color >> 8), (uint8_t)(Color & 0xFF) };
  uint8_t buf[128];
  for (uint32_t i = 0; i < 64; i++)
  {
    buf[i * 2]     = pixel[0];
    buf[i * 2 + 1] = pixel[1];
  }

  uint32_t total = Width * Height;
  while (total > 0)
  {
    uint32_t chunk = (total > 64) ? 64 : total;
    if (st7735_send_data(&pObj->Ctx, buf, chunk * 2) != ST7735_OK) return ST7735_ERROR;
    total -= chunk;
  }

  return ST7735_OK;
}

int32_t ST7735_FillRGBRect(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint8_t *pData, uint32_t Width, uint32_t Height)
{
  if (pObj == NULL || pData == NULL || Width == 0 || Height == 0) return ST7735_ERROR;
  if (Xpos >= ST7735Ctx.Width || Ypos >= ST7735Ctx.Height) return ST7735_ERROR;
  if (Width > (ST7735Ctx.Width - Xpos) || Height > (ST7735Ctx.Height - Ypos)) return ST7735_ERROR;

  if (ST7735_SetDisplayWindow(pObj, Xpos, Ypos, Width, Height) != ST7735_OK) return ST7735_ERROR;
  return st7735_send_data(&pObj->Ctx, pData, Width * Height * 2);
}

int32_t ST7735_DrawBitmap(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint8_t *pBmp)
{
  (void)pObj;
  (void)Xpos;
  (void)Ypos;
  (void)pBmp;
  return ST7735_ERROR;
}

int32_t ST7735_GetXSize(ST7735_Object_t *pObj, uint32_t *XSize)
{
  (void)pObj;
  if (XSize == NULL) return ST7735_ERROR;
  *XSize = ST7735Ctx.Width;
  return ST7735_OK;
}

int32_t ST7735_GetYSize(ST7735_Object_t *pObj, uint32_t *YSize)
{
  (void)pObj;
  if (YSize == NULL) return ST7735_ERROR;
  *YSize = ST7735Ctx.Height;
  return ST7735_OK;
}

static int32_t ST7735_ReadRegWrap(void *Handle, uint8_t Reg, uint8_t *pData)
{
  ST7735_Object_t *pObj = (ST7735_Object_t *)Handle;
  return pObj->IO.ReadReg(Reg, pData);
}

static int32_t ST7735_WriteRegWrap(void *Handle, uint8_t Reg, uint8_t *pData, uint32_t Length)
{
  ST7735_Object_t *pObj = (ST7735_Object_t *)Handle;
  return pObj->IO.WriteReg(Reg, pData, Length);
}

static int32_t ST7735_SendDataWrap(void *Handle, uint8_t *pData, uint32_t Length)
{
  ST7735_Object_t *pObj = (ST7735_Object_t *)Handle;
  return pObj->IO.SendData(pData, Length);
}

static int32_t ST7735_RecvDataWrap(void *Handle, uint8_t *pData, uint32_t Length)
{
  ST7735_Object_t *pObj = (ST7735_Object_t *)Handle;
  return pObj->IO.RecvData(pData, Length);
}

static int32_t ST7735_IO_Delay(ST7735_Object_t *pObj, uint32_t Delay)
{
  if (pObj == NULL || pObj->IO.GetTick == NULL) return ST7735_ERROR;
  uint32_t tickstart = pObj->IO.GetTick();
  while ((pObj->IO.GetTick() - tickstart) < Delay)
  {
  }
  return ST7735_OK;
}
