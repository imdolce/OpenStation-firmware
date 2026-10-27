/**
  ******************************************************************************
  * @file    st7735_reg.c
  * @author  MCD Application Team
  * @brief   This file includes the LCD driver for st7735 LCD.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2018 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "st7735_reg.h"

/** @addtogroup BSP
  * @{
  */

/** @addtogroup Components
  * @{
  */

/** @addtogroup ST7735_REG
  * @{
  */

/** @addtogroup ST7735_REG_Exported_Functions
  * @{
  */

/**
  * @brief  Read ST7735 register
  * @param  ctx   Component context
  * @param  reg   Register to read
  * @param  pdata data to read from the register
  * @retval Component status
  */
int32_t st7735_read_reg(st7735_ctx_t *ctx, uint8_t reg, uint8_t *pdata)
{
  return ctx->ReadReg(ctx->handle, reg, pdata);
}

/**
  * @brief  Write ST7735 register
  * @param  ctx    Component context
  * @param  reg    Register to write
  * @param  pdata  data to write to the register
  * @param  length length of data to write to the register
  * @retval Component status
  */
int32_t st7735_write_reg(st7735_ctx_t *ctx, uint8_t reg, uint8_t *pdata, uint32_t length)
{
  return ctx->WriteReg(ctx->handle, reg, pdata, length);
}

/**
  * @brief  Send data
  * @param  ctx    Component context
  * @param  pdata  data to write
  * @param  length length of data to write
  * @retval Component status
  */
int32_t st7735_send_data(st7735_ctx_t *ctx, uint8_t *pdata, uint32_t length)
{
  return ctx->SendData(ctx->handle, pdata, length);
}

/**
  * @brief  Receive data
  * @param  ctx    Component context
  * @param  pdata  data to read
  * @param  length length of data to read
  * @retval Component status
  */
int32_t st7735_recv_data(st7735_ctx_t *ctx, uint8_t *pdata, uint32_t length)
{
  return ctx->RecvData(ctx->handle, pdata, length);
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

