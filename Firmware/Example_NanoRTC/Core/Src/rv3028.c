/*************************************************************************************
 Title   :   Micro Crystal RV-3028 RTC Driver Implementation
 File    :   rv3028.c
 Author  :   Bardia Alikhan Afshar <bardia.a.afshar@gmail.com>
 Date    :   2026-06-04
**************************************************************************************/

/*======================================================================================================
 =                                              Includes                                                =
 ======================================================================================================*/

#include <stdint.h>
#include <stddef.h>
#include "rv3028.h"

/*======================================================================================================
 =                                       Private constants                                              =
 ======================================================================================================*/

/* 7-bit address (0x52) pre-shifted to 8-bit write form for the RV3028_I2C_Write/Read HAL wrappers. */
#define RV3028_I2C_ADDR                (0xA4u)
#define RV3028_EEBUSY_TIMEOUT_MS       (500u)

#define RV3028_REG_SECONDS             (0x00u)
#define RV3028_REG_MINUTES             (0x01u)
#define RV3028_REG_HOURS               (0x02u)
#define RV3028_REG_WEEKDAY             (0x03u)
#define RV3028_REG_DATE                (0x04u)
#define RV3028_REG_MONTH               (0x05u)
#define RV3028_REG_YEAR                (0x06u)
#define RV3028_REG_MIN_ALARM           (0x07u)
#define RV3028_REG_HOUR_ALARM          (0x08u)
#define RV3028_REG_WD_DATE_ALARM       (0x09u)
#define RV3028_REG_TIMER_VALUE0        (0x0Au)
#define RV3028_REG_TIMER_VALUE1        (0x0Bu)
#define RV3028_REG_TIMER_STATUS0       (0x0Cu)
#define RV3028_REG_TIMER_STATUS1       (0x0Du)
#define RV3028_REG_STATUS              (0x0Eu)
#define RV3028_REG_CTRL1               (0x0Fu)
#define RV3028_REG_CTRL2               (0x10u)
#define RV3028_REG_EVENT_CTRL          (0x13u)
#define RV3028_REG_COUNT_TS            (0x14u)
#define RV3028_REG_SECONDS_TS          (0x15u)
#define RV3028_REG_UNIX_TIME0          (0x1Bu)
#define RV3028_REG_EE_ADDR             (0x25u)
#define RV3028_REG_EE_DATA             (0x26u)
#define RV3028_REG_EE_CMD              (0x27u)
#define RV3028_REG_ID                  (0x28u)
#define RV3028_REG_EEPROM_CLKOUT       (0x35u)
#define RV3028_REG_EEPROM_OFFSET       (0x36u)
#define RV3028_REG_EEPROM_BACKUP       (0x37u)

#define RV3028_STATUS_EEBUSY           (1u << 7)
#define RV3028_STATUS_BSF              (1u << 5)
#define RV3028_STATUS_UF               (1u << 4)
#define RV3028_STATUS_TF               (1u << 3)
#define RV3028_STATUS_AF               (1u << 2)
#define RV3028_STATUS_EVF              (1u << 1)

#define RV3028_CTRL1_TRPT              (1u << 7)
#define RV3028_CTRL1_WADA              (1u << 5)
#define RV3028_CTRL1_USEL              (1u << 4)
#define RV3028_CTRL1_EERD              (1u << 3)
#define RV3028_CTRL1_TE                (1u << 2)
#define RV3028_CTRL1_TD_MASK           (3u << 0)
#define RV3028_CTRL2_12_24             (1u << 1)

#define RV3028_CTRL2_TSE               (1u << 7)
#define RV3028_CTRL2_UIE               (1u << 5)
#define RV3028_CTRL2_TIE               (1u << 4)
#define RV3028_CTRL2_AIE               (1u << 3)

#define RV3028_EVENT_CTRL_TSOW         (1u << 4)

#define RV3028_EEP_CLKOUT_CLKOE        (1u << 7)
#define RV3028_EEP_CLKOUT_FD_MASK      (7u << 0)

#define RV3028_EEP_BACKUP_BSIE         (1u << 6)
#define RV3028_EEP_BACKUP_TCE          (1u << 5)
#define RV3028_EEP_BACKUP_BSM_MASK     (3u << 2)
#define RV3028_EEP_BACKUP_EEOFFSET0    (1u << 7)
#define RV3028_EEP_BACKUP_TCR_MASK     (3u << 0)

#define RV3028_ALARM_AE                (1u << 7)

#define RV3028_EE_CMD_PREAMBLE         (0x00u)
#define RV3028_EE_CMD_UPDATE_ALL       (0x11u)
#define RV3028_EE_CMD_REFRESH_ALL      (0x12u)
#define RV3028_EE_CMD_WRITE_BYTE       (0x21u)
#define RV3028_EE_CMD_READ_BYTE        (0x22u)

/*======================================================================================================
 =                                       Private variables                                              =
 ======================================================================================================*/

/* Flag to track whether the I2C layer has been initialized. */
static volatile uint8_t driverInitialized = 0u;

/*======================================================================================================
 =                                       Private functions                                              =
 ======================================================================================================*/

/*--------------------------------------------------------------------------------------------------------
 | @fn      rv3028_bcd_to_dec
 | @brief   Converts a BCD-encoded byte to decimal.
 |
 | @param   bcd  BCD value where upper nibble is tens and lower nibble is units.
 |
 | @return  Decoded decimal value.
 ---------------------------------------------------------------------------------------------------------*/

static inline uint8_t rv3028_bcd_to_dec(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10u) + (bcd & 0x0Fu));
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      rv3028_dec_to_bcd
 | @brief   Converts a decimal byte to BCD encoding.
 |
 | @param   dec  Decimal value to encode (expected 0-99).
 |
 | @return  BCD-encoded byte.
 ---------------------------------------------------------------------------------------------------------*/

static inline uint8_t rv3028_dec_to_bcd(uint8_t dec)
{
    return (uint8_t)((((dec / 10u) << 4) & 0xF0u) | (dec % 10u));
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      rv3028_write_reg
 | @brief   Writes len bytes from data to the RTC register at reg over I2C.
 |
 | @param   reg   Register address to write to.
 | @param   data  Pointer to the data buffer to write.
 | @param   len   Number of bytes to write.
 |
 | @return  RV3028_OK on success, RV3028_ERR if the I2C write fails.
 ---------------------------------------------------------------------------------------------------------*/

static int8_t rv3028_write_reg(uint8_t reg, const uint8_t *data, uint16_t len)
{
    /* Cast away const at the HAL boundary — RV3028_I2C_Write does not modify the buffer. */
    if (RV3028_I2C_Write(RV3028_I2C_ADDR, reg, (uint8_t *)data, len) != 0)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      rv3028_read_reg
 | @brief   Reads len bytes into data from the RTC register at reg over I2C.
 |
 | @param   reg   Register address to read from.
 | @param   data  Pointer to the buffer where data is stored.
 | @param   len   Number of bytes to read.
 |
 | @return  RV3028_OK on success, RV3028_ERR if the I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

static int8_t rv3028_read_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
    if (RV3028_I2C_Read(RV3028_I2C_ADDR, reg, data, len) != 0)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      rv3028_rmw
 | @brief   Reads register reg, clears bits in mask, ORs in value & mask, and writes back.
 |
 | @param   reg    Register address to modify.
 | @param   mask   Bit mask of the field to update.
 | @param   value  New bit values (only bits set in mask are applied).
 |
 | @return  RV3028_OK on success, RV3028_ERR if the I2C read or write fails.
 ---------------------------------------------------------------------------------------------------------*/

static int8_t rv3028_rmw(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t regValue;

    if (rv3028_read_reg(reg, &regValue, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    regValue = (uint8_t)((regValue & (uint8_t)(~mask)) | (value & mask));
    if (rv3028_write_reg(reg, &regValue, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      rv3028_wait_eebusy
 | @brief   Polls the EEBUSY bit in STATUS until clear or until timeoutMs milliseconds elapse.
 |          RV3028_GetTick() must return a monotonically increasing uint32_t in milliseconds;
 |          unsigned subtraction handles rollover correctly.
 |
 | @param   timeoutMs  Maximum time to wait for EEBUSY to clear, in milliseconds.
 |
 | @return  RV3028_OK when EEBUSY clears, RV3028_ERR if it does not clear within timeoutMs.
 ---------------------------------------------------------------------------------------------------------*/

static int8_t rv3028_wait_eebusy(uint32_t timeoutMs)
{
    uint8_t status;
    uint32_t startTick = RV3028_GetTick();

    while (1)
    {
        if (rv3028_read_reg(RV3028_REG_STATUS, &status, 1u) != RV3028_OK)
        {
            return RV3028_ERR;
        }
        if ((status & RV3028_STATUS_EEBUSY) == 0u)
        {
            /* Short settling delay before resuming to allow EEPROM stabilization. */
            RV3028_Delay(2u);
            return RV3028_OK;
        }
        if ((RV3028_GetTick() - startTick) >= timeoutMs)
        {
            return RV3028_ERR;
        }
        /* Wait 1 ms before polling again to avoid flooding the I2C bus. */
        RV3028_Delay(1u);
    }
}

/*======================================================================================================
 =                                        Public functions                                              =
 ======================================================================================================*/

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_Init
 | @brief   Initializes the I2C layer with the given handle and clears all stale interrupt flags on
 |          first call only. Subsequent calls are a no-op and return RV3028_OK immediately.
 |
 | @param   i2cHandle  Pointer to the HAL I2C handle used for all RTC communication.
 |
 | @return  RV3028_OK on success, RV3028_ERR if the flag clear I2C operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_Init(void *i2cHandle)
{
    if (driverInitialized == 0u)
    {
        RV3028_I2C_Init(i2cHandle);
        /* Clear any stale interrupt flags left from a previous power cycle. */
        if (RV3028_ClearAllFlags() != RV3028_OK)
        {
            return RV3028_ERR;
        }
        driverInitialized = 1u;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_DeInit
 | @brief   Resets the driver initialized state, forcing RV3028_Init to re-run the I2C setup on next call.
 ---------------------------------------------------------------------------------------------------------*/

void RV3028_DeInit(void)
{
    driverInitialized = 0u;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetDateTime
 | @brief   Encodes a RV3028_DateTime struct into BCD and writes all 7 time/date registers
 |          (seconds through year) in a single I2C transaction. The AM/PM bit is written into
 |          the hours register when the struct's ampm field is non-zero. Hours are validated
 |          against the current 12/24-hour mode read from CTRL2.
 |
 | @param   dateTime  Pointer to a RV3028_DateTime struct containing the values to program.
 |
 | @return  RV3028_OK on success, RV3028_ERR if dateTime is NULL or the I2C write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetDateTime(const RV3028_DateTime *dateTime)
{
    uint8_t buf[7];
    uint8_t ctrl2;
    uint8_t maxHours;

    /* Reject null pointer to avoid undefined behavior. */
    if (dateTime == NULL)
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_CTRL2, &ctrl2, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    /* CTRL2_12_24 bit set means 12-hour mode; cleared means 24-hour mode. */
    maxHours = ((ctrl2 & RV3028_CTRL2_12_24) != 0u) ? 12u : 23u;

    if (dateTime->seconds > 59u || dateTime->minutes > 59u || dateTime->weekday > 6u)
    {
        return RV3028_ERR;
    }

    if (dateTime->hours > maxHours ||
        ((ctrl2 & RV3028_CTRL2_12_24) != 0u && dateTime->hours < 1u) ||
        dateTime->date < 1u || dateTime->date > 31u ||
        dateTime->month < 1u || dateTime->month > 12u ||
        dateTime->year > 99u)
    {
        return RV3028_ERR;
    }

    buf[0] = rv3028_dec_to_bcd(dateTime->seconds);
    buf[1] = rv3028_dec_to_bcd(dateTime->minutes);
    buf[2] = rv3028_dec_to_bcd(dateTime->hours);
    if (dateTime->ampm != 0u)
    {
        /* Bit 5 of the hours register is the AM/PM indicator. */
        buf[2] = (uint8_t)(buf[2] | (1u << 5));
    }
    buf[3] = rv3028_dec_to_bcd(dateTime->weekday);
    buf[4] = rv3028_dec_to_bcd(dateTime->date);
    buf[5] = rv3028_dec_to_bcd(dateTime->month);
    buf[6] = rv3028_dec_to_bcd(dateTime->year);

    if (rv3028_write_reg(RV3028_REG_SECONDS, buf, 7u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetDateTime
 | @brief   Reads all 7 time/date registers from the RTC and decodes them into a RV3028_DateTime struct.
 |          Checks CTRL2 to determine 12/24-hour mode; in 12-hour mode the ampm field reflects
 |          bit 5 of the hours register, in 24-hour mode ampm is always set to 0.
 |
 | @param   dateTime  Pointer to a RV3028_DateTime struct where the decoded values are stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if dateTime is NULL or any I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetDateTime(RV3028_DateTime *dateTime)
{
    uint8_t buf[7];
    uint8_t ctrl2;

    /* Reject null pointer to avoid undefined behavior. */
    if (dateTime == NULL)
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_SECONDS, buf, 7u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    if (rv3028_read_reg(RV3028_REG_CTRL2, &ctrl2, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    dateTime->seconds = rv3028_bcd_to_dec((uint8_t)(buf[0] & 0x7Fu));
    dateTime->minutes = rv3028_bcd_to_dec((uint8_t)(buf[1] & 0x7Fu));
    if ((ctrl2 & RV3028_CTRL2_12_24) != 0u)
    {
        /* In 12-hour mode, hours use only bits [4:0]. */
        dateTime->hours = rv3028_bcd_to_dec((uint8_t)(buf[2] & 0x1Fu));
        dateTime->ampm  = (uint8_t)((buf[2] >> 5) & 0x01u);
    }
    else
    {
        /* In 24-hour mode, hours use bits [5:0]. */
        dateTime->hours = rv3028_bcd_to_dec((uint8_t)(buf[2] & 0x3Fu));
        /* AM/PM field is not meaningful in 24-hour mode. */
        dateTime->ampm  = 0u;
    }
    dateTime->weekday = rv3028_bcd_to_dec((uint8_t)(buf[3] & 0x07u));
    dateTime->date    = rv3028_bcd_to_dec((uint8_t)(buf[4] & 0x3Fu));
    dateTime->month   = rv3028_bcd_to_dec((uint8_t)(buf[5] & 0x1Fu));
    dateTime->year    = rv3028_bcd_to_dec(buf[6]);

    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_Set12_24Mode
 | @brief   Configures the hour format by updating the 12/24-hour bit in CTRL2.
 |
 | @param   use24h  Non-zero to select 24-hour mode, zero to select 12-hour mode.
 |
 | @return  RV3028_OK on success, RV3028_ERR if the I2C read-modify-write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_Set12_24Mode(uint8_t use24h)
{
    /* Bit is 0 for 24-hour mode, 1 for 12-hour mode — inverted from the parameter name. */
    uint8_t value = (use24h != 0u) ? 0u : RV3028_CTRL2_12_24;

    if (rv3028_rmw(RV3028_REG_CTRL2, RV3028_CTRL2_12_24, value) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_Get12_24Mode
 | @brief   Reads the 12/24-hour mode bit from CTRL2 and writes the decoded result to *use24h.
 |
 | @param   use24h  Pointer to a uint8_t set to 1 for 24-hour mode, 0 for 12-hour mode.
 |
 | @return  RV3028_OK on success, RV3028_ERR if NULL or I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_Get12_24Mode(uint8_t *use24h)
{
    uint8_t ctrl2;

    if (use24h == NULL)
    {
        return RV3028_ERR;
    }
    if (rv3028_read_reg(RV3028_REG_CTRL2, &ctrl2, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    /* CTRL2_12_24 bit set means 12-hour mode; cleared means 24-hour mode. */
    *use24h = ((ctrl2 & RV3028_CTRL2_12_24) == 0u) ? 1u : 0u;
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetUnixTime
 | @brief   Writes a 32-bit UNIX timestamp to the RTC UNIX time registers (little-endian).
 |          The countdown timer (TE bit) is temporarily disabled during the write and restored
 |          to its previous state afterwards.
 |
 | @param   unixTime  32-bit UNIX timestamp (seconds since 1970-01-01 00:00:00 UTC).
 |
 | @return  RV3028_OK on success, RV3028_ERR if any I2C operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetUnixTime(uint32_t unixTime)
{
    uint8_t ctrl1;
    uint8_t timeBytes[4];

    if (rv3028_read_reg(RV3028_REG_CTRL1, &ctrl1, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    /* Disable the countdown timer before writing UNIX time registers per datasheet §4.6. */
    if (rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_TE, 0u) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    timeBytes[0] = (uint8_t)(unixTime & 0xFFu);
    timeBytes[1] = (uint8_t)((unixTime >> 8) & 0xFFu);
    timeBytes[2] = (uint8_t)((unixTime >> 16) & 0xFFu);
    timeBytes[3] = (uint8_t)((unixTime >> 24) & 0xFFu);

    if (rv3028_write_reg(RV3028_REG_UNIX_TIME0, timeBytes, 4u) != RV3028_OK)
    {
        /* Best-effort restore of CTRL1 before returning error. */
        (void)rv3028_write_reg(RV3028_REG_CTRL1, &ctrl1, 1u);
        return RV3028_ERR;
    }

    /* Restore CTRL1 to re-enable the countdown timer if it was active before. */
    if (rv3028_write_reg(RV3028_REG_CTRL1, &ctrl1, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetUnixTime
 | @brief   Reads 4 consecutive UNIX time registers from the RTC and reconstructs the 32-bit
 |          timestamp (little-endian byte order).
 |
 | @param   unixTime  Pointer to a uint32_t where the UNIX timestamp is stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if unixTime is NULL or the I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetUnixTime(uint32_t *unixTime)
{
    uint8_t timeBytes[4];

    /* Reject null pointer to avoid undefined behavior. */
    if (unixTime == NULL)
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_UNIX_TIME0, timeBytes, 4u) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    *unixTime = ((uint32_t)timeBytes[0]) |
                    ((uint32_t)timeBytes[1] << 8) |
                    ((uint32_t)timeBytes[2] << 16) |
                    ((uint32_t)timeBytes[3] << 24);
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetAlarm
 | @brief   Encodes the alarm fields into BCD, sets or clears the AE (alarm-enable) bit for each
 |          field based on the enable flags in the struct, writes all 3 alarm registers, and updates
 |          the WADA bit in CTRL1 to select date vs. weekday matching. Hours are validated against
 |          the current 12/24-hour mode; weekday_date is validated against the useDate flag.
 |
 | @param   alarm    Pointer to a RV3028_Alarm struct with minutes, hours, weekday/date values
 |                   and per-field match-enable flags.
 | @param   useDate  Non-zero to match against day-of-month, zero to match against weekday.
 |
 | @return  RV3028_OK on success, RV3028_ERR if alarm is NULL or any I2C write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetAlarm(const RV3028_Alarm *alarm, uint8_t useDate)
{
    uint8_t alarmData[3];
    uint8_t wada;
    uint8_t ctrl2;
    uint8_t maxHours;

    /* Reject null pointer to avoid undefined behavior. */
    if (alarm == NULL)
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_CTRL2, &ctrl2, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    /* CTRL2_12_24 bit set means 12-hour mode; cleared means 24-hour mode. */
    maxHours = ((ctrl2 & RV3028_CTRL2_12_24) != 0u) ? 12u : 23u;

    if (alarm->minutes > 59u)
    {
        return RV3028_ERR;
    }
    if (alarm->hours > maxHours ||
        ((ctrl2 & RV3028_CTRL2_12_24) != 0u && alarm->hours < 1u))
    {
        return RV3028_ERR;
    }
    if (useDate != 0u)
    {
        if (alarm->weekday_date < 1u || alarm->weekday_date > 31u)
        {
            return RV3028_ERR;
        }
    }
    else
    {
        if (alarm->weekday_date > 6u)
        {
            return RV3028_ERR;
        }
    }

    alarmData[0] = rv3028_dec_to_bcd(alarm->minutes);
    alarmData[1] = rv3028_dec_to_bcd(alarm->hours);
    if ((ctrl2 & RV3028_CTRL2_12_24) != 0u && alarm->ampm != 0u)
    {
        /* Bit 5 of the alarm hours register is the AM/PM indicator in 12-hour mode. */
        alarmData[1] = (uint8_t)(alarmData[1] | (1u << 5));
    }
    alarmData[2] = rv3028_dec_to_bcd(alarm->weekday_date);

    /* AE bit set in a field disables that field from participating in the alarm match. */
    if (alarm->enable_minutes_match == 0u)
    {
        alarmData[0] = (uint8_t)(alarmData[0] | RV3028_ALARM_AE);
    }
    if (alarm->enable_hours_match == 0u)
    {
        alarmData[1] = (uint8_t)(alarmData[1] | RV3028_ALARM_AE);
    }
    if (alarm->enable_weekday_date_match == 0u)
    {
        alarmData[2] = (uint8_t)(alarmData[2] | RV3028_ALARM_AE);
    }

    if (rv3028_write_reg(RV3028_REG_MIN_ALARM, alarmData, 3u) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    wada = (useDate != 0u) ? RV3028_CTRL1_WADA : 0u;
    if (rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_WADA, wada) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EnableAlarmInterrupt
 | @brief   Sets or clears the AIE bit in CTRL2 to enable or disable the alarm interrupt on the INT pin.
 |
 | @param   enable  Non-zero to enable the alarm interrupt, zero to disable.
 |
 | @return  RV3028_OK on success, RV3028_ERR if the I2C read-modify-write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EnableAlarmInterrupt(uint8_t enable)
{
    uint8_t value = (enable != 0u) ? RV3028_CTRL2_AIE : 0u;

    if (rv3028_rmw(RV3028_REG_CTRL2, RV3028_CTRL2_AIE, value) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetAlarmFlag
 | @brief   Reads the STATUS register and extracts the AF (Alarm Flag) bit.
 |
 | @param   flag  Pointer to a uint8_t set to 1 if the alarm has fired, 0 otherwise.
 |
 | @return  RV3028_OK on success, RV3028_ERR if flag is NULL or the I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetAlarmFlag(uint8_t *flag)
{
    uint8_t status;

    /* Reject null pointer to avoid undefined behavior. */
    if (flag == NULL)
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_STATUS, &status, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    *flag = (uint8_t)((status & RV3028_STATUS_AF) != 0u);
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ClearAlarmFlag
 | @brief   Clears the AF (Alarm Flag) bit in the STATUS register via a read-modify-write.
 |
 | @return  RV3028_OK on success, RV3028_ERR if the I2C read-modify-write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ClearAlarmFlag(void)
{
    if (rv3028_rmw(RV3028_REG_STATUS, RV3028_STATUS_AF, 0u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetCountdownTimer
 | @brief   Loads a 12-bit reload value into the timer registers and configures the clock source,
 |          repeat mode, and enable state in CTRL1. Values above 4095 are truncated to 12 bits.
 |
 | @param   value   12-bit countdown reload value (0-4095).
 | @param   clkSrc  Clock source for the countdown timer (4096 Hz, 64 Hz, 1 Hz, or 1/60 Hz).
 | @param   repeat  Non-zero to automatically reload and restart the timer on expiry (TRPT bit).
 | @param   enable  Non-zero to start the countdown timer immediately (TE bit).
 |
 | @return  RV3028_OK on success, RV3028_ERR if any I2C write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetCountdownTimer(uint16_t value, RV3028_TimerClkSrc clkSrc, uint8_t repeat, uint8_t enable)
{
    uint8_t timerBytes[2];
    uint8_t ctrl1Value;

    /* Limit timer value to 12 bits as the hardware register is 12-bit wide. */
    value &= 0x0FFFu;

    timerBytes[0] = (uint8_t)(value & 0xFFu);
    timerBytes[1] = (uint8_t)((value >> 8) & 0x0Fu);

    if (rv3028_write_reg(RV3028_REG_TIMER_VALUE0, timerBytes, 2u) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    ctrl1Value = (uint8_t)((uint8_t)clkSrc & RV3028_CTRL1_TD_MASK);
    if (repeat != 0u)
    {
        /* TRPT bit makes the timer reload and restart on expiry. */
        ctrl1Value = (uint8_t)(ctrl1Value | RV3028_CTRL1_TRPT);
    }
    if (enable != 0u)
    {
        ctrl1Value = (uint8_t)(ctrl1Value | RV3028_CTRL1_TE);
    }

    if (rv3028_rmw(RV3028_REG_CTRL1,
                   (uint8_t)(RV3028_CTRL1_TD_MASK | RV3028_CTRL1_TRPT | RV3028_CTRL1_TE),
                   ctrl1Value) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetCountdownTimer
 | @brief   Reads back the 12-bit reload value from TIMER_VALUE0 and TIMER_VALUE1 registers.
 |
 | @param   value  Pointer to a uint16_t where the 12-bit reload value is stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if value is NULL or the I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetCountdownTimer(uint16_t *value)
{
    uint8_t timerBytes[2];

    /* Reject null pointer to avoid undefined behavior. */
    if (value == NULL)
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_TIMER_VALUE0, timerBytes, 2u) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    *value = (uint16_t)(timerBytes[0] | ((uint16_t)(timerBytes[1] & 0x0Fu) << 8));
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetCountdownTimerCount
 | @brief   Reads the 12-bit remaining count from TIMER_STATUS0 and TIMER_STATUS1 registers (0x0C, 0x0D).
 |
 | @param   count  Pointer to a uint16_t where the remaining 12-bit count is stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if count is NULL or the I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetCountdownTimerCount(uint16_t *count)
{
    uint8_t countBytes[2];

    /* Reject null pointer to avoid undefined behavior. */
    if (count == NULL)
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_TIMER_STATUS0, countBytes, 2u) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    *count = (uint16_t)(countBytes[0] | ((uint16_t)(countBytes[1] & 0x0Fu) << 8));
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EnableTimerInterrupt
 | @brief   Sets or clears the TIE bit in CTRL2 to enable or disable the countdown timer interrupt
 |          on the INT pin.
 |
 | @param   enable  Non-zero to enable the timer interrupt, zero to disable.
 |
 | @return  RV3028_OK on success, RV3028_ERR if the I2C read-modify-write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EnableTimerInterrupt(uint8_t enable)
{
    uint8_t value = (enable != 0u) ? RV3028_CTRL2_TIE : 0u;

    if (rv3028_rmw(RV3028_REG_CTRL2, RV3028_CTRL2_TIE, value) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetTimerFlag
 | @brief   Reads the STATUS register and extracts the TF (Timer Flag) bit.
 |
 | @param   flag  Pointer to a uint8_t set to 1 if the countdown timer has expired, 0 otherwise.
 |
 | @return  RV3028_OK on success, RV3028_ERR if flag is NULL or the I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetTimerFlag(uint8_t *flag)
{
    uint8_t status;

    /* Reject null pointer to avoid undefined behavior. */
    if (flag == NULL)
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_STATUS, &status, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    *flag = (uint8_t)((status & RV3028_STATUS_TF) != 0u);
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ClearTimerFlag
 | @brief   Clears the TF (Timer Flag) bit in the STATUS register via a read-modify-write.
 |
 | @return  RV3028_OK on success, RV3028_ERR if the I2C read-modify-write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ClearTimerFlag(void)
{
    if (rv3028_rmw(RV3028_REG_STATUS, RV3028_STATUS_TF, 0u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EnableUpdateInterrupt
 | @brief   Configures the periodic update interrupt by setting UIE in CTRL2 and USEL in CTRL1.
 |          The interrupt fires once per second or once per minute depending on onMinute.
 |
 | @param   enable    Non-zero to enable the periodic update interrupt, zero to disable.
 | @param   onMinute  Non-zero to trigger on every minute change, zero to trigger on every second change.
 |
 | @return  RV3028_OK on success, RV3028_ERR if any I2C read-modify-write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EnableUpdateInterrupt(uint8_t enable, uint8_t onMinute)
{
    uint8_t ctrl2Value = (enable != 0u) ? RV3028_CTRL2_UIE : 0u;
    uint8_t uselValue  = (onMinute != 0u) ? RV3028_CTRL1_USEL : 0u;

    if (rv3028_rmw(RV3028_REG_CTRL2, RV3028_CTRL2_UIE, ctrl2Value) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    if (rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_USEL, uselValue) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetUpdateFlag
 | @brief   Reads the STATUS register and extracts the UF (Update Flag) bit.
 |
 | @param   flag  Pointer to a uint8_t set to 1 if a periodic update event has occurred, 0 otherwise.
 |
 | @return  RV3028_OK on success, RV3028_ERR if flag is NULL or the I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetUpdateFlag(uint8_t *flag)
{
    uint8_t status;

    /* Reject null pointer to avoid undefined behavior. */
    if (flag == NULL)
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_STATUS, &status, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    *flag = (uint8_t)((status & RV3028_STATUS_UF) != 0u);
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ClearUpdateFlag
 | @brief   Clears the UF (Update Flag) bit in the STATUS register via a read-modify-write.
 |
 | @return  RV3028_OK on success, RV3028_ERR if the I2C read-modify-write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ClearUpdateFlag(void)
{
    if (rv3028_rmw(RV3028_REG_STATUS, RV3028_STATUS_UF, 0u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetStatus
 | @brief   Reads the raw STATUS register byte into the caller's buffer. The caller can inspect
 |          individual flag bits (EEBUSY, BSF, UF, TF, AF, EVF) directly from the returned byte.
 |
 | @param   status  Pointer to a uint8_t where the raw STATUS register value is stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if status is NULL or the I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetStatus(uint8_t *status)
{
    /* Reject null pointer to avoid undefined behavior. */
    if (status == NULL)
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_STATUS, status, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ClearAllFlags
 | @brief   Clears all clearable status flags (AF, TF, UF, EVF, BSF) in a single
 |          read-modify-write on the STATUS register. Also called by RV3028_Init at startup.
 |
 | @return  RV3028_OK on success, RV3028_ERR if the I2C read-modify-write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ClearAllFlags(void)
{
    uint8_t mask = (uint8_t)(RV3028_STATUS_AF |
                              RV3028_STATUS_TF |
                              RV3028_STATUS_UF |
                              RV3028_STATUS_EVF |
                              RV3028_STATUS_BSF);

    if (rv3028_rmw(RV3028_REG_STATUS, mask, 0u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetClkout
 | @brief   Updates the CLKOUT EEPROM register with the selected frequency and output-enable state,
 |          then issues a refresh-all command so the shadow registers take effect immediately.
 |
 | @param   freq    Output frequency selection for the CLKOUT pin (see RV3028_ClkoutFreq).
 | @param   enable  Non-zero to drive the CLKOUT pin, zero to disable it.
 |
 | @return  RV3028_OK on success, RV3028_ERR if freq is out of range or any EEPROM/I2C operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetClkout(RV3028_ClkoutFreq freq, uint8_t enable)
{
    uint8_t eeData;
    uint8_t command;

    if ((uint8_t)freq > (uint8_t)RV3028_CLKOUT_LOW)
    {
        return RV3028_ERR;
    }

    if (RV3028_EEReadByte(RV3028_REG_EEPROM_CLKOUT, &eeData) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    eeData = (uint8_t)(eeData & (uint8_t)(~RV3028_EEP_CLKOUT_FD_MASK));
    eeData = (uint8_t)(eeData | (((uint8_t)freq) & RV3028_EEP_CLKOUT_FD_MASK));
    if (enable != 0u)
    {
        eeData = (uint8_t)(eeData | RV3028_EEP_CLKOUT_CLKOE);
    }
    else
    {
        eeData = (uint8_t)(eeData & (uint8_t)(~RV3028_EEP_CLKOUT_CLKOE));
    }

    if (RV3028_EEWriteByte(RV3028_REG_EEPROM_CLKOUT, eeData) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    /* Start EEPROM-access session by setting EERD bit in CTRL1. */
    if (rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, RV3028_CTRL1_EERD) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    if (rv3028_wait_eebusy(RV3028_EEBUSY_TIMEOUT_MS) != RV3028_OK)
    {
        /* Best-effort cleanup: clear EERD before returning error. */
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* Mandatory preamble before EEPROM commands per datasheet. */
    command = RV3028_EE_CMD_PREAMBLE;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* WARNING: REFRESH_ALL copies ALL EEPROM to ALL shadow registers — this also activates
     * any pending EEPROM changes to the BACKUP and OFFSET registers, not just CLKOUT. */
    command = RV3028_EE_CMD_REFRESH_ALL;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }
    if (rv3028_wait_eebusy(RV3028_EEBUSY_TIMEOUT_MS) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* Final preamble closes the EEPROM command sequence. */
    command = RV3028_EE_CMD_PREAMBLE;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* End EEPROM-access session by clearing EERD bit in CTRL1. */
    if (rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EEWriteByte
 | @brief   Writes a single byte to the specified EEPROM address using the full command sequence:
 |          set EERD, wait for not-busy, write address and data, send preamble, send write-byte
 |          command, wait for completion, send final preamble, clear EERD.
 |
 | @param   eeAddr  EEPROM byte address to write.
 | @param   data    Byte value to write to the EEPROM address.
 |
 | @return  RV3028_OK on success, RV3028_ERR if any I2C or EEPROM busy-timeout operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EEWriteByte(uint8_t eeAddr, uint8_t data)
{
    uint8_t command;

    /* Start EEPROM-access session by setting EERD bit in CTRL1. */
    if (rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, RV3028_CTRL1_EERD) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    if (rv3028_wait_eebusy(RV3028_EEBUSY_TIMEOUT_MS) != RV3028_OK)
    {
        /* Best-effort cleanup: clear EERD before returning error. */
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    if (rv3028_write_reg(RV3028_REG_EE_ADDR, &eeAddr, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }
    if (rv3028_write_reg(RV3028_REG_EE_DATA, &data, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* Mandatory preamble before EEPROM commands per datasheet. */
    command = RV3028_EE_CMD_PREAMBLE;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    command = RV3028_EE_CMD_WRITE_BYTE;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }
    if (rv3028_wait_eebusy(RV3028_EEBUSY_TIMEOUT_MS) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* Final preamble closes the EEPROM command sequence. */
    command = RV3028_EE_CMD_PREAMBLE;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* End EEPROM-access session by clearing EERD bit in CTRL1. */
    if (rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EEReadByte
 | @brief   Reads a single byte from the specified EEPROM address using the full command sequence:
 |          set EERD, wait for not-busy, write address, send preamble, send read-byte command,
 |          wait for completion, read EE_DATA, send final preamble, clear EERD.
 |
 | @param   eeAddr  EEPROM byte address to read.
 | @param   data    Pointer to a uint8_t where the read byte is stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if data is NULL or any EEPROM/I2C operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EEReadByte(uint8_t eeAddr, uint8_t *data)
{
    uint8_t command;

    /* Reject null pointer to avoid undefined behavior. */
    if (data == NULL)
    {
        return RV3028_ERR;
    }

    /* Start EEPROM-access session by setting EERD bit in CTRL1. */
    if (rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, RV3028_CTRL1_EERD) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    if (rv3028_wait_eebusy(RV3028_EEBUSY_TIMEOUT_MS) != RV3028_OK)
    {
        /* Best-effort cleanup: clear EERD before returning error. */
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    if (rv3028_write_reg(RV3028_REG_EE_ADDR, &eeAddr, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* Mandatory preamble before EEPROM commands per datasheet. */
    command = RV3028_EE_CMD_PREAMBLE;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    command = RV3028_EE_CMD_READ_BYTE;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }
    if (rv3028_wait_eebusy(RV3028_EEBUSY_TIMEOUT_MS) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_EE_DATA, data, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* Final preamble closes the EEPROM command sequence. */
    command = RV3028_EE_CMD_PREAMBLE;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* End EEPROM-access session by clearing EERD bit in CTRL1. */
    if (rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EEUpdateAll
 | @brief   Copies all active shadow configuration registers to EEPROM using the update-all command,
 |          making configuration changes (clkout, backup, offset, etc.) persistent across power cycles.
 |
 | @return  RV3028_OK on success, RV3028_ERR if any EEPROM/I2C operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EEUpdateAll(void)
{
    uint8_t command;

    /* Start EEPROM-access session by setting EERD bit in CTRL1. */
    if (rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, RV3028_CTRL1_EERD) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    if (rv3028_wait_eebusy(RV3028_EEBUSY_TIMEOUT_MS) != RV3028_OK)
    {
        /* Best-effort cleanup: clear EERD before returning error. */
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* Mandatory preamble before EEPROM commands per datasheet. */
    command = RV3028_EE_CMD_PREAMBLE;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    command = RV3028_EE_CMD_UPDATE_ALL;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }
    if (rv3028_wait_eebusy(RV3028_EEBUSY_TIMEOUT_MS) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* Final preamble closes the EEPROM command sequence. */
    command = RV3028_EE_CMD_PREAMBLE;
    if (rv3028_write_reg(RV3028_REG_EE_CMD, &command, 1u) != RV3028_OK)
    {
        (void)rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u);
        return RV3028_ERR;
    }

    /* End EEPROM-access session by clearing EERD bit in CTRL1. */
    if (rv3028_rmw(RV3028_REG_CTRL1, RV3028_CTRL1_EERD, 0u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ConfigureBackup
 | @brief   Writes the backup switchover mode (BSM field) and backup switchover interrupt enable
 |          (BSIE bit) to the EEPROM BACKUP register, preserving all other bit fields.
 |
 | @param   mode        Backup switchover mode: disabled, direct, or level-switching.
 | @param   bsieEnable  Non-zero to generate an interrupt on backup switchover, zero to suppress it.
 |
 | @return  RV3028_OK on success, RV3028_ERR if mode is invalid or any EEPROM/I2C operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ConfigureBackup(RV3028_BackupMode mode, uint8_t bsieEnable)
{
    uint8_t eeData;

    if (mode != RV3028_BSM_DISABLED && mode != RV3028_BSM_DIRECT && mode != RV3028_BSM_LEVEL)
    {
        return RV3028_ERR;
    }

    if (RV3028_EEReadByte(RV3028_REG_EEPROM_BACKUP, &eeData) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    eeData = (uint8_t)(eeData & (uint8_t)(~RV3028_EEP_BACKUP_BSM_MASK));
    eeData = (uint8_t)(eeData | (((uint8_t)mode << 2) & RV3028_EEP_BACKUP_BSM_MASK));
    if (bsieEnable != 0u)
    {
        eeData = (uint8_t)(eeData | RV3028_EEP_BACKUP_BSIE);
    }
    else
    {
        eeData = (uint8_t)(eeData & (uint8_t)(~RV3028_EEP_BACKUP_BSIE));
    }

    if (RV3028_EEWriteByte(RV3028_REG_EEPROM_BACKUP, eeData) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ConfigureTrickleCharger
 | @brief   Writes the trickle charger enable (TCE bit) and series resistor selection (TCR field)
 |          to the EEPROM BACKUP register, preserving all other bit fields.
 |
 | @param   enable    Non-zero to enable the trickle charger, zero to disable.
 | @param   resistor  Series resistor for the trickle charge path (3 kOhm, 5 kOhm, 9 kOhm, or 15 kOhm).
 |
 | @return  RV3028_OK on success, RV3028_ERR if resistor is out of range or any EEPROM/I2C operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ConfigureTrickleCharger(uint8_t enable, RV3028_TrickleResistor resistor)
{
    uint8_t eeData;

    if ((uint8_t)resistor > (uint8_t)RV3028_TCR_15K)
    {
        return RV3028_ERR;
    }

    if (RV3028_EEReadByte(RV3028_REG_EEPROM_BACKUP, &eeData) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    eeData = (uint8_t)(eeData & (uint8_t)(~RV3028_EEP_BACKUP_TCR_MASK));
    eeData = (uint8_t)(eeData | (((uint8_t)resistor) & RV3028_EEP_BACKUP_TCR_MASK));
    if (enable != 0u)
    {
        eeData = (uint8_t)(eeData | RV3028_EEP_BACKUP_TCE);
    }
    else
    {
        eeData = (uint8_t)(eeData & (uint8_t)(~RV3028_EEP_BACKUP_TCE));
    }

    if (RV3028_EEWriteByte(RV3028_REG_EEPROM_BACKUP, eeData) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetTimeStamp
 | @brief   Reads the EVI event counter and 6 consecutive timestamp registers (seconds through year),
 |          then decodes them into a RV3028_DateTime struct. Hours and ampm are decoded according to
 |          the current 12/24-hour mode read from CTRL2. The weekday field is always set to 0
 |          because the hardware does not capture weekday in the timestamp.
 |
 | @param   timeStamp  Pointer to a RV3028_DateTime struct where the decoded timestamp is stored.
 | @param   count      Pointer to a uint8_t where the number of EVI events since last read is stored.
 |
 | @return  RV3028_OK on success, RV3028_ERR if either pointer is NULL or any I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetTimeStamp(RV3028_DateTime *timeStamp, uint8_t *count)
{
    uint8_t timeStampData[6];
    uint8_t ctrl2;

    /* Reject null pointers to avoid undefined behavior. */
    if ((timeStamp == NULL) || (count == NULL))
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_COUNT_TS, count, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    if (rv3028_read_reg(RV3028_REG_SECONDS_TS, timeStampData, 6u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    if (rv3028_read_reg(RV3028_REG_CTRL2, &ctrl2, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    timeStamp->seconds = rv3028_bcd_to_dec((uint8_t)(timeStampData[0] & 0x7Fu));
    timeStamp->minutes = rv3028_bcd_to_dec((uint8_t)(timeStampData[1] & 0x7Fu));
    if ((ctrl2 & RV3028_CTRL2_12_24) != 0u)
    {
        /* In 12-hour mode, hours use only bits [4:0]. */
        timeStamp->hours = rv3028_bcd_to_dec((uint8_t)(timeStampData[2] & 0x1Fu));
        timeStamp->ampm  = (uint8_t)((timeStampData[2] >> 5) & 0x01u);
    }
    else
    {
        /* In 24-hour mode, hours use bits [5:0]. */
        timeStamp->hours = rv3028_bcd_to_dec((uint8_t)(timeStampData[2] & 0x3Fu));
        /* AM/PM field is not meaningful in 24-hour mode. */
        timeStamp->ampm  = 0u;
    }
    timeStamp->date    = rv3028_bcd_to_dec((uint8_t)(timeStampData[3] & 0x3Fu));
    timeStamp->month   = rv3028_bcd_to_dec((uint8_t)(timeStampData[4] & 0x1Fu));
    timeStamp->year    = rv3028_bcd_to_dec(timeStampData[5]);
    /* Weekday is not captured in the timestamp registers; set to zero. */
    timeStamp->weekday = 0u;

    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_EnableTimeStamp
 | @brief   Configures the timestamp feature by updating TSE in CTRL2 (capture enable) and
 |          TSOW in EVENT_CTRL (overflow behavior).
 |
 | @param   enable        Non-zero to enable timestamp capture on EVI pin events, zero to disable.
 | @param   overwriteOld  Non-zero to overwrite the oldest entry when the counter overflows,
 |                        zero to stop capturing new events when full.
 |
 | @return  RV3028_OK on success, RV3028_ERR if any I2C read-modify-write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_EnableTimeStamp(uint8_t enable, uint8_t overwriteOld)
{
    uint8_t tseValue  = (enable != 0u) ? RV3028_CTRL2_TSE : 0u;
    uint8_t tsowValue = (overwriteOld != 0u) ? RV3028_EVENT_CTRL_TSOW : 0u;

    if (rv3028_rmw(RV3028_REG_CTRL2, RV3028_CTRL2_TSE, tseValue) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    if (rv3028_rmw(RV3028_REG_EVENT_CTRL, RV3028_EVENT_CTRL_TSOW, tsowValue) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_GetEventFlag
 | @brief   Reads the STATUS register and extracts the EVF (Event Flag) bit.
 |
 | @param   flag  Pointer to a uint8_t set to 1 if an EVI event has occurred, 0 otherwise.
 |
 | @return  RV3028_OK on success, RV3028_ERR if flag is NULL or the I2C read fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_GetEventFlag(uint8_t *flag)
{
    uint8_t status;

    /* Reject null pointer to avoid undefined behavior. */
    if (flag == NULL)
    {
        return RV3028_ERR;
    }

    if (rv3028_read_reg(RV3028_REG_STATUS, &status, 1u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    *flag = (uint8_t)((status & RV3028_STATUS_EVF) != 0u);
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_ClearTimeStampFlag
 | @brief   Clears the EVF (Event Flag) bit in the STATUS register via a read-modify-write.
 |
 | @return  RV3028_OK on success, RV3028_ERR if the I2C read-modify-write fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_ClearTimeStampFlag(void)
{
    if (rv3028_rmw(RV3028_REG_STATUS, RV3028_STATUS_EVF, 0u) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      RV3028_SetFrequencyOffset
 | @brief   Programs a 9-bit signed frequency correction value into EEPROM. The upper 8 bits are
 |          written to EEPROM_OFFSET and the LSB is stored in bit 7 of the EEPROM BACKUP register.
 |          Values outside [-256, +255] are clamped to the boundary before writing. The two EEPROM
 |          writes are non-atomic; if the second write fails, the caller should retry the full call.
 |
 | @param   offset  Signed frequency correction value, clamped to the range [-256, +255].
 |
 | @return  RV3028_OK on success, RV3028_ERR if any EEPROM/I2C operation fails.
 ---------------------------------------------------------------------------------------------------------*/

int8_t RV3028_SetFrequencyOffset(int16_t offset)
{
    int16_t clamped;
    uint16_t raw9;
    uint8_t offsetMsb;
    uint8_t eeBackup;

    if (offset > 255)
    {
        clamped = 255;
    }
    else if (offset < -256)
    {
        clamped = -256;
    }
    else
    {
        clamped = offset;
    }

    /* Mask to 9 bits to obtain the raw two's-complement representation. */
    raw9      = (uint16_t)clamped & 0x01FFu;
    /* Shift right by 1 to get the 8 MSBs written to EEPROM_OFFSET register. */
    offsetMsb = (uint8_t)((raw9 >> 1) & 0xFFu);

    if (RV3028_EEReadByte(RV3028_REG_EEPROM_BACKUP, &eeBackup) != RV3028_OK)
    {
        return RV3028_ERR;
    }

    /* The LSB of the 9-bit offset is stored in bit 7 of the BACKUP register. */
    if ((raw9 & 0x0001u) != 0u)
    {
        eeBackup = (uint8_t)(eeBackup | RV3028_EEP_BACKUP_EEOFFSET0);
    }
    else
    {
        eeBackup = (uint8_t)(eeBackup & (uint8_t)(~RV3028_EEP_BACKUP_EEOFFSET0));
    }

    /* The 9-bit offset is split across two EEPROM locations (EEPROM_BACKUP holds the LSB in bit 7,
     * EEPROM_OFFSET holds the upper 8 bits). If the second write fails the registers are left in
     * an inconsistent state; the caller should retry the full call. */
    if (RV3028_EEWriteByte(RV3028_REG_EEPROM_BACKUP, eeBackup) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    if (RV3028_EEWriteByte(RV3028_REG_EEPROM_OFFSET, offsetMsb) != RV3028_OK)
    {
        return RV3028_ERR;
    }
    return RV3028_OK;
}
