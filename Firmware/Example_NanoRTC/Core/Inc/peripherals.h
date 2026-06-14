/*************************************************************************************
 Title   :   Peripheral Handles for STM32 Using HAL
 File    :   peripherals.h
 Author  :   Bardia Alikhan Afshar <bardia.a.afshar@gmail.com>
 Date    :   2026-03-15
*************************************************************************************/

#ifndef __PERIPHERALS_H
#define __PERIPHERALS_H

/*======================================================================================================
 =                                              Includes                                                =
 ======================================================================================================*/

#include "stm32g4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*======================================================================================================
 =                                       Public variables                                               =
 ======================================================================================================*/

extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart2;

#ifdef __cplusplus
}
#endif

#endif /* __PERIPHERALS_H */
