/*************************************************************************************
 Title   :   RV-3028 Platform-Agnostic I2C Abstraction Implementation
 File    :   rv3028_i2c.c
 Author  :   Bardia Alikhan Afshar <bardia.a.afshar@gmail.com>
 Date    :   2026-06-04
*************************************************************************************/

/*======================================================================================================
 =                                              Includes                                                =
 ======================================================================================================*/

#include "rv3028_i2c.h"
#include "stm32g4xx_hal.h"

/*======================================================================================================
 =                                       Private constants                                              =
 ======================================================================================================*/

#define RV3028_I2C_TIMEOUT_MS    (100u)

/*======================================================================================================
 =                                       Private variables                                              =
 ======================================================================================================*/

static void *i2cHandle = (void *)0;

/*======================================================================================================
 =                                        Public functions                                              =
 ======================================================================================================*/

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_I2C_Init
 | @brief   Stores a platform-specific I2C handle pointer.
 |
 | @param   handle Platform-specific I2C peripheral handle.
 |
 | @return  None
 ---------------------------------------------------------------------------------------------------------*/
void RV3028_I2C_Init(void *handle)
{
    i2cHandle = handle;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_I2C_Write
 | @brief   Platform hook for writing one or more bytes to RV-3028.
 |
 | @param   dev_addr 8-bit device address.
 | @param   reg_addr Register address.
 | @param   data Pointer to write buffer.
 | @param   len Number of bytes to write.
 |
 | @return  0 on success, non-zero on error.
 ---------------------------------------------------------------------------------------------------------*/
int8_t RV3028_I2C_Write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    I2C_HandleTypeDef *halI2cHandle;

    if ((i2cHandle == (void *)0) || (data == (void *)0) || (len == 0u))
    {
        return -1;
    }

    halI2cHandle = (I2C_HandleTypeDef *)i2cHandle;

    if (HAL_I2C_Mem_Write(halI2cHandle,
                          dev_addr,
                          reg_addr,
                          I2C_MEMADD_SIZE_8BIT,
                          data,
                          len,
                          RV3028_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return -1;
    }

    return 0;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_I2C_Read
 | @brief   Platform hook for reading one or more bytes from RV-3028.
 |
 | @param   dev_addr 8-bit device address.
 | @param   reg_addr Register address.
 | @param   data Pointer to read buffer.
 | @param   len Number of bytes to read.
 |
 | @return  0 on success, non-zero on error.
 ---------------------------------------------------------------------------------------------------------*/
int8_t RV3028_I2C_Read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    I2C_HandleTypeDef *halI2cHandle;

    if ((i2cHandle == (void *)0) || (data == (void *)0) || (len == 0u))
    {
        return -1;
    }

    halI2cHandle = (I2C_HandleTypeDef *)i2cHandle;

    if (HAL_I2C_Mem_Read(halI2cHandle,
                         dev_addr,
                         reg_addr,
                         I2C_MEMADD_SIZE_8BIT,
                         data,
                         len,
                         RV3028_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return -1;
    }

    return 0;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetTick
 | @brief   Platform hook returning a millisecond tick counter.
 |
 | @return  Current tick in milliseconds.
 ---------------------------------------------------------------------------------------------------------*/
uint32_t RV3028_GetTick(void)
{
    return HAL_GetTick();
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_Delay
 | @brief   Platform hook for blocking millisecond delay.
 |
 | @param   ms Delay duration in milliseconds.
 |
 | @return  None
 ---------------------------------------------------------------------------------------------------------*/
void RV3028_Delay(uint32_t ms)
{
    HAL_Delay(ms);
}
