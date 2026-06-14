/*************************************************************************************
 Title   :   RV-3028 I2C Abstraction Interface
 File    :   rv3028_i2c.h
 Author  :   Bardia Alikhan Afshar <bardia.a.afshar@gmail.com>
 Date    :   2026-06-04
**************************************************************************************/
#ifndef RV3028_I2C_H
#define RV3028_I2C_H

/*======================================================================================================
 =                                              Includes                                                =
 ======================================================================================================*/

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*======================================================================================================
 =                                        Public functions                                              =
 ======================================================================================================*/

 /* Initializes the I2C interface for the RV-3028 RTC */
void RV3028_I2C_Init(void *handle);

/* Writes data to the specified register of the RV-3028 RTC */
int8_t RV3028_I2C_Write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);

/* Reads data from the specified register of the RV-3028 RTC */
int8_t RV3028_I2C_Read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);

/* Gets the current tick value */
uint32_t RV3028_GetTick(void);

/* Delays execution for the specified number of milliseconds */
void RV3028_Delay(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* RV3028_I2C_H */
