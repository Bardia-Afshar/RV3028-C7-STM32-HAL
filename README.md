# RV-3028 STM32 Driver

A full-featured driver for the [Micro Crystal RV-3028-C7](https://www.microcrystal.com/en/products/real-time-clock-rtc-modules/rv-3028-c7/) extreme low power RTC module, written in C. Works out of the box with STM32 HAL — change one include line to target any STM32 series, or port the thin I²C abstraction layer to any other microcontroller.

![Language](https://img.shields.io/badge/language-C-blue)
![Platform](https://img.shields.io/badge/platform-any-brightgreen)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

---

## Table of Contents

- [Features](#features)
- [Hardware](#hardware)
- [Developer Board](#developer-board)
- [File Structure](#file-structure)
- [Integration](#integration)
- [Concepts](#concepts)
  - [Date and Time](#date-and-time)
  - [UNIX Timestamp](#unix-timestamp)
  - [12-Hour and 24-Hour Mode](#12-hour-and-24-hour-mode)
  - [Alarm](#alarm)
  - [Countdown Timer](#countdown-timer)
  - [Update Interrupt](#update-interrupt)
  - [CLKOUT](#clkout)
  - [EEPROM](#eeprom)
  - [Backup Switchover](#backup-switchover)
  - [Trickle Charger](#trickle-charger)
  - [Event Timestamp](#event-timestamp)
  - [Frequency Offset](#frequency-offset)
  - [Status and Flags](#status-and-flags)

---

## Features

- Set and get full date/time with BCD encoding handled internally
- 12-hour and 24-hour mode
- UNIX timestamp read/write
- Alarm with per-field enable flags and weekday/date selection
- Periodic countdown timer with configurable clock source and repeat mode
- Periodic time update interrupt (per second or per minute)
- External event timestamp on EVI pin
- CLKOUT frequency configuration (32768 Hz down to 1 Hz, or timer-driven)
- EEPROM single-byte read/write and full RAM-to-EEPROM flush
- Automatic backup switchover (Direct and Level Switching Mode)
- Trickle charger for supercapacitor backup
- Crystal frequency offset calibration (9-bit signed, ±256 steps)
- Full status register and individual flag access

---

## Hardware

| Property | Value |
|---|---|
| Device | Micro Crystal RV-3028-C7 |
| Interface | I²C (up to 400 kHz) |
| I²C Address | 0x52 (7-bit) / 0xA4 write, 0xA5 read |
| Supply voltage | 1.2 V – 5.5 V |
| Current consumption | 45 nA typical |

---

## Developer Board

<img width="400" alt="RV3028_DEV" src="https://github.com/user-attachments/assets/1c44855e-5b83-4d43-8ac3-c7298485f138" />


Don't want to hand-solder the RV-3028-C7 yourself? I designed a dedicated developer board for this driver — it carries the RTC, decoupling capacitors, I²C pull-up resistors, and a CR1220 backup battery holder in a compact breakout form factor.


All hardware files are in the [`Hardware/`](Hardware/) folder:

```
Hardware/
├── Fabrication/      # BOM and CPL files
├── Gerber/           # Gerber files
└── Schematic/        # Full schematic
```

---

## File Structure

```
RV3028-C7-STM32-HAL/
├── Firmware/
│   ├── rv3028.h          # Public API
│   ├── rv3028.c          # Driver implementation
│   ├── rv3028_i2c.h      # I²C abstraction interface
│   ├── rv3028_i2c.c      # STM32 HAL implementation
│   └── Example_NanoRTC/  # Example project — NUCLEO-G431KB, outputs date/time over UART
├── Hardware/             # Schematic, Gerbers, BOM
├── LICENSE
└── README.md
```

The driver targets STM32 HAL out of the box via `rv3028_i2c.c`. To use a different STM32 series, change one include line. To port to another microcontroller entirely — ESP32, nRF52, bare-metal AVR — replace only `rv3028_i2c.c` with your own I²C implementation.

---

## Integration

### 1. Implement the I²C abstraction for your platform

`rv3028_i2c.h` declares five functions that the driver calls internally. You provide the implementation in `rv3028_i2c.c`:

```c
void     RV3028_I2C_Init(void *handle);                                              // store your peripheral handle
int8_t   RV3028_I2C_Write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);
int8_t   RV3028_I2C_Read (uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);
uint32_t RV3028_GetTick(void);    // return millisecond tick counter
void     RV3028_Delay(uint32_t ms);
```

Return `0` on success and any non-zero value on error from the Write and Read functions. `RV3028_GetTick()` must return a monotonically increasing `uint32_t` in milliseconds — unsigned subtraction handles 32-bit rollover correctly. The provided `rv3028_i2c.c` implements all five using STM32 HAL. For a different platform, replace only that file.

### 2. STM32 — change the HAL include if needed

The provided `rv3028_i2c.c` targets STM32G4. Change line 13 to match your series:

```c
#include "stm32g4xx_hal.h"   // STM32G4  ← default
#include "stm32f4xx_hal.h"   // STM32F4
#include "stm32h7xx_hal.h"   // STM32H7
```

### 3. Initialize in your application

Call `RV3028_Init` once after your I²C peripheral is initialized. Pass a pointer to your HAL I²C handle — the driver stores it internally and all other functions use it automatically. Subsequent calls to `RV3028_Init` are a no-op and return `RV3028_OK` immediately, so it is safe to call from multiple places.

```c
#include "rv3028.h"

// inside main(), after MX_I2C1_Init()
if (RV3028_Init(&hi2c1) != RV3028_OK)
{
    // I²C communication failed — check wiring and pull-up resistors
}
```

To tear down and re-initialize (e.g. after a peripheral reset), call `RV3028_DeInit()` first, then `RV3028_Init` again:

```c
RV3028_DeInit();

// reconfigure I²C peripheral if needed...

if (RV3028_Init(&hi2c1) != RV3028_OK)
{
    // handle error
}
```

### Return values

Every driver function returns `int8_t`:

| Value | Meaning |
|---|---|
| `RV3028_OK` (0) | Success |
| `RV3028_ERR` (-1) | I²C failure, NULL pointer, or invalid argument |

Always check the return value in production code. A returned `RV3028_ERR` means an I²C transaction failed, a NULL pointer was passed, or a parameter was out of range.

---

## Concepts

### Date and Time

The RV-3028 stores time in **BCD format** (Binary Coded Decimal) internally. BCD is a way of encoding each decimal digit in its own nibble — for example, the number 34 is stored as `0x34`, not `0x22`. The driver handles all BCD conversion internally so you always work with plain decimal values.

Time and date are represented using the `RV3028_DateTime` struct:

```c
typedef struct {
    uint8_t seconds;    // 0–59
    uint8_t minutes;    // 0–59
    uint8_t hours;      // 0–23 in 24h mode, 1–12 in 12h mode
    uint8_t weekday;    // 0–6, mapping is user-defined (e.g. 0=Sunday)
    uint8_t date;       // 1–31
    uint8_t month;      // 1–12
    uint8_t year;       // 0–99 (last two digits, e.g. 25 = 2025)
    uint8_t ampm;       // 0 = AM, 1 = PM — only used in 12h mode
} RV3028_DateTime;
```

**Set the time:**

```c
RV3028_DateTime dt;

dt.seconds = 0;
dt.minutes = 30;
dt.hours   = 14;
dt.weekday = 5;    // Friday in your mapping
dt.date    = 13;
dt.month   = 6;
dt.year    = 25;   // 2025
dt.ampm    = 0;    // ignored in 24h mode

if (RV3028_SetDateTime(&dt) != RV3028_OK)
{
    // handle error
}
```

**Read the time:**

```c
RV3028_DateTime now;

if (RV3028_GetDateTime(&now) == RV3028_OK)
{
    // now.hours, now.minutes, now.seconds etc. are populated
}
```

> **Note:** `RV3028_SetDateTime` validates all fields against their legal range and the current 12/24h mode before writing. Invalid values (e.g. month 13, or hour 0 in 12h mode) return `RV3028_ERR` without modifying any register.

> **Note:** The RTC keeps running while you read. The driver reads all 7 time/date bytes in a single I²C burst transaction, which gives you a consistent snapshot that cannot be split across a second boundary.

---

### UNIX Timestamp

A **UNIX timestamp** is the number of seconds elapsed since 1 January 1970 00:00:00 UTC. It is a single 32-bit integer that uniquely identifies any moment in time, regardless of timezone. It is widely used in embedded systems, logging, and communication protocols because it requires no struct, no formatting, and is trivially comparable and sortable.

The RV-3028 has a dedicated 4-byte UNIX time counter that increments automatically every second, independent of the calendar registers.

**Set UNIX time:**

```c
// 1 January 2024 00:00:00 UTC = 1704067200
if (RV3028_SetUnixTime(1704067200u) != RV3028_OK)
{
    // handle error
}
```

**Read UNIX time:**

```c
uint32_t ts;

if (RV3028_GetUnixTime(&ts) == RV3028_OK)
{
    // ts = seconds since epoch
    // use ts directly for logging, comparison, or transmission
}
```

> **Note:** The driver temporarily disables the countdown timer (TE bit) during a UNIX time write, as required by the datasheet, and restores the original CTRL1 state afterward. If the write fails mid-way, a best-effort CTRL1 restore is attempted before returning `RV3028_ERR`.

---

### 12-Hour and 24-Hour Mode

By default the RV-3028 operates in **24-hour mode** (hours 0–23). You can switch to **12-hour mode** (hours 1–12 with AM/PM) if your application requires it.

In 12-hour mode, the `ampm` field of `RV3028_DateTime` is used: `0` = AM, `1` = PM.

```c
// Switch to 12-hour mode
RV3028_Set12_24Mode(0);

RV3028_DateTime dt;
dt.hours = 3;
dt.ampm  = 1;   // 3:00 PM
// ... set other fields
RV3028_SetDateTime(&dt);

// Query the current mode at any time
uint8_t is24h;
RV3028_Get12_24Mode(&is24h);   // is24h = 1 → 24h, 0 → 12h

// Switch back to 24-hour mode
RV3028_Set12_24Mode(1);
```

> **Important:** The mode selection is stored in the CTRL2 RAM register. It does not automatically persist to EEPROM. If you power-cycle the device, the EEPROM default (24-hour) will be restored unless you call `RV3028_EEUpdateAll()` after switching modes.

---

### Alarm

The alarm fires when the current time matches the configured alarm fields. Each field (minutes, hours, weekday/date) can be individually **enabled or disabled** using the match flags. Setting a flag to `1` means "match this field" and `0` means "ignore this field" — so setting all three to `0` would match nothing and the alarm would never fire.

```c
typedef struct {
    uint8_t minutes;                   // alarm minutes value (0–59)
    uint8_t hours;                     // alarm hours value
    uint8_t weekday_date;              // weekday (0–6) or date (1–31) depending on useDate
    uint8_t enable_minutes_match;      // 1 = match minutes,      0 = ignore minutes
    uint8_t enable_hours_match;        // 1 = match hours,        0 = ignore hours
    uint8_t enable_weekday_date_match; // 1 = match weekday/date, 0 = ignore weekday/date
    uint8_t ampm;                      // 0 = AM, 1 = PM — only used in 12h mode
} RV3028_Alarm;
```

The `useDate` parameter in `RV3028_SetAlarm` selects whether `weekday_date` is interpreted as a weekday (`useDate = 0`) or a calendar date (`useDate = 1`).

**Example: alarm every day at 07:30**

```c
RV3028_Alarm alarm;

alarm.minutes                   = 30;
alarm.hours                     = 7;
alarm.weekday_date              = 0;
alarm.enable_minutes_match      = 1;   // match minutes
alarm.enable_hours_match        = 1;   // match hours
alarm.enable_weekday_date_match = 0;   // ignore weekday — fires every day
alarm.ampm                      = 0;   // ignored in 24h mode

RV3028_SetAlarm(&alarm, 0);       // 0 = weekday mode (ignored anyway since field is disabled)
RV3028_EnableAlarmInterrupt(1);   // enable INT pin output
```

**Example: alarm once, on a specific date**

```c
RV3028_Alarm alarm;

alarm.minutes                   = 0;
alarm.hours                     = 9;
alarm.weekday_date              = 15;  // 15th of the month
alarm.enable_minutes_match      = 1;
alarm.enable_hours_match        = 1;
alarm.enable_weekday_date_match = 1;
alarm.ampm                      = 0;

RV3028_SetAlarm(&alarm, 1);       // 1 = useDate mode
RV3028_EnableAlarmInterrupt(1);
```

**Check and clear the alarm flag:**

```c
uint8_t flag;
RV3028_GetAlarmFlag(&flag);
if (flag)
{
    // alarm fired
    RV3028_ClearAlarmFlag();
}
```

> **Tip:** You can poll the flag in your main loop, or wire the INT pin to a GPIO and handle it in an EXTI interrupt for true low-power operation.

---

### Countdown Timer

The countdown timer counts down from a preset value to zero, then sets the timer flag (TF) and optionally repeats. It is useful for periodic wake-up, watchdog-style timeouts, or any time-based event that doesn't need calendar precision.

The timer value is 12 bits (0–4095) and the clock source determines the tick rate:

```c
typedef enum {
    RV3028_TIMER_4096HZ = 0,  // finest resolution: ~244 µs per tick — max ~1 second
    RV3028_TIMER_64HZ,         // ~15.6 ms per tick — max ~64 seconds
    RV3028_TIMER_1HZ,          // 1 second per tick — max ~68 minutes
    RV3028_TIMER_1_60HZ        // 1 minute per tick — max ~2.8 days
} RV3028_TimerClkSrc;
```

**Example: 5-second single-shot timer**

```c
RV3028_SetCountdownTimer(5, RV3028_TIMER_1HZ, 0, 1);
// value=5, clock=1Hz → fires after 5 seconds
// repeat=0 → single shot
// enable=1 → start immediately

RV3028_EnableTimerInterrupt(1);

// later — poll the flag or handle in ISR:
uint8_t flag;
RV3028_GetTimerFlag(&flag);
if (flag)
{
    RV3028_ClearTimerFlag();
}
```

**Example: repeating 100 ms tick using 64 Hz source**

```c
// 64 Hz clock → 6 ticks ≈ 93.75 ms, 7 ticks ≈ 109 ms
RV3028_SetCountdownTimer(6, RV3028_TIMER_64HZ, 1, 1);
// repeat=1 → auto-reloads and fires continuously
```

**Read remaining count without stopping the timer:**

```c
uint16_t remaining;
RV3028_GetCountdownTimerCount(&remaining);   // reads TIMER_STATUS registers
```

> **Note:** `RV3028_GetCountdownTimer` reads the reload value (what you set), while `RV3028_GetCountdownTimerCount` reads the live countdown value (how many ticks remain). These are separate register pairs.

---

### Update Interrupt

The update interrupt fires automatically either **once per second** or **once per minute**, without needing to configure an alarm. It is useful for refreshing a display, logging, or any regular periodic task.

```c
// Fire every second
RV3028_EnableUpdateInterrupt(1, 0);   // enable=1, onMinute=0

// Fire every minute instead
RV3028_EnableUpdateInterrupt(1, 1);   // enable=1, onMinute=1

// Check flag
uint8_t flag;
RV3028_GetUpdateFlag(&flag);
if (flag)
{
    RV3028_ClearUpdateFlag();
    // do your periodic work here
}

// Disable
RV3028_EnableUpdateInterrupt(0, 0);
```

---

### CLKOUT

The CLKOUT pin outputs a square wave at a selectable frequency. It can be used to clock another IC, generate a precise timebase, or synchronize external hardware.

```c
typedef enum {
    RV3028_CLKOUT_32768HZ = 0,  // crystal frequency — most accurate
    RV3028_CLKOUT_8192HZ,
    RV3028_CLKOUT_1024HZ,
    RV3028_CLKOUT_64HZ,
    RV3028_CLKOUT_32HZ,
    RV3028_CLKOUT_1HZ,          // 1 Hz pulse — useful for LED heartbeat
    RV3028_CLKOUT_TIMER,        // follows the countdown timer interrupt
    RV3028_CLKOUT_LOW           // pin held LOW (disabled)
} RV3028_ClkoutFreq;
```

```c
// Output 1 Hz on CLKOUT pin
RV3028_SetClkout(RV3028_CLKOUT_1HZ, 1);

// Disable CLKOUT
RV3028_SetClkout(RV3028_CLKOUT_LOW, 0);
```

> **Note:** CLKOUT settings are stored in EEPROM (`0x35`) and persist across power cycles. `RV3028_SetClkout` writes the EEPROM byte and then issues a `REFRESH_ALL` command so the change takes effect in the RAM shadow registers immediately — no power cycle needed. The `REFRESH_ALL` command also refreshes the BACKUP and OFFSET shadow registers from EEPROM, so make sure those are already set to their intended values before calling this function.

---

### EEPROM

The RV-3028 contains 43 bytes of user-accessible EEPROM (addresses `0x00`–`0x2A`) plus configuration EEPROM at `0x35`–`0x37`. EEPROM is non-volatile — it survives power loss. The driver gives you direct byte-level access.

The configuration EEPROM registers (`0x35`–`0x37`) are mirrored in RAM. The RAM mirror is what the chip actually uses at runtime, but it is only refreshed from EEPROM at power-on. Writing to EEPROM therefore does not take effect in RAM until the next power-on — unless you call `RV3028_EEUpdateAll()` (which copies RAM→EEPROM) or the relevant high-level function (e.g. `RV3028_SetClkout` issues a `REFRESH_ALL` automatically).

**Write a byte to user EEPROM:**

```c
uint8_t myValue = 0x42;
RV3028_EEWriteByte(0x00, myValue);
```

**Read it back:**

```c
uint8_t data;
RV3028_EEReadByte(0x00, &data);
// data == 0x42
```

**Flush all RAM config to EEPROM:**

```c
// After changing CTRL registers you want to persist across power cycles:
RV3028_EEUpdateAll();
```

> **Warning:** EEPROM has a limited write endurance (~10,000 cycles per byte). Avoid writing to it in a fast loop. Use it for configuration that changes rarely — not for logging or counters.

> **Warning:** Do not write directly to EEPROM addresses `0x35`, `0x36`, or `0x37` using `RV3028_EEWriteByte`. Use `RV3028_SetClkout`, `RV3028_SetFrequencyOffset`, `RV3028_ConfigureBackup`, and `RV3028_ConfigureTrickleCharger` instead — those functions perform safe read-modify-write operations that preserve unrelated bits in those registers.

---

### Backup Switchover

The RV-3028 can automatically switch to a backup power supply (battery or supercapacitor on the VBACKUP pin) when the main VDD drops. This keeps the RTC running and timekeeping accurate during a power failure.

There are two switchover modes:

```c
typedef enum {
    RV3028_BSM_DISABLED = 0,  // no switchover — VBACKUP ignored
    RV3028_BSM_DIRECT   = 1,  // switch when VDD < VBACKUP
    RV3028_BSM_LEVEL    = 3   // switch when VDD < 2.0V AND VBACKUP > 2.0V
} RV3028_BackupMode;
```

**Direct Switching Mode (DSM):** switches as soon as VDD falls below VBACKUP. Use this with a coin cell.

**Level Switching Mode (LSM):** switches only when both conditions are met (VDD below 2.0 V and VBACKUP above 2.0 V). Use this with a supercapacitor to avoid switching on small VDD fluctuations.

The `bsieEnable` parameter enables a backup switchover interrupt on the INT pin when a switchover occurs.

```c
// Level switching mode, interrupt on switchover
RV3028_ConfigureBackup(RV3028_BSM_LEVEL, 1);

// Direct switching, no interrupt
RV3028_ConfigureBackup(RV3028_BSM_DIRECT, 0);

// Disable backup entirely
RV3028_ConfigureBackup(RV3028_BSM_DISABLED, 0);
```

> **Note:** Backup settings are stored in EEPROM register `0x37` and persist across power cycles.

---

### Trickle Charger

When using a **supercapacitor** as the backup source, the trickle charger slowly charges it from VDD through a series resistor. This keeps the supercapacitor topped up so it is ready when VDD drops.

Do not enable the trickle charger with a non-rechargeable battery (e.g. CR1220) — it will damage the battery.

```c
typedef enum {
    RV3028_TCR_3K  = 0,   // 3 kΩ — fastest charge, highest current
    RV3028_TCR_5K  = 1,
    RV3028_TCR_9K  = 2,
    RV3028_TCR_15K = 3    // 15 kΩ — slowest charge, lowest current
} RV3028_TrickleResistor;
```

Choose the resistor based on your supercapacitor's charge current rating. A larger resistor means lower charge current and longer charge time.

```c
// Enable trickle charger with 9 kΩ resistor
RV3028_ConfigureTrickleCharger(1, RV3028_TCR_9K);

// Disable
RV3028_ConfigureTrickleCharger(0, RV3028_TCR_9K);
```

> **Note:** Trickle charger settings are stored in EEPROM register `0x37` alongside the backup switchover configuration.

---

### Event Timestamp

The RV-3028 can record the exact date and time of an external event detected on the **EVI pin**. It stores the timestamp in dedicated read-only registers and keeps a count of how many events have occurred (up to 255, then wraps).

This is useful for logging button presses, power events, external triggers, or any signal edge you want to timestamp without involving the MCU.

```c
// Enable timestamping — overwriteOld=1 means each new event overwrites the previous
RV3028_EnableTimeStamp(1, 1);

// Later, read the stored timestamp
RV3028_DateTime ts;
uint8_t count;

if (RV3028_GetTimeStamp(&ts, &count) == RV3028_OK)
{
    // ts = date/time of the last event
    // count = number of events since last reset (wraps at 255)
    // ts.weekday is always 0 — weekday is not captured in the timestamp registers
}

// Check and clear the event flag
uint8_t flag;
RV3028_GetEventFlag(&flag);
if (flag)
{
    RV3028_ClearTimeStampFlag();
}

// Disable timestamping
RV3028_EnableTimeStamp(0, 0);
```

---

### Frequency Offset

The 32.768 kHz crystal is never perfectly accurate — it drifts slightly due to temperature, load capacitance, and manufacturing tolerance. The RV-3028 provides a **digital frequency offset register** that adds or subtracts small corrections to the oscillator, allowing you to compensate for measured drift.

The offset is a **9-bit signed value** stored across two EEPROM registers (`0x36` and bit 7 of `0x37`). Each step corresponds to approximately **0.238 ppm**. The full range covers ±256 steps, or roughly ±61 ppm — enough to correct any realistic crystal.

To calibrate: measure the actual drift of your RTC over a known period, calculate the ppm error, divide by 0.238 to get the number of steps, and set the offset.

```c
// Crystal runs 1 ppm fast → apply -4 steps to slow it down
// (-4 × 0.238 ppm = -0.952 ppm correction)
RV3028_SetFrequencyOffset(-4);

// Crystal runs slow → positive offset
RV3028_SetFrequencyOffset(10);

// No correction
RV3028_SetFrequencyOffset(0);
```

Valid range: -256 to +255. Values outside this range are clamped automatically.

> **Note:** The 9-bit offset is split across two EEPROM locations. The two writes are non-atomic — if the second write fails, the registers are left in an inconsistent state. The function returns `RV3028_ERR`, and the caller should retry the full call.

---

### Status and Flags

The status register contains flags that indicate which events have occurred. You can read the full register or check individual flags.

| Flag | Bit | Set when |
|---|---|---|
| `EVF` | 1 | External event detected on EVI pin |
| `AF` | 2 | Alarm matched |
| `TF` | 3 | Countdown timer reached zero |
| `UF` | 4 | Periodic update interval elapsed |
| `BSF` | 5 | Backup switchover occurred |

```c
// Read full status register (inspect any flag directly)
uint8_t status;
RV3028_GetStatus(&status);

// Check individual flags
uint8_t flag;
RV3028_GetAlarmFlag(&flag);
RV3028_GetTimerFlag(&flag);
RV3028_GetUpdateFlag(&flag);
RV3028_GetEventFlag(&flag);

// Clear individual flags
RV3028_ClearAlarmFlag();
RV3028_ClearTimerFlag();
RV3028_ClearUpdateFlag();
RV3028_ClearTimeStampFlag();   // clears EVF

// Clear everything at once
RV3028_ClearAllFlags();        // clears AF, TF, UF, EVF, BSF in one write
```

> **Note:** `RV3028_Init` calls `RV3028_ClearAllFlags` automatically on the first call, clearing any stale flags left from a previous power cycle.

---

