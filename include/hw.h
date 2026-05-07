/*
 * pico-umac pin definitions
 *
 * Centralized pinout configuration for pico-mac project.
 * All GPIO assignments are defined here to prevent conflicts.
 *
 * Copyright 2024 Matt Evans
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef HW_H
#define HW_H

/*==============================================================================
 * PINOUT SUMMARY
 *=============================================================================
 *
 * GPIO  Function           Used By         Notes
 * ----  ----------------   -------------   -------------------------------
 * 0     UART0 TX           stdio           Debug console
 * 1     UART0 RX           stdio           Debug console
 * 2     SD SPI SCK         SD card         FatFS storage
 * 3     SD SPI TX/MOSI     SD card
 * 4     SD SPI RX/MISO     SD card
 * 5     SD SPI CS          SD card
 * 6     I2S BCLK           PCM5102 DAC     Bit clock for I2S audio
 * 7     I2S LRCLK          PCM5102 DAC     Left/right clock (WS)
 * 8     I2S DIN            PCM5102 DAC     Data to DAC
 * 15    PWM audio out      PWM audio       Single-pin audio output
 * 18    Video data[0]      VGA output      Base of 4-pin video data bus
 * 19    Video data[1]      VGA output
 * 20    Video data[2]      VGA output
 * 21    Video data[3]      VGA output
 * 22    Video VSYNC        VGA output
 * 23    Video CLK          VGA output      Pixel clock
 * 24    Video HSYNC        VGA output
 * 25    LED                onboard         Activity LED (PICO_DEFAULT_LED_PIN)
 *
 *==============================================================================
 * PWM PIN MAPPING (IMPORTANT!)
 *=============================================================================
 *
 * RP2350 PWM slices are fixed to specific GPIO pins:
 *
 * GPIO   PWM Slice   Channel   Typical Use
 * ----   ---------   -------   -----------
 * 0      0           A         UART TX (don't use for PWM)
 * 1      0           B         UART RX (don't use for PWM)
 * 2      1           A         SD SCK (conflict if using SD!)
 * 3      1           B
 * 4      2           A         SD MISO
 * 5      2           B         SD CS
 * 6      3           A         I2S BCLK (conflict with DAC!)
 * 7      3           B         I2S LRCLK
 * 8      4           A         I2S DIN
 * 9      4           B
 * 10     5           A
 * 11     5           B
 * 12     6           A
 * 13     6           B
 * 14     7           A
 * 15     7           B         <-- DEFAULT PWM AUDIO (GPIO_AUDIO_PIN)
 * 16     0           A
 * 17     0           B
 * 18     1           A         Video data[0] (conflict!)
 * 19     1           B         Video data[1]
 * 20     2           A         Video data[2]
 * 21     2           B         Video data[3]
 * 22     3           A         Video VSYNC
 * 23     3           B         Video CLK
 * 24     4           A         Video HSYNC
 * 25     4           B         Onboard LED
 *
 * CHANGING PWM AUDIO PIN:
 * -----------------------
 * If you move PWM audio from GPIO 15, you MUST update AUDIO_PWM_SLICE
 * and AUDIO_PWM_CHANNEL in audio_pwm.c to match the new GPIO:
 *
 *   GPIO 15 (default) -> Slice 7, Channel B
 *   GPIO 14           -> Slice 7, Channel A
 *   GPIO 12           -> Slice 6, Channel A
 *   GPIO 13           -> Slice 6, Channel B
 *   GPIO 10           -> Slice 5, Channel A
 *   GPIO 11           -> Slice 5, Channel B
 *
 * WARNING: PWM slices are shared between A and B channels. If you use
 * GPIO 14 (slice 7A), you cannot use GPIO 15 (slice 7B) for anything
 * else on the same PWM slice.
 *
 * CONFLICTS TO AVOID:
 * -------------------
 * - GPIO 0-1:  UART (stdio/debug)
 * - GPIO 2-5:  SD card SPI
 * - GPIO 6-8:  I2S DAC (when USE_DAC=ON)
 * - GPIO 18-24: Video output (when VGA enabled)
 * - GPIO 25:   Onboard LED
 *
 * Safe alternatives for PWM audio if GPIO 15 won't work:
 * - GPIO 10-11 (PWM slice 5): Available if not used elsewhere
 * - GPIO 12-13 (PWM slice 6): Available if not used elsewhere
 * - GPIO 14    (PWM slice 7A): Swaps with GPIO 15 (7B)
 */

/*==============================================================================
 * System Pins
 *=============================================================================*/

#define GPIO_LED_PIN    PICO_DEFAULT_LED_PIN

/*==============================================================================
 * Video Output Pins (VGA)
 *=============================================================================
 * Video uses 7 pins total: 4-bit data + VSYNC + CLK + HSYNC
 * Base pin is configurable at compile time (default 18)
 */

#define GPIO_VID_DATA   GPIO_VID_BASE
#define GPIO_VID_VS     (GPIO_VID_DATA + 1)
#define GPIO_VID_CLK    (GPIO_VID_VS + 1)
#define GPIO_VID_HS     (GPIO_VID_CLK + 1)

/*==============================================================================
 * Audio Output Pins
 *=============================================================================
 * Two audio options are supported (mutually exclusive at compile time):
 *
 * 1. PWM Audio (USE_PWM_AUDIO=ON):
 *    Single-pin output on GPIO_AUDIO_PIN (default 15)
 *    Uses PWM slice 7, channel B
 *
 * 2. I2S DAC Audio (USE_DAC=ON):
 *    Three-pin I2S interface to PCM5102 DAC
 *    BCLK (bit clock), LRCLK (word select), DIN (data)
 */

/* PWM Audio output pin - single pin, simple RC filter or direct drive */
/* Default: GPIO 15 (PWM slice 7, channel B) */
/* See PWM PIN MAPPING table above if changing this */
#ifndef GPIO_AUDIO_PIN
#define GPIO_AUDIO_PIN  15
#endif

/* I2S Audio pins for PCM5102 DAC (used when USE_DAC=ON) */
/* These pins use PIO, not PWM, so no slice/channel constraints */
#ifndef GPIO_I2S_DIN_PIN
#define GPIO_I2S_DIN_PIN    8   /* Data input to DAC (SD/DIN pin) */
#endif

#ifndef GPIO_I2S_BCLK_PIN
#define GPIO_I2S_BCLK_PIN   6   /* Bit clock (BCK pin) */
#endif

#ifndef GPIO_I2S_LRCLK_PIN
#define GPIO_I2S_LRCLK_PIN  7   /* Left/right clock / word select (LCK pin) */
#endif

/*==============================================================================
 * SD Card Pins (SPI)
 *=============================================================================
 * Used when USE_SD=ON for disk image storage
 * Default SPI pins - can be overridden at cmake time
 */

#ifndef SD_SCK
#define SD_SCK  2   /* SPI clock */
#endif

#ifndef SD_TX
#define SD_TX   3   /* SPI MOSI */
#endif

#ifndef SD_RX
#define SD_RX   4   /* SPI MISO */
#endif

#ifndef SD_CS
#define SD_CS   5   /* SPI chip select */
#endif

/*==============================================================================
 * Pin Conflict Validation
 *=============================================================================
 * Compile-time checks for common misconfigurations
 */

#if defined(USE_DAC) && defined(USE_PWM_AUDIO)
#warning "Both USE_DAC and USE_PWM_AUDIO defined - only one audio output allowed"
#endif

#if GPIO_AUDIO_PIN >= 18 && GPIO_AUDIO_PIN <= 24
#warning "GPIO_AUDIO_PIN conflicts with video output pins (18-24)"
#endif

#if GPIO_AUDIO_PIN >= 2 && GPIO_AUDIO_PIN <= 5
#warning "GPIO_AUDIO_PIN conflicts with SD card pins (2-5)"
#endif

#if GPIO_AUDIO_PIN >= 6 && GPIO_AUDIO_PIN <= 8 && defined(USE_DAC)
#warning "GPIO_AUDIO_PIN conflicts with I2S DAC pins (6-8)"
#endif

#endif /* HW_H */
