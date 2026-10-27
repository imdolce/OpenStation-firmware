/**
  ******************************************************************************
  * @file    st7735.h
  * @author  MCD Application Team / Antigravity Port for CH32X033
  * @brief   This file contains all the functions prototypes for st7735.c
  ******************************************************************************
  */

#ifndef ST7735_H
#define ST7735_H

#ifdef __cplusplus
extern "C" {
#endif

#include "st7735_reg.h"
#include <stddef.h>
#include <stdint.h>

/* Exported Types ------------------------------------------------------------*/
typedef int32_t  (*ST7735_Init_Func)(void);
typedef int32_t  (*ST7735_DeInit_Func)(void);
typedef uint32_t (*ST7735_GetTick_Func)(void);
typedef int32_t  (*ST7735_Delay_Func)(uint32_t);
typedef int32_t  (*ST7735_WriteReg_Func)(uint8_t, uint8_t *, uint32_t);
typedef int32_t  (*ST7735_ReadReg_Func)(uint8_t, uint8_t *);
typedef int32_t  (*ST7735_SendData_Func)(uint8_t *, uint32_t);
typedef int32_t  (*ST7735_RecvData_Func)(uint8_t *, uint32_t);

typedef struct
{
  ST7735_Init_Func          Init;
  ST7735_DeInit_Func        DeInit;
  uint16_t                  Address;
  ST7735_WriteReg_Func      WriteReg;
  ST7735_ReadReg_Func       ReadReg;
  ST7735_SendData_Func      SendData;
  ST7735_RecvData_Func      RecvData;
  ST7735_GetTick_Func       GetTick;
} ST7735_IO_t;

typedef struct
{
  ST7735_IO_t         IO;
  st7735_ctx_t        Ctx;
  uint8_t             IsInitialized;
} ST7735_Object_t;

typedef struct
{
  /* Control functions */
  int32_t (*Init)(ST7735_Object_t *, uint32_t, uint32_t);
  int32_t (*DeInit)(ST7735_Object_t *);
  int32_t (*ReadID)(ST7735_Object_t *, uint32_t *);
  int32_t (*DisplayOn)(ST7735_Object_t *);
  int32_t (*DisplayOff)(ST7735_Object_t *);
  int32_t (*SetBrightness)(ST7735_Object_t *, uint32_t);
  int32_t (*GetBrightness)(ST7735_Object_t *, uint32_t *);
  int32_t (*SetOrientation)(ST7735_Object_t *, uint32_t);
  int32_t (*GetOrientation)(ST7735_Object_t *, uint32_t *);

  /* Drawing functions*/
  int32_t (*SetCursor)(ST7735_Object_t *, uint32_t, uint32_t);
  int32_t (*DrawBitmap)(ST7735_Object_t *, uint32_t, uint32_t, uint8_t *);
  int32_t (*FillRGBRect)(ST7735_Object_t *, uint32_t, uint32_t, uint8_t *, uint32_t, uint32_t);
  int32_t (*DrawHLine)(ST7735_Object_t *, uint32_t, uint32_t, uint32_t, uint32_t);
  int32_t (*DrawVLine)(ST7735_Object_t *, uint32_t, uint32_t, uint32_t, uint32_t);
  int32_t (*FillRect)(ST7735_Object_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
  int32_t (*GetPixel)(ST7735_Object_t *, uint32_t, uint32_t, uint32_t *);
  int32_t (*SetPixel)(ST7735_Object_t *, uint32_t, uint32_t, uint32_t);
  int32_t (*GetXSize)(ST7735_Object_t *, uint32_t *);
  int32_t (*GetYSize)(ST7735_Object_t *, uint32_t *);
} ST7735_LCD_Drv_t;

/* Exported Constants --------------------------------------------------------*/
#define ST7735_OK                (0)
#define ST7735_ERROR             (-1)

#define ST7735_ID                0x5CU

/* Native Physical Resolution: 128 x 160 */
#define ST7735_WIDTH             128U
#define ST7735_HEIGHT            160U

/* Display Orientation */
#define ST7735_ORIENTATION_PORTRAIT         0x00U /* Portrait (128x160)              */
#define ST7735_ORIENTATION_PORTRAIT_ROT180  0x01U /* Portrait rotated 180 (128x160)   */
#define ST7735_ORIENTATION_LANDSCAPE        0x02U /* Landscape (160x128)             */
#define ST7735_ORIENTATION_LANDSCAPE_ROT180 0x03U /* Landscape rotated 180 (160x128)  */

/* Pixel Format */
#define ST7735_FORMAT_RBG444                0x03U
#define ST7735_FORMAT_RBG565                0x05U
#define ST7735_FORMAT_RBG666                0x06U
#define ST7735_FORMAT_DEFAULT               ST7735_FORMAT_RBG565

/* Exported Functions --------------------------------------------------------*/
int32_t ST7735_RegisterBusIO(ST7735_Object_t *pObj, ST7735_IO_t *pIO);
int32_t ST7735_Init(ST7735_Object_t *pObj, uint32_t ColorCoding, uint32_t Orientation);
int32_t ST7735_DeInit(ST7735_Object_t *pObj);
int32_t ST7735_ReadID(ST7735_Object_t *pObj, uint32_t *Id);
int32_t ST7735_DisplayOn(ST7735_Object_t *pObj);
int32_t ST7735_DisplayOff(ST7735_Object_t *pObj);
int32_t ST7735_SetBrightness(ST7735_Object_t *pObj, uint32_t Brightness);
int32_t ST7735_GetBrightness(ST7735_Object_t *pObj, uint32_t *Brightness);
int32_t ST7735_SetOrientation(ST7735_Object_t *pObj, uint32_t Orientation);
int32_t ST7735_GetOrientation(ST7735_Object_t *pObj, uint32_t *Orientation);

int32_t ST7735_SetDisplayWindow(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t Width, uint32_t Height);
int32_t ST7735_SetCursor(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos);
int32_t ST7735_DrawBitmap(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint8_t *pBmp);
int32_t ST7735_FillRGBRect(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint8_t *pData, uint32_t Width, uint32_t Height);
int32_t ST7735_DrawHLine(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t Length, uint32_t Color);
int32_t ST7735_DrawVLine(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t Length, uint32_t Color);
int32_t ST7735_FillRect(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t Width, uint32_t Height, uint32_t Color);
int32_t ST7735_GetPixel(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t *Color);
int32_t ST7735_SetPixel(ST7735_Object_t *pObj, uint32_t Xpos, uint32_t Ypos, uint32_t Color);
int32_t ST7735_GetXSize(ST7735_Object_t *pObj, uint32_t *XSize);
int32_t ST7735_GetYSize(ST7735_Object_t *pObj, uint32_t *YSize);

void    ST7735_SetOffsets(ST7735_Object_t *pObj, uint8_t X_Offset, uint8_t Y_Offset);
void    ST7735_GetOffsets(ST7735_Object_t *pObj, uint8_t *X_Offset, uint8_t *Y_Offset);
int32_t ST7735_ClearRawRAM(ST7735_Object_t *pObj, uint16_t color);

extern ST7735_LCD_Drv_t ST7735_LCD_Driver;

#ifdef __cplusplus
}
#endif

#endif /* ST7735_H */
