/*************************************************************************************
 Title   :   NANO-RTC UART Clock Example — RV-3028 Application Layer
 File    :   nano_rtc.c
 Author  :   Bardia Alikhan Afshar <bardia.a.afshar@gmail.com>
 Date    :   2026-06-13
**************************************************************************************/

/*======================================================================================================
 =                                              Includes                                                =
 ======================================================================================================*/

#include "nano_rtc.h"
#include "rv3028_i2c.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*======================================================================================================
 =                                        Private constants                                             =
 ======================================================================================================*/

/* Timeout in milliseconds for UART receive — doubles as the ~1 Hz clock tick. */
#define NANO_RTC_UART_TICK_MS          (900u)

/* Duration of the startup splash screen pause before the clock begins. */
#define NANO_RTC_STARTUP_DELAY_MS      (1500u)

/* BSF bit position in the RV-3028 STATUS register (backup switchover flag). */
#define NANO_RTC_STATUS_BSF            (1u << 5)

/*======================================================================================================
 =                                        Private variables                                             =
 ======================================================================================================*/

/* External HAL handle for the I2C1 peripheral, defined in main.c. */
extern I2C_HandleTypeDef hi2c1;

/* External HAL handle for the USART2 peripheral, defined in main.c. */
extern UART_HandleTypeDef huart2;

/* Weekday display strings, indexed by RV3028_DateTime.weekday (0=Sunday … 6=Saturday). */
static const char *weekdayNames[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

/* Current hour mode: 1 = 24-hour, 0 = 12-hour AM/PM. Mirrors the chip register. */
static uint8_t currentUse24h = 1u;

/* Set to 1 whenever the banner is (re)drawn so the panel prints fresh on the next tick. */
static uint8_t clockPanelFirstDraw = 1u;

/* Last interrupt event shown in the EVENT row of the clock panel. */
static const char *lastEvent = "---";

/*======================================================================================================
 =                                        Private functions                                             =
 ======================================================================================================*/

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_print
 | @brief   Transmits a null-terminated string over USART2, blocking until all bytes are sent.
 |
 | @param   msg  Null-terminated string to transmit.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_print(const char *msg)
{
    /* Transmit the null-terminated string over USART2, blocking until complete. */
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, (uint16_t)strlen(msg), HAL_MAX_DELAY);
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_read_char
 | @brief   Receives one byte from USART2 (blocking), echoes it to the terminal, and returns it.
 |
 | @return  The byte received from the terminal.
 ---------------------------------------------------------------------------------------------------------*/

static uint8_t nano_rtc_read_char(void)
{
    uint8_t rxByte = 0u;

    /* Block until one byte arrives on USART2. */
    HAL_UART_Receive(&huart2, &rxByte, 1u, HAL_MAX_DELAY);

    /* Echo the received byte back so the user sees what they typed. */
    HAL_UART_Transmit(&huart2, &rxByte, 1u, HAL_MAX_DELAY);

    /* Return the received character to the caller. */
    return rxByte;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_read_line
 | @brief   Reads a line from USART2 into outBuf, terminated by CR, LF, or a lone B/b back key.
 |          Echoes printable characters back to the terminal; null-terminates the result.
 |
 | @param   outBuf   Destination buffer for the collected string.
 | @param   maxLen   Size of outBuf in bytes, including the null terminator.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_read_line(char *outBuf, uint8_t maxLen)
{
    uint8_t idx = 0u;
    uint8_t rxByte;

    /* Collect characters until CR, LF, or the buffer is full. */
    while (idx < (maxLen - 1u))
    {
        /* Block until one byte arrives on USART2. */
        HAL_UART_Receive(&huart2, &rxByte, 1u, HAL_MAX_DELAY);

        /* CR signals end of input; echo CRLF to advance the terminal line. */
        if (rxByte == '\r')
        {
            nano_rtc_print("\r\n");
            break;
        }

        /* Bare LF also terminates input without an extra echo. */
        if (rxByte == '\n')
        {
            break;
        }

        /* Echo the printable character back to the terminal. */
        HAL_UART_Transmit(&huart2, &rxByte, 1u, HAL_MAX_DELAY);

        /* Store the character in the output buffer and advance the index. */
        outBuf[idx] = (char)rxByte;
        idx++;

        /* If the first and only character typed so far is B/b, treat it as
         * an immediate back command — no Enter required, matching the feel
         * of the single-key (read_char) prompts. */
        if ((idx == 1u) && ((outBuf[0] == 'B') || (outBuf[0] == 'b')))
        {
            nano_rtc_print("\r\n");
            break;
        }
    }

    /* Null-terminate the collected string. */
    outBuf[idx] = '\0';
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_print_banner
 | @brief   Clears the terminal and prints the NANO-RTC ASCII art logo with subtitle and author.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_print_banner(void)
{
    /* Clear the terminal and position cursor at the top-left corner. */
    nano_rtc_print("\033[2J\033[H");

    /* Print the NANO RTC ASCII art logo — source file is UTF-8. */
    nano_rtc_print("███╗   ██╗ █████╗ ███╗   ██╗ ██████╗       ██████╗ ████████╗ ██████╗\r\n");
    nano_rtc_print("████╗  ██║██╔══██╗████╗  ██║██╔═══██╗      ██╔══██╗╚══██╔══╝██╔════╝\r\n");
    nano_rtc_print("██╔██╗ ██║███████║██╔██╗ ██║██║   ██║      ██████╔╝   ██║   ██║\r\n");
    nano_rtc_print("██║╚██╗██║██╔══██║██║╚██╗██║██║   ██║      ██╔══██╗   ██║   ██║\r\n");
    nano_rtc_print("██║ ╚████║██║  ██║██║ ╚████║╚██████╔╝      ██║  ██║   ██║   ╚██████╗\r\n");
    nano_rtc_print("╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝  ╚═══╝ ╚═════╝       ╚═╝  ╚═╝   ╚═╝    ╚═════╝\r\n");

    /* Print subtitle and author information below the logo. */
    nano_rtc_print("\r\n");
    nano_rtc_print("  RV3028-C7 High Precision RTC Example\r\n");
    nano_rtc_print("  Driver by Bardia Alikhan Afshar\r\n");
    nano_rtc_print("  github.com/Bardia-Afshar\r\n");
    nano_rtc_print("\r\n");
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_print_clock_header
 | @brief   Redraws the banner and prints the clock-mode status line and separator.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_print_clock_header(void)
{
    /* Redraw the persistent banner at the top of the clock screen. */
    nano_rtc_print_banner();

    /* Print the clock-mode status line and separator. */
    nano_rtc_print("  Press 'S' at any time to enter Settings\r\n");
    nano_rtc_print("------------------------------------------------------------------\r\n");

    /* Blank line reserved for the overwriting clock output. */
    nano_rtc_print("\r\n");

    /* Banner was just redrawn — next panel call must print fresh, not cursor-up. */
    clockPanelFirstDraw = 1u;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_is_back_char
 | @brief   Returns 1 if rxByte is the upper- or lower-case back key ('B'/'b'), 0 otherwise.
 |
 | @param   rxByte  Single byte received from the terminal.
 |
 | @return  1 if the byte is a back key press, 0 otherwise.
 ---------------------------------------------------------------------------------------------------------*/

static uint8_t nano_rtc_is_back_char(uint8_t rxByte)
{
    /* Treat both upper-case and lower-case B as the back command. */
    return ((rxByte == 'B') || (rxByte == 'b')) ? 1u : 0u;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_is_back_line
 | @brief   Returns 1 if buf is exactly "B" or "b" (the single-character back command), 0 otherwise.
 |
 | @param   buf  Null-terminated string from nano_rtc_read_line.
 |
 | @return  1 if the line contains only a back key, 0 otherwise.
 ---------------------------------------------------------------------------------------------------------*/

static uint8_t nano_rtc_is_back_line(const char *buf)
{
    /* A line is a back command when it is exactly the single character B or b. */
    return (((buf[0] == 'B') || (buf[0] == 'b')) && (buf[1] == '\0')) ? 1u : 0u;
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_menu_datetime
 | @brief   Prompts the user over USART2 to enter year, month, date, weekday, hours, AM/PM,
 |          minutes, and seconds, then writes the assembled date/time to the RTC. Pressing 'B'
 |          at any field prompt returns immediately to the settings menu.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_menu_datetime(void)
{
    char inputBuf[16];
    RV3028_DateTime dateTime;
    uint8_t weekdayInput;
    uint8_t rxByte;

    /* Draw the banner and submenu title before collecting input. */
    nano_rtc_print_banner();
    nano_rtc_print("------------------------------------------------------------------\r\n");
    nano_rtc_print("  SET DATE & TIME\r\n");
    nano_rtc_print("  Press 'B' to go back\r\n");
    nano_rtc_print("------------------------------------------------------------------\r\n");

    /* Zero-initialise the struct so unused fields are well-defined. */
    memset(&dateTime, 0, sizeof(dateTime));

    /* Prompt for and read the 2-digit year. */
    nano_rtc_print("  Year (00-99): ");
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    dateTime.year = (uint8_t)atoi(inputBuf);
    if (dateTime.year > 99u)
    {
        nano_rtc_print("  Invalid input.\r\n");
        return;
    }

    /* Prompt for and read the month. */
    nano_rtc_print("  Month (1-12): ");
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    dateTime.month = (uint8_t)atoi(inputBuf);
    if ((dateTime.month < 1u) || (dateTime.month > 12u))
    {
        nano_rtc_print("  Invalid input.\r\n");
        return;
    }

    /* Prompt for and read the day-of-month. */
    nano_rtc_print("  Date (1-31): ");
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    dateTime.date = (uint8_t)atoi(inputBuf);
    if ((dateTime.date < 1u) || (dateTime.date > 31u))
    {
        nano_rtc_print("  Invalid input.\r\n");
        return;
    }

    /* Prompt for weekday using 1=Monday…7=Sunday convention. */
    nano_rtc_print("  Weekday (1=Mon...7=Sun): ");
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    weekdayInput = (uint8_t)atoi(inputBuf);
    if ((weekdayInput < 1u) || (weekdayInput > 7u))
    {
        nano_rtc_print("  Invalid input.\r\n");
        return;
    }

    /* Convert user input (1-7, Mon-Sun) to chip format (0-6, Sun-Sat). */
    dateTime.weekday = (weekdayInput == 7u) ? 0u : weekdayInput;

    /* Prompt for and read hours; range depends on the current hour mode. */
    if (currentUse24h != 0u)
    {
        nano_rtc_print("  Hours (0-23): ");
    }
    else
    {
        nano_rtc_print("  Hours (1-12): ");
    }
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    dateTime.hours = (uint8_t)atoi(inputBuf);
    if (currentUse24h != 0u)
    {
        if (dateTime.hours > 23u)
        {
            nano_rtc_print("  Invalid input.\r\n");
            return;
        }
    }
    else
    {
        if ((dateTime.hours < 1u) || (dateTime.hours > 12u))
        {
            nano_rtc_print("  Invalid input.\r\n");
            return;
        }
    }

    /* In 12h mode, ask AM or PM; in 24h mode the ampm field is not used. */
    if (currentUse24h == 0u)
    {
        nano_rtc_print("  AM or PM? (a/p/B=back): ");
        rxByte = nano_rtc_read_char();
        nano_rtc_print("\r\n");

        if (nano_rtc_is_back_char(rxByte) != 0u)
        {
            return;
        }

        dateTime.ampm = ((rxByte == 'p') || (rxByte == 'P')) ? 1u : 0u;
    }
    else
    {
        dateTime.ampm = 0u;
    }

    /* Prompt for and read minutes. */
    nano_rtc_print("  Minutes (0-59): ");
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    dateTime.minutes = (uint8_t)atoi(inputBuf);
    if (dateTime.minutes > 59u)
    {
        nano_rtc_print("  Invalid input.\r\n");
        return;
    }

    /* Prompt for and read seconds. */
    nano_rtc_print("  Seconds (0-59): ");
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    dateTime.seconds = (uint8_t)atoi(inputBuf);
    if (dateTime.seconds > 59u)
    {
        nano_rtc_print("  Invalid input.\r\n");
        return;
    }

    /* Write the assembled date/time structure to the RTC. */
    if (RV3028_SetDateTime(&dateTime) != RV3028_OK)
    {
        nano_rtc_print("  Error: RTC write failed.\r\n");
        return;
    }

    /* Confirm successful write to the user. */
    nano_rtc_print("  Time set successfully.\r\n");
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_menu_alarm
 | @brief   Prompts the user to configure an alarm: hours (range based on current hour mode),
 |          AM/PM (only in 12h mode), minutes, per-field match enables, and day-of-month vs.
 |          weekday selection. Writes the assembled configuration to the RTC and enables the
 |          alarm interrupt on the INT pin.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_menu_alarm(void)
{
    char inputBuf[16];
    RV3028_Alarm alarmCfg;
    uint8_t rxByte;
    uint8_t useDate;
    uint8_t weekdayInput;

    /* Draw the banner and submenu title before collecting input. */
    nano_rtc_print_banner();
    nano_rtc_print("------------------------------------------------------------------\r\n");
    nano_rtc_print("  SET ALARM\r\n");
    nano_rtc_print("  Press 'B' to go back\r\n");
    nano_rtc_print("------------------------------------------------------------------\r\n");

    /* Zero-initialise the struct so unused fields are well-defined. */
    memset(&alarmCfg, 0, sizeof(alarmCfg));

    /* Prompt for and read the alarm hours; range depends on the current hour mode. */
    if (currentUse24h != 0u)
    {
        nano_rtc_print("  Hours (0-23): ");
    }
    else
    {
        nano_rtc_print("  Hours (1-12): ");
    }
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    alarmCfg.hours = (uint8_t)atoi(inputBuf);
    if (currentUse24h != 0u)
    {
        if (alarmCfg.hours > 23u)
        {
            nano_rtc_print("  Invalid input.\r\n");
            return;
        }
    }
    else
    {
        if ((alarmCfg.hours < 1u) || (alarmCfg.hours > 12u))
        {
            nano_rtc_print("  Invalid input.\r\n");
            return;
        }
    }

    /* In 12h mode, ask AM or PM; in 24h mode the ampm field stays zero. */
    if (currentUse24h == 0u)
    {
        nano_rtc_print("  AM or PM? (a/p/B=back): ");
        rxByte = nano_rtc_read_char();
        nano_rtc_print("\r\n");

        if (nano_rtc_is_back_char(rxByte) != 0u)
        {
            return;
        }

        alarmCfg.ampm = ((rxByte == 'p') || (rxByte == 'P')) ? 1u : 0u;
    }

    /* Prompt for and read the alarm minutes. */
    nano_rtc_print("  Minutes (0-59): ");
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    alarmCfg.minutes = (uint8_t)atoi(inputBuf);
    if (alarmCfg.minutes > 59u)
    {
        nano_rtc_print("  Invalid input.\r\n");
        return;
    }

    /* Ask whether the hours field should participate in the alarm match. */
    nano_rtc_print("  Match hours? (y/n/B): ");
    rxByte = nano_rtc_read_char();
    nano_rtc_print("\r\n");

    /* Return to settings menu if the user pressed the back key. */
    if (nano_rtc_is_back_char(rxByte) != 0u)
    {
        return;
    }
    alarmCfg.enable_hours_match = ((rxByte == 'y') || (rxByte == 'Y')) ? 1u : 0u;

    /* Ask whether the minutes field should participate in the alarm match. */
    nano_rtc_print("  Match minutes? (y/n/B): ");
    rxByte = nano_rtc_read_char();
    nano_rtc_print("\r\n");

    /* Return to settings menu if the user pressed the back key. */
    if (nano_rtc_is_back_char(rxByte) != 0u)
    {
        return;
    }
    alarmCfg.enable_minutes_match = ((rxByte == 'y') || (rxByte == 'Y')) ? 1u : 0u;

    /* Ask whether the weekday/date field should participate in the alarm match. */
    nano_rtc_print("  Match weekday/date? (y/n/B): ");
    rxByte = nano_rtc_read_char();
    nano_rtc_print("\r\n");

    /* Return to settings menu if the user pressed the back key. */
    if (nano_rtc_is_back_char(rxByte) != 0u)
    {
        return;
    }
    alarmCfg.enable_weekday_date_match = ((rxByte == 'y') || (rxByte == 'Y')) ? 1u : 0u;

    /* Ask whether the alarm day field should compare against date or weekday. */
    nano_rtc_print("  Use date or weekday? (d/w/B): ");
    rxByte = nano_rtc_read_char();
    nano_rtc_print("\r\n");

    /* Return to settings menu if the user pressed the back key. */
    if (nano_rtc_is_back_char(rxByte) != 0u)
    {
        return;
    }
    useDate = ((rxByte == 'd') || (rxByte == 'D')) ? 1u : 0u;

    /* Collect the day-of-month value when comparing against calendar date. */
    if (useDate != 0u)
    {
        /* Prompt for and read the day-of-month for the alarm comparison. */
        nano_rtc_print("  Date (1-31): ");
        nano_rtc_read_line(inputBuf, sizeof(inputBuf));

        /* Return to settings menu if the user entered the back key. */
        if (nano_rtc_is_back_line(inputBuf) != 0u)
        {
            return;
        }
        alarmCfg.weekday_date = (uint8_t)atoi(inputBuf);
        if ((alarmCfg.weekday_date < 1u) || (alarmCfg.weekday_date > 31u))
        {
            nano_rtc_print("  Invalid input.\r\n");
            return;
        }
    }
    else
    {
        /* Prompt for weekday using 1=Monday…7=Sunday convention. */
        nano_rtc_print("  Weekday (1=Mon...7=Sun): ");
        nano_rtc_read_line(inputBuf, sizeof(inputBuf));

        /* Return to settings menu if the user entered the back key. */
        if (nano_rtc_is_back_line(inputBuf) != 0u)
        {
            return;
        }
        weekdayInput = (uint8_t)atoi(inputBuf);
        if ((weekdayInput < 1u) || (weekdayInput > 7u))
        {
            nano_rtc_print("  Invalid input.\r\n");
            return;
        }

        /* Convert user input (1-7, Mon-Sun) to chip format (0-6, Sun-Sat). */
        alarmCfg.weekday_date = (weekdayInput == 7u) ? 0u : weekdayInput;
    }

    /* Write the alarm configuration to the RTC. */
    if (RV3028_SetAlarm(&alarmCfg, useDate) != RV3028_OK)
    {
        nano_rtc_print("  Error: RTC write failed.\r\n");
        return;
    }

    /* Enable the alarm interrupt so the INT pin is asserted on alarm match. */
    RV3028_EnableAlarmInterrupt(1u);

    /* Confirm successful alarm configuration to the user. */
    nano_rtc_print("  Alarm set.\r\n");
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_menu_timer
 | @brief   Prompts the user to select a clock source, enter a 12-bit reload value, and choose
 |          whether the timer should repeat on expiry. Writes the countdown timer configuration
 |          and enables the timer interrupt on the INT pin.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_menu_timer(void)
{
    char inputBuf[16];
    uint8_t rxByte;
    uint16_t timerValue;
    uint8_t repeat;
    RV3028_TimerClkSrc clkSrc;

    /* Draw the banner and submenu title before collecting input. */
    nano_rtc_print_banner();
    nano_rtc_print("------------------------------------------------------------------\r\n");
    nano_rtc_print("  SET COUNTDOWN TIMER\r\n");
    nano_rtc_print("  Press 'B' to go back\r\n");
    nano_rtc_print("------------------------------------------------------------------\r\n");

    /* Display available clock source options for the countdown timer. */
    nano_rtc_print("  Clock source:\r\n");
    nano_rtc_print("  1. 4096 Hz\r\n");
    nano_rtc_print("  2. 64 Hz\r\n");
    nano_rtc_print("  3. 1 Hz\r\n");
    nano_rtc_print("  4. 1/60 Hz\r\n");
    nano_rtc_print("  Choice (B=back): ");

    /* Read a single keypress for the clock source selection. */
    rxByte = nano_rtc_read_char();
    nano_rtc_print("\r\n");

    /* Return to settings menu if the user pressed the back key. */
    if (nano_rtc_is_back_char(rxByte) != 0u)
    {
        return;
    }

    /* Map the user's keypress to the corresponding timer clock source enum. */
    switch (rxByte)
    {
        case '2': clkSrc = RV3028_TIMER_64HZ;   break;
        case '3': clkSrc = RV3028_TIMER_1HZ;    break;
        case '4': clkSrc = RV3028_TIMER_1_60HZ; break;
        default:  clkSrc = RV3028_TIMER_4096HZ; break;
    }

    /* Prompt for and read the 12-bit timer reload value. */
    nano_rtc_print("  Timer value (0-4095): ");
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    timerValue = (uint16_t)atoi(inputBuf);
    if (timerValue > 4095u)
    {
        nano_rtc_print("  Invalid input.\r\n");
        return;
    }

    /* Ask whether the timer should automatically restart after expiry. */
    nano_rtc_print("  Repeat? (y/n/B): ");
    rxByte = nano_rtc_read_char();
    nano_rtc_print("\r\n");

    /* Return to settings menu if the user pressed the back key. */
    if (nano_rtc_is_back_char(rxByte) != 0u)
    {
        return;
    }
    repeat = ((rxByte == 'y') || (rxByte == 'Y')) ? 1u : 0u;

    /* Write the countdown timer configuration and start it immediately. */
    if (RV3028_SetCountdownTimer(timerValue, clkSrc, repeat, 1u) != RV3028_OK)
    {
        nano_rtc_print("  Error: RTC write failed.\r\n");
        return;
    }

    /* Enable the timer interrupt so the INT pin is asserted on expiry. */
    RV3028_EnableTimerInterrupt(1u);

    /* Confirm successful timer configuration to the user. */
    nano_rtc_print("  Timer set.\r\n");
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_menu_clkout
 | @brief   Presents the available CLKOUT frequencies (32768 Hz down to disabled) and writes the
 |          selected frequency and enable state to the RTC EEPROM CLKOUT register.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_menu_clkout(void)
{
    uint8_t rxByte;
    uint8_t enable;
    RV3028_ClkoutFreq freq;

    /* Draw the banner and submenu title before collecting input. */
    nano_rtc_print_banner();
    nano_rtc_print("------------------------------------------------------------------\r\n");
    nano_rtc_print("  SET CLKOUT FREQUENCY\r\n");
    nano_rtc_print("  Press 'B' to go back\r\n");
    nano_rtc_print("------------------------------------------------------------------\r\n");

    /* Display available CLKOUT frequency options. */
    nano_rtc_print("  1. 32768 Hz\r\n");
    nano_rtc_print("  2. 8192 Hz\r\n");
    nano_rtc_print("  3. 1024 Hz\r\n");
    nano_rtc_print("  4. 64 Hz\r\n");
    nano_rtc_print("  5. 32 Hz\r\n");
    nano_rtc_print("  6. 1 Hz\r\n");
    nano_rtc_print("  7. Timer\r\n");
    nano_rtc_print("  8. Disable\r\n");
    nano_rtc_print("  Choice (B=back): ");

    /* Read a single keypress for the frequency selection. */
    rxByte = nano_rtc_read_char();
    nano_rtc_print("\r\n");

    /* Return to settings menu if the user pressed the back key. */
    if (nano_rtc_is_back_char(rxByte) != 0u)
    {
        return;
    }

    /* Default to 1 Hz enabled; option 8 overrides enable to off. */
    freq   = RV3028_CLKOUT_1HZ;
    enable = 1u;

    /* Map the user's keypress to the corresponding frequency enum. */
    switch (rxByte)
    {
        case '1': freq = RV3028_CLKOUT_32768HZ;               break;
        case '2': freq = RV3028_CLKOUT_8192HZ;                break;
        case '3': freq = RV3028_CLKOUT_1024HZ;                break;
        case '4': freq = RV3028_CLKOUT_64HZ;                  break;
        case '5': freq = RV3028_CLKOUT_32HZ;                  break;
        case '6': freq = RV3028_CLKOUT_1HZ;                   break;
        case '7': freq = RV3028_CLKOUT_TIMER;                 break;
        case '8': freq = RV3028_CLKOUT_LOW; enable = 0u;      break;
        default:                                              break;
    }

    /* Apply the selected frequency and enable/disable state to the CLKOUT pin. */
    if (RV3028_SetClkout(freq, enable) != RV3028_OK)
    {
        nano_rtc_print("  Error: RTC write failed.\r\n");
        return;
    }

    /* Confirm successful CLKOUT configuration to the user. */
    nano_rtc_print("  CLKOUT configured.\r\n");
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_menu_backup
 | @brief   Presents backup switchover modes (disabled, direct, level) and prompts for the
 |          switchover interrupt enable; writes the selection to the EEPROM BACKUP register.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_menu_backup(void)
{
    uint8_t rxByte;
    uint8_t bsieEnable;
    RV3028_BackupMode mode;

    /* Draw the banner and submenu title before collecting input. */
    nano_rtc_print_banner();
    nano_rtc_print("------------------------------------------------------------------\r\n");
    nano_rtc_print("  CONFIGURE BACKUP MODE\r\n");
    nano_rtc_print("  Press 'B' to go back\r\n");
    nano_rtc_print("------------------------------------------------------------------\r\n");

    /* Display available backup switchover mode options. */
    nano_rtc_print("  1. Disabled\r\n");
    nano_rtc_print("  2. Direct Switching\r\n");
    nano_rtc_print("  3. Level Switching\r\n");
    nano_rtc_print("  Choice (B=back): ");

    /* Read a single keypress for the backup mode selection. */
    rxByte = nano_rtc_read_char();
    nano_rtc_print("\r\n");

    /* Return to settings menu if the user pressed the back key. */
    if (nano_rtc_is_back_char(rxByte) != 0u)
    {
        return;
    }

    /* Map the user's keypress to the corresponding backup mode enum. */
    switch (rxByte)
    {
        case '2': mode = RV3028_BSM_DIRECT;   break;
        case '3': mode = RV3028_BSM_LEVEL;    break;
        default:  mode = RV3028_BSM_DISABLED; break;
    }

    /* Ask whether to enable the backup switchover interrupt. */
    nano_rtc_print("  Enable switchover interrupt? (y/n/B): ");
    rxByte = nano_rtc_read_char();
    nano_rtc_print("\r\n");

    /* Return to settings menu if the user pressed the back key. */
    if (nano_rtc_is_back_char(rxByte) != 0u)
    {
        return;
    }
    bsieEnable = ((rxByte == 'y') || (rxByte == 'Y')) ? 1u : 0u;

    /* Write the backup configuration to the RTC EEPROM. */
    if (RV3028_ConfigureBackup(mode, bsieEnable) != RV3028_OK)
    {
        nano_rtc_print("  Error: RTC write failed.\r\n");
        return;
    }

    /* Confirm successful backup configuration to the user. */
    nano_rtc_print("  Backup configured.\r\n");
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_menu_offset
 | @brief   Prompts the user for a signed frequency correction value in the range [-256, +255]
 |          and writes it to the RTC EEPROM calibration registers.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_menu_offset(void)
{
    char inputBuf[16];
    int16_t freqOffset;

    /* Draw the banner and submenu title before collecting input. */
    nano_rtc_print_banner();
    nano_rtc_print("------------------------------------------------------------------\r\n");
    nano_rtc_print("  SET FREQUENCY OFFSET\r\n");
    nano_rtc_print("  Press 'B' to go back\r\n");
    nano_rtc_print("------------------------------------------------------------------\r\n");

    /* Prompt for and read the signed frequency correction value. */
    nano_rtc_print("  Enter offset (-256 to 255): ");
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    freqOffset = (int16_t)atoi(inputBuf);
    if ((freqOffset < -256) || (freqOffset > 255))
    {
        nano_rtc_print("  Invalid input.\r\n");
        return;
    }

    /* Write the frequency offset to the RTC EEPROM calibration register. */
    if (RV3028_SetFrequencyOffset(freqOffset) != RV3028_OK)
    {
        nano_rtc_print("  Error: RTC write failed.\r\n");
        return;
    }

    /* Confirm successful offset write to the user. */
    nano_rtc_print("  Offset set.\r\n");
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_menu_unix
 | @brief   Prompts the user for a 32-bit UNIX timestamp (seconds since 1970-01-01) and writes
 |          it to the RTC UNIX time counter registers.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_menu_unix(void)
{
    char inputBuf[16];
    uint32_t unixTime;
    long rawUnix;

    /* Draw the banner and submenu title before collecting input. */
    nano_rtc_print_banner();
    nano_rtc_print("------------------------------------------------------------------\r\n");
    nano_rtc_print("  SET UNIX TIME\r\n");
    nano_rtc_print("  Press 'B' to go back\r\n");
    nano_rtc_print("------------------------------------------------------------------\r\n");

    /* Prompt for and read the 32-bit Unix timestamp. */
    nano_rtc_print("  Enter Unix time (seconds since 1970-01-01): ");
    nano_rtc_read_line(inputBuf, sizeof(inputBuf));

    /* Return to settings menu if the user entered the back key. */
    if (nano_rtc_is_back_line(inputBuf) != 0u)
    {
        return;
    }
    rawUnix = atol(inputBuf);
    if (rawUnix < 0)
    {
        nano_rtc_print("  Invalid input.\r\n");
        return;
    }
    unixTime = (uint32_t)rawUnix;

    /* Write the Unix timestamp to the RTC counter registers. */
    if (RV3028_SetUnixTime(unixTime) != RV3028_OK)
    {
        nano_rtc_print("  Error: RTC write failed.\r\n");
        return;
    }

    /* Confirm successful write to the user. */
    nano_rtc_print("  Unix time set.\r\n");
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_menu_hourmode
 | @brief   Prompts the user to select 24-hour or 12-hour AM/PM mode, writes the selection to
 |          CTRL2 via the driver, and updates the module-level currentUse24h variable so the
 |          clock display reflects the change immediately.
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_menu_hourmode(void)
{
    uint8_t rxByte;
    uint8_t use24h;

    /* Draw the banner and submenu title before collecting input. */
    nano_rtc_print_banner();
    nano_rtc_print("------------------------------------------------------------------\r\n");
    nano_rtc_print("  SET HOUR MODE\r\n");
    nano_rtc_print("  Press 'B' to go back\r\n");
    nano_rtc_print("------------------------------------------------------------------\r\n");

    /* Display the two available hour modes. */
    nano_rtc_print("  1. 24-hour mode\r\n");
    nano_rtc_print("  2. 12-hour mode (AM/PM)\r\n");
    nano_rtc_print("  Choice (B=back): ");

    /* Read a single keypress for the mode selection. */
    rxByte = nano_rtc_read_char();
    nano_rtc_print("\r\n");

    /* Return to settings menu if the user pressed the back key. */
    if (nano_rtc_is_back_char(rxByte) != 0u)
    {
        return;
    }

    /* Map the keypress to the use24h flag expected by the driver. */
    use24h = (rxByte == '1') ? 1u : 0u;

    /* Write the hour mode to the RTC control register. */
    if (RV3028_Set12_24Mode(use24h) != RV3028_OK)
    {
        nano_rtc_print("  Error: RTC write failed.\r\n");
        return;
    }

    /* Mirror the chip mode in the module variable so the clock display updates. */
    currentUse24h = use24h;

    /* Confirm the applied mode to the user. */
    if (use24h != 0u)
    {
        nano_rtc_print("  24-hour mode set.\r\n");
    }
    else
    {
        nano_rtc_print("  12-hour (AM/PM) mode set.\r\n");
    }
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_print_clock_panel
 | @brief   Renders the boxed clock panel over USART2. On the first call the panel is printed
 |          fresh; on every subsequent call ANSI cursor-up escape moves back to the top of the
 |          panel so it redraws in-place without scrolling.
 |
 | @param   dt         Pointer to the current date/time from the RTC.
 | @param   unix       Current UNIX timestamp.
 | @param   vbackupOk  Non-zero when VBACKUP is nominal (BSF clear), zero when switched.
 | @param   use24h     Non-zero for 24-hour display, zero for 12-hour AM/PM display.
 | @param   event      Null-terminated string describing the last fired event ("---", "ALARM FIRED",
 |                     or "TIMER FIRED").
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_print_clock_panel(const RV3028_DateTime *dt,
                                       uint32_t unix,
                                       uint8_t vbackupOk,
                                       uint8_t use24h,
                                       const char *event)
{
    static const char *months[12] = {
        "January", "February", "March",     "April",   "May",      "June",
        "July",    "August",   "September", "October", "November", "December"
    };

    const char *weekday = (dt->weekday < 7u)                        ? weekdayNames[dt->weekday]   : "?";
    const char *month   = (dt->month >= 1u && dt->month <= 12u)     ? months[dt->month - 1u]      : "?";
    const char *vbat    = (vbackupOk != 0u)                         ? "OK"                        : "SWITCHED";

    char timeBuf[20];
    char dateBuf[36];
    char unixBuf[14];
    char rowBuf[80];

    /* Build the TIME field string. */
    if (use24h != 0u)
    {
        snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u",
                 (unsigned)dt->hours, (unsigned)dt->minutes, (unsigned)dt->seconds);
    }
    else
    {
        snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u %s",
                 (unsigned)dt->hours, (unsigned)dt->minutes, (unsigned)dt->seconds,
                 (dt->ampm != 0u) ? "PM" : "AM");
    }

    /* Build the DATE field string: "Wednesday 14 June 2026" */
    snprintf(dateBuf, sizeof(dateBuf), "%s %02u %s 20%02u",
             weekday, (unsigned)dt->date, month, (unsigned)dt->year);

    /* Build the UNIX field string. */
    snprintf(unixBuf, sizeof(unixBuf), "%lu", (unsigned long)unix);

    /* On every draw after the first, jump cursor up 10 lines to overwrite in-place. */
    if (clockPanelFirstDraw == 0u)
    {
        nano_rtc_print("\033[10A\r");
    }
    clockPanelFirstDraw = 0u;

    /* ── Box top ────────────────────────────────────────────────────── */
    nano_rtc_print("╔══════════════════════════════════════════════╗\r\n");

    /* ── Title row ──────────────────────────────────────────────────── */
    nano_rtc_print("║  NANO-RTC │  RV-3028-C7 High Precision RTC   ║\r\n");

    /* ── Separator ──────────────────────────────────────────────────── */
    nano_rtc_print("╠══════════════════════════════════════════════╣\r\n");

    /* ── TIME row  (value field padded to 32 chars) ──────────────────── */
    snprintf(rowBuf, sizeof(rowBuf), "║  TIME     │  %-32s║\r\n", timeBuf);
    nano_rtc_print(rowBuf);

    /* ── DATE row ────────────────────────────────────────────────────── */
    snprintf(rowBuf, sizeof(rowBuf), "║  DATE     │  %-32s║\r\n", dateBuf);
    nano_rtc_print(rowBuf);

    /* ── UNIX row ────────────────────────────────────────────────────── */
    snprintf(rowBuf, sizeof(rowBuf), "║  UNIX     │  %-32s║\r\n", unixBuf);
    nano_rtc_print(rowBuf);

    /* ── VBACKUP row ─────────────────────────────────────────────────── */
    snprintf(rowBuf, sizeof(rowBuf), "║  VBACKUP  │  %-32s║\r\n", vbat);
    nano_rtc_print(rowBuf);

    /* ── EVENT row ──────────────────────────────────────────────────── */
    snprintf(rowBuf, sizeof(rowBuf), "║  EVENT    │  %-32s║\r\n", event);
    nano_rtc_print(rowBuf);

    /* ── Box bottom ─────────────────────────────────────────────────── */
    nano_rtc_print("╚══════════════════════════════════════════════╝\r\n");

    /* ── Footer ─────────────────────────────────────────────────────── */
    nano_rtc_print("         Press 'S' for Settings\r\n");
}

/*--------------------------------------------------------------------------------------------------------
 | @fn      nano_rtc_run_settings
 | @brief   Displays the top-level settings menu in a loop and dispatches to the appropriate
 |          submenu function based on the user's keypress. Returns when the user selects Back
 |          (key '9', 'B', or 'b').
 |
 | @return  None.
 ---------------------------------------------------------------------------------------------------------*/

static void nano_rtc_run_settings(void)
{
    uint8_t rxByte;

    /* Loop until the user selects Back to return to the clock screen. */
    while (1)
    {
        /* Redraw the banner and settings menu on each entry or return from submenu. */
        nano_rtc_print_banner();
        nano_rtc_print("------------------------------------------------------------------\r\n");
        nano_rtc_print("  NANO-RTC SETTINGS\r\n");
        nano_rtc_print("------------------------------------------------------------------\r\n");
        nano_rtc_print("  1. Set Date & Time\r\n");
        nano_rtc_print("  2. Set Alarm\r\n");
        nano_rtc_print("  3. Set Countdown Timer\r\n");
        nano_rtc_print("  4. Set CLKOUT Frequency\r\n");
        nano_rtc_print("  5. Configure Backup Mode\r\n");
        nano_rtc_print("  6. Set Frequency Offset\r\n");
        nano_rtc_print("  7. Set Unix Time\r\n");
        nano_rtc_print("  8. Set Hour Mode (12h/24h)\r\n");
        nano_rtc_print("  9. Back\r\n");
        nano_rtc_print("------------------------------------------------------------------\r\n");
        nano_rtc_print("> ");

        /* Read a single keypress for the menu selection. */
        rxByte = nano_rtc_read_char();
        nano_rtc_print("\r\n");

        /* Dispatch to the selected submenu, or return on Back. */
        switch (rxByte)
        {
            case '1': nano_rtc_menu_datetime(); break;
            case '2': nano_rtc_menu_alarm();    break;
            case '3': nano_rtc_menu_timer();    break;
            case '4': nano_rtc_menu_clkout();   break;
            case '5': nano_rtc_menu_backup();   break;
            case '6': nano_rtc_menu_offset();   break;
            case '7': nano_rtc_menu_unix();     break;
            case '8': nano_rtc_menu_hourmode(); break;
            case '9':
            case 'B':
            case 'b': return;
            default:  break;
        }
    }
}

/*======================================================================================================
 =                                         Public functions                                             =
 ======================================================================================================*/

/*--------------------------------------------------------------------------------------------------------
 | @fn      NANO_RTC_Run
 | @brief   Initialises the RV-3028 and reads back the stored hour mode, then enters an infinite
 |          loop that checks for alarm and timer events, reads the current date/time and UNIX
 |          timestamp, and overwrites the clock line on USART2 at approximately 1 Hz. Pressing
 |          'S' suspends the clock loop and opens the interactive settings menu; the clock resumes
 |          when the user selects Back.
 |
 | @return  Does not return.
 ---------------------------------------------------------------------------------------------------------*/

void NANO_RTC_Run(void)
{
    uint8_t alarmFlag;
    uint8_t timerFlag;
    uint8_t rxByte;
    uint8_t statusReg;
    uint32_t unixTime;
    RV3028_DateTime dateTime;
    HAL_StatusTypeDef uartStatus;

    /* Initialise the RV-3028 driver and clear any stale interrupt flags. */
    if (RV3028_Init(&hi2c1) != RV3028_OK)
    {
        nano_rtc_print("  Error: RTC init failed.\r\n");
        while (1) {}
    }

    /* Read back the hour mode stored in the chip and sync the module variable. */
    RV3028_Get12_24Mode(&currentUse24h);

    /* Draw the startup splash screen with banner and clock-mode header. */
    nano_rtc_print_clock_header();

    /* Pause so the user can read the startup screen before the clock begins. */
    HAL_Delay(NANO_RTC_STARTUP_DELAY_MS);

    /* ---- Main clock loop ------------------------------------------ */
    while (1)
    {
        /* Read the alarm flag from the STATUS register. */
        alarmFlag = 0u;
        RV3028_GetAlarmFlag(&alarmFlag);

        /* Announce and clear an alarm event if one has fired. */
        if (alarmFlag != 0u)
        {
            /* Record the event so the clock panel EVENT row reflects it. */
            lastEvent = "ALARM FIRED";

            /* Clear the alarm flag in the STATUS register. */
            RV3028_ClearAlarmFlag();
        }

        /* Read the timer flag from the STATUS register. */
        timerFlag = 0u;
        RV3028_GetTimerFlag(&timerFlag);

        /* Announce and clear a timer event if one has fired. */
        if (timerFlag != 0u)
        {
            /* Record the event so the clock panel EVENT row reflects it. */
            lastEvent = "TIMER FIRED";

            /* Clear the timer flag in the STATUS register. */
            RV3028_ClearTimerFlag();
        }

        /* Wait up to NANO_RTC_UART_TICK_MS for a keypress (serves as ~1 Hz tick). */
        rxByte     = 0u;
        uartStatus = HAL_UART_Receive(&huart2, &rxByte, 1u, NANO_RTC_UART_TICK_MS);

        /* Enter settings when the user presses S or s. */
        if ((uartStatus == HAL_OK) && ((rxByte == 'S') || (rxByte == 's')))
        {
            /* Run the settings menu; returns when the user selects Back. */
            nano_rtc_run_settings();

            /* Restore the clock screen header after returning from settings. */
            nano_rtc_print_clock_header();

            /* Restart the loop to immediately check flags before the next tick. */
            continue;
        }

        /* Read the current date and time from the RTC. */
        RV3028_GetDateTime(&dateTime);

        /* Read the UNIX timestamp from the RTC. */
        unixTime = 0u;
        RV3028_GetUnixTime(&unixTime);

        /* Read the STATUS register to check the backup switchover flag. */
        statusReg = 0u;
        RV3028_GetStatus(&statusReg);

        /* Render the boxed clock panel, overwriting in-place after the first draw. */
        nano_rtc_print_clock_panel(&dateTime, unixTime,
                                   (statusReg & NANO_RTC_STATUS_BSF) == 0u,
                                   currentUse24h,
                                   lastEvent);
    }
}
