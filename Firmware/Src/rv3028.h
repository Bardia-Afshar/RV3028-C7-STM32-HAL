/*************************************************************************************
 Title   :   RV-3028 RTC Driver Header File
 File    :   rv3028.h
 Author  :   Bardia Alikhan Afshar <bardia.a.afshar@gmail.com>
 Date    :   2026-06-04
**************************************************************************************/
#ifndef RV3028_H
#define RV3028_H

/*======================================================================================================
 =                                              Includes                                                =
 ======================================================================================================*/

#include <stdint.h>
#include <stddef.h>
#include "rv3028_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/*======================================================================================================
 =                                           Public types                                               =
 ======================================================================================================*/

/*--------------------------------------------------------------------------------------------------------
 | Date and time structure containing second, minute, hour, day of week, date, month, year, and AM/PM
 ---------------------------------------------------------------------------------------------------------*/
typedef struct
{
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t weekday;
    uint8_t date;
    uint8_t month;
    uint8_t year;
    uint8_t ampm;
} RV3028_DateTime;

/*--------------------------------------------------------------------------------------------------------
 | Alarm configuration structure containing match criteria for minutes, hours, weekday/date, and enable flags
 ---------------------------------------------------------------------------------------------------------*/
typedef struct
{
    uint8_t minutes;
    uint8_t hours;
    uint8_t weekday_date;
    uint8_t enable_minutes_match;
    uint8_t enable_hours_match;
    uint8_t enable_weekday_date_match;
    uint8_t ampm;
} RV3028_Alarm;

/*--------------------------------------------------------------------------------------------------------
 | Clock output frequency selection enumeration for CLKOUT pin configurations
 ---------------------------------------------------------------------------------------------------------*/
typedef enum
{
    RV3028_CLKOUT_32768HZ = 0,
    RV3028_CLKOUT_8192HZ,
    RV3028_CLKOUT_1024HZ,
    RV3028_CLKOUT_64HZ,
    RV3028_CLKOUT_32HZ,
    RV3028_CLKOUT_1HZ,
    RV3028_CLKOUT_TIMER,
    RV3028_CLKOUT_LOW
} RV3028_ClkoutFreq;

/*--------------------------------------------------------------------------------------------------------
 | Timer clock source selection enumeration for configurable timer frequencies
 ---------------------------------------------------------------------------------------------------------*/
typedef enum
{
    RV3028_TIMER_4096HZ = 0,
    RV3028_TIMER_64HZ,
    RV3028_TIMER_1HZ,
    RV3028_TIMER_1_60HZ
} RV3028_TimerClkSrc;

/*--------------------------------------------------------------------------------------------------------
 | Backup supply mode enumeration for battery backup operation configurations
 ---------------------------------------------------------------------------------------------------------*/
typedef enum
{
    RV3028_BSM_DISABLED = 0,
    RV3028_BSM_DIRECT =   1,
    RV3028_BSM_LEVEL =    3
} RV3028_BackupMode;

/*--------------------------------------------------------------------------------------------------------
 | Trickle charge resistor selection enumeration for backup battery charging control
 ---------------------------------------------------------------------------------------------------------*/
typedef enum
{
    RV3028_TCR_3K =  0,
    RV3028_TCR_5K =  1,
    RV3028_TCR_9K =  2,
    RV3028_TCR_15K = 3
} RV3028_TrickleResistor;

/*======================================================================================================
 =                                        Public constants                                              =
 ======================================================================================================*/

#define RV3028_OK   (0)
#define RV3028_ERR  (-1)

/*======================================================================================================
 =                                        Public functions                                              =
 ======================================================================================================*/

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_Init
 | @brief   Initializes the I2C layer and clears all stale interrupt flags on first call only.
 |          Subsequent calls are a no-op and return RV3028_OK immediately.
 |
 | @param   i2cHandle  HAL I2C handle for all RTC communication.
 |
 | @return  RV3028_OK on success, RV3028_ERR on I2C failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_Init(void *i2cHandle);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_DeInit
 | @brief   Resets the internal driverInitialized flag so RV3028_Init will re-run I2C setup on next call.
 ---------------------------------------------------------------------------------------------------------*/
void RV3028_DeInit(void);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetDateTime
 | @brief   BCD-encodes and writes all 7 time/date registers; sets AM/PM bit when ampm is non-zero.
 |
 | @param   dateTime  Pointer to a RV3028_DateTime struct with values to program.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or I2C write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetDateTime(const RV3028_DateTime *dateTime);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetDateTime
 | @brief   Reads and BCD-decodes all 7 time/date registers; sets ampm from bit 5 of hours
 |          in 12-hour mode, 0 in 24-hour mode.
 |
 | @param   dateTime  Pointer to a RV3028_DateTime struct where decoded values are stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or any I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetDateTime(RV3028_DateTime *dateTime);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_Set12_24Mode
 | @brief   Sets the 12/24-hour mode bit in CTRL2.
 |
 | @param   use24h  Non-zero for 24-hour mode, zero for 12-hour mode.
 |
 | @return  RV3028_OK on success, RV3028_ERR on I2C read-modify-write failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_Set12_24Mode(uint8_t use24h);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_Get12_24Mode
 | @brief   Reads the 12/24-hour mode bit from CTRL2.
 |
 | @param   use24h  Pointer to a uint8_t set to 1 for 24-hour mode, 0 for 12-hour mode.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_Get12_24Mode(uint8_t *use24h);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetUnixTime
 | @brief   Writes a 32-bit UNIX timestamp (little-endian); disables and restores the countdown
 |          timer during the write.
 |
 | @param   unixTime  32-bit UNIX timestamp (seconds since 1970-01-01 00:00:00 UTC).
 |
 | @return  RV3028_OK on success, RV3028_ERR on any I2C failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetUnixTime(uint32_t unixTime);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetUnixTime
 | @brief   Reads 4 UNIX time registers and reconstructs the 32-bit timestamp (little-endian).
 |
 | @param   unixTime  Pointer to a uint32_t where the UNIX timestamp is stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetUnixTime(uint32_t *unixTime);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetAlarm
 | @brief   BCD-encodes alarm fields, applies AE bits per enable flags, writes 3 alarm registers,
 |          and sets WADA in CTRL1 for date vs. weekday matching.
 |
 | @param   alarm    Pointer to a RV3028_Alarm struct with match values, enable flags, and ampm
 |                   (non-zero for PM in 12-hour mode; ignored in 24-hour mode).
 | @param   useDate  Non-zero to match day-of-month, zero to match weekday.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or any I2C write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetAlarm(const RV3028_Alarm *alarm, uint8_t useDate);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EnableAlarmInterrupt
 | @brief   Sets or clears the AIE bit in CTRL2.
 |
 | @param   enable  Non-zero to enable the alarm interrupt, zero to disable.
 |
 | @return  RV3028_OK on success, RV3028_ERR on I2C read-modify-write failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EnableAlarmInterrupt(uint8_t enable);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetAlarmFlag
 | @brief   Reads the AF bit from the STATUS register.
 |
 | @param   flag  Pointer to a uint8_t set to 1 if the alarm has fired, 0 otherwise.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetAlarmFlag(uint8_t *flag);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ClearAlarmFlag
 | @brief   Clears the AF bit in STATUS via read-modify-write.
 |
 | @return  RV3028_OK on success, RV3028_ERR on I2C read-modify-write failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ClearAlarmFlag(void);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetCountdownTimer
 | @brief   Writes the 12-bit reload value and configures clock source, repeat, and enable in CTRL1.
 |
 | @param   value   12-bit countdown reload value (0-4095).
 | @param   clkSrc  Clock source for the countdown timer.
 | @param   repeat  Non-zero to auto-reload on expiry (TRPT bit).
 | @param   enable  Non-zero to start the timer immediately (TE bit).
 |
 | @return  RV3028_OK on success, RV3028_ERR on any I2C write failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetCountdownTimer(uint16_t value, RV3028_TimerClkSrc clkSrc, uint8_t repeat, uint8_t enable);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetCountdownTimer
 | @brief   Reads back the 12-bit reload value from TIMER_VALUE0 and TIMER_VALUE1 registers.
 |
 | @param   value  Pointer to a uint16_t where the 12-bit reload value is stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetCountdownTimer(uint16_t *value);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetCountdownTimerCount
 | @brief   Reads the 12-bit remaining count from TIMER_STATUS0 and TIMER_STATUS1 registers (0x0C, 0x0D).
 |
 | @param   count  Pointer to a uint16_t where the remaining 12-bit count is stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetCountdownTimerCount(uint16_t *count);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EnableTimerInterrupt
 | @brief   Sets or clears the TIE bit in CTRL2.
 |
 | @param   enable  Non-zero to enable the timer interrupt, zero to disable.
 |
 | @return  RV3028_OK on success, RV3028_ERR on I2C read-modify-write failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EnableTimerInterrupt(uint8_t enable);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetTimerFlag
 | @brief   Reads the TF bit from the STATUS register.
 |
 | @param   flag  Pointer to a uint8_t set to 1 if the timer has expired, 0 otherwise.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetTimerFlag(uint8_t *flag);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ClearTimerFlag
 | @brief   Clears the TF bit in STATUS via read-modify-write.
 |
 | @return  RV3028_OK on success, RV3028_ERR on I2C read-modify-write failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ClearTimerFlag(void);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EnableUpdateInterrupt
 | @brief   Sets UIE in CTRL2 and USEL in CTRL1; fires once per second or once per minute.
 |
 | @param   enable    Non-zero to enable the update interrupt, zero to disable.
 | @param   onMinute  Non-zero to trigger per minute, zero to trigger per second.
 |
 | @return  RV3028_OK on success, RV3028_ERR on any I2C read-modify-write failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EnableUpdateInterrupt(uint8_t enable, uint8_t onMinute);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetUpdateFlag
 | @brief   Reads the UF bit from the STATUS register.
 |
 | @param   flag  Pointer to a uint8_t set to 1 if a periodic update event has occurred, 0 otherwise.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetUpdateFlag(uint8_t *flag);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ClearUpdateFlag
 | @brief   Clears the UF bit in STATUS via read-modify-write.
 |
 | @return  RV3028_OK on success, RV3028_ERR on I2C read-modify-write failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ClearUpdateFlag(void);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetStatus
 | @brief   Reads the raw STATUS register byte into the caller's buffer.
 |
 | @param   status  Pointer to a uint8_t where the raw STATUS value is stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetStatus(uint8_t *status);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ClearAllFlags
 | @brief   Clears AF, TF, UF, EVF, and BSF flags in a single read-modify-write on STATUS.
 |
 | @return  RV3028_OK on success, RV3028_ERR on I2C read-modify-write failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ClearAllFlags(void);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetClkout
 | @brief   Writes the CLKOUT EEPROM register and issues a refresh-all so changes take effect immediately.
 |
 | @param   freq    Output frequency for the CLKOUT pin (see RV3028_ClkoutFreq).
 | @param   enable  Non-zero to drive the CLKOUT pin, zero to disable.
 |
 | @return  RV3028_OK on success, RV3028_ERR if freq is out of range or any EEPROM/I2C operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetClkout(RV3028_ClkoutFreq freq, uint8_t enable);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EEWriteByte
 | @brief   Writes one byte to an EEPROM address using the full EERD command sequence.
 |          Intended for the user-accessible EEPROM region only. Do not write to addresses 0x35,
 |          0x36, or 0x37 (CLKOUT, OFFSET, BACKUP) directly — use RV3028_SetClkout,
 |          RV3028_SetFrequencyOffset, RV3028_ConfigureBackup, and RV3028_ConfigureTrickleCharger
 |          instead, as those functions perform safe read-modify-write operations.
 |
 | @param   eeAddr  EEPROM byte address to write.
 | @param   data    Byte value to write.
 |
 | @return  RV3028_OK on success, RV3028_ERR on any I2C or EEPROM timeout failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EEWriteByte(uint8_t eeAddr, uint8_t data);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EEReadByte
 | @brief   Reads one byte from an EEPROM address using the full EERD command sequence.
 |          Intended for the user-accessible EEPROM region only. Do not write to addresses 0x35,
 |          0x36, or 0x37 (CLKOUT, OFFSET, BACKUP) using RV3028_EEWriteByte directly — use
 |          RV3028_SetClkout, RV3028_SetFrequencyOffset, RV3028_ConfigureBackup, and
 |          RV3028_ConfigureTrickleCharger instead.
 |
 | @param   eeAddr  EEPROM byte address to read.
 | @param   data    Pointer to a uint8_t where the read byte is stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or any EEPROM/I2C operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EEReadByte(uint8_t eeAddr, uint8_t *data);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EEUpdateAll
 | @brief   Copies all active shadow registers to EEPROM using the update-all command.
 |
 | @return  RV3028_OK on success, RV3028_ERR on any EEPROM/I2C failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EEUpdateAll(void);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ConfigureBackup
 | @brief   Writes the BSM field and BSIE bit to the EEPROM BACKUP register.
 |
 | @param   mode        Backup switchover mode: disabled, direct, or level-switching.
 | @param   bsieEnable  Non-zero to interrupt on backup switchover, zero to suppress.
 |
 | @return  RV3028_OK on success, RV3028_ERR if mode is invalid or any EEPROM/I2C operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ConfigureBackup(RV3028_BackupMode mode, uint8_t bsieEnable);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ConfigureTrickleCharger
 | @brief   Writes the TCE bit and TCR field to the EEPROM BACKUP register.
 |
 | @param   enable    Non-zero to enable the trickle charger, zero to disable.
 | @param   resistor  Series resistor for the charge path (3 kΩ, 5 kΩ, 9 kΩ, or 15 kΩ).
 |
 | @return  RV3028_OK on success, RV3028_ERR if resistor is out of range or any EEPROM/I2C fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ConfigureTrickleCharger(uint8_t enable, RV3028_TrickleResistor resistor);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetTimeStamp
 | @brief   Reads the EVI event counter and 6 timestamp registers; decodes into a RV3028_DateTime struct.
 |          weekday is always 0 (not captured by hardware).
 |
 | @param   timeStamp  Pointer to a RV3028_DateTime struct where the decoded timestamp is stored.
 | @param   count      Pointer to a uint8_t for the number of EVI events since last read.
 |
 | @return  RV3028_OK on success, RV3028_ERR if either pointer is NULL or any I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetTimeStamp(RV3028_DateTime *timeStamp, uint8_t *count);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EnableTimeStamp
 | @brief   Sets TSE in CTRL2 and TSOW in EVENT_CTRL to configure timestamp capture.
 |
 | @param   enable        Non-zero to enable timestamp on EVI pin events, zero to disable.
 | @param   overwriteOld  Non-zero to overwrite on overflow, zero to stop when full.
 |
 | @return  RV3028_OK on success, RV3028_ERR on any I2C read-modify-write failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EnableTimeStamp(uint8_t enable, uint8_t overwriteOld);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetEventFlag
 | @brief   Reads the EVF bit from the STATUS register.
 |
 | @param   flag  Pointer to a uint8_t set to 1 if an EVI event has occurred, 0 otherwise.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetEventFlag(uint8_t *flag);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ClearTimeStampFlag
 | @brief   Clears the EVF bit in STATUS via read-modify-write.
 |
 | @return  RV3028_OK on success, RV3028_ERR on I2C read-modify-write failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ClearTimeStampFlag(void);

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetFrequencyOffset
 | @brief   Programs a 9-bit signed frequency correction to EEPROM_OFFSET and BACKUP,
 |          clamped to [-256, +255]. The two EEPROM writes are non-atomic; if the second write
 |          fails the registers are left inconsistent and the caller should retry the full call.
 |
 | @param   offset  Signed frequency correction value, clamped to [-256, +255].
 |
 | @return  RV3028_OK on success, RV3028_ERR on any EEPROM/I2C failure.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetFrequencyOffset(int16_t offset);

#ifdef __cplusplus
}
#endif

#endif /* RV3028_H */

