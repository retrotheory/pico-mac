/* audio_scanline.c - Audio synced to video scanlines (HBLANK)
 *
 * Outputs Mac audio samples synchronized to VGA scanline timing.
 * Uses fractional sample stepping to handle the mismatch between
 * VGA scanline rate (31.5 kHz) and Mac sample rate (22.255 kHz).
 *
 * Output is triggered from video DMA IRQ (scanline rate)
 */

#include <stdio.h>
#include <string.h>
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "audio.h"
#include "hw.h"

/*==============================================================================
 * Configuration
 *=============================================================================*/

/* PWM slice and channel for GPIO_AUDIO_PIN */
/* GPIO 15 maps to PWM7 B (slice 7, channel B) */
#define AUDIO_PWM_SLICE     7
#define AUDIO_PWM_CHANNEL   PWM_CHAN_B
#define AUDIO_GPIO          GPIO_AUDIO_PIN

/* Audio sample rate = video scanline rate */
#define AUDIO_SAMPLE_RATE   MAC_AUDIO_SAMPLE_RATE  /* 22254 Hz */

/*==============================================================================
 * Data Structures
 *=============================================================================*/

typedef struct {
    /* PWM configuration */
    uint pwm_slice;
    
    /* Audio state */
    audio_state_t state;
    
    /* Current sample from Mac RAM */
    uint8_t current_sample;
} audio_driver_t;

/*==============================================================================
 * Global State
 *=============================================================================*/

static audio_driver_t g_audio;

/*==============================================================================
 * PWM Initialization
 *=============================================================================*/

static void audio_init_pwm(void)
{
    g_audio.pwm_slice = AUDIO_PWM_SLICE;

    /* Set up GPIO for PWM */
    gpio_set_function(AUDIO_GPIO, GPIO_FUNC_PWM);

    /* Configure PWM */
    pwm_config cfg = pwm_get_default_config();

    /* Run at sample_rate * 256 for 8-bit resolution */
    float div = (float)clock_get_hz(clk_sys) / (float)(AUDIO_SAMPLE_RATE * 256);
    pwm_config_set_clkdiv(&cfg, div);
    pwm_config_set_wrap(&cfg, 255);

    /* Initialize but don't start yet */
    pwm_init(g_audio.pwm_slice, &cfg, false);
    pwm_set_chan_level(g_audio.pwm_slice, AUDIO_PWM_CHANNEL, 128);
}

/*==============================================================================
 * Sound Buffer Management
 *=============================================================================*/

static inline uint8_t *get_sound_buffer_address(void)
{
    /* Standard Mac sound buffer locations */
    /* Main buffer: RAM_SIZE - 0x300 */
    /* Alt buffer: RAM_SIZE - 0x5F00 */
    if (g_audio.state.main_buffer) {
        return g_audio.state.ram_base + (g_audio.state.ram_size - 0x300);
    } else {
        return g_audio.state.ram_base + (g_audio.state.ram_size - 0x5F00);
    }
}

/*==============================================================================
 * Scanline-based Audio Output (called from video IRQ)
 *=============================================================================
 *
 * VGA 640x480@60 timing: 525 total scanlines per frame (480 visible + 45 blanking)
 * Mac audio: 370 samples per frame at 22.254 kHz
 *
 * We need to output 370 samples spread across 525 scanlines.
 * Ratio: 525/370 = 1.4189 scanlines per sample
 *
 * Use fixed-point accumulator to track when to output samples:
 * - Increment by 1.0 (65536 in 16.16 format) each scanline
 * - Threshold = 65536 * 525 / 370 = 92984
 * - When accumulator >= threshold, output sample and subtract threshold
 */

/* Pre-computed audio frame buffer */
static uint8_t audio_frame_buffer[MAC_AUDIO_BUFFER_SIZE];
static volatile uint32_t audio_sample_idx = 0;

/* Fixed-point accumulator for fractional sample stepping */
/* 16.16 format: upper 16 bits = integer, lower 16 bits = fraction */
#define SCANLINES_PER_FRAME     525     /* VGA total scanlines */
#define SAMPLES_PER_FRAME       370     /* Mac samples per frame */
#define FP_SHIFT                16
#define FP_ONE                  (1 << FP_SHIFT)  /* 1.0 in fixed-point */
#define SAMPLE_STEP_THRESHOLD   ((FP_ONE * SCANLINES_PER_FRAME) / SAMPLES_PER_FRAME)

static volatile uint32_t scanline_accumulator = 0;

/* Called once per scanline (HBLANK) - must be FAST (IRQ context) */
void audio_hblank_callback(void)
{
    /* Increment accumulator each scanline */
    scanline_accumulator += FP_ONE;
    
    /* Check if it's time to output a sample */
    if (scanline_accumulator >= SAMPLE_STEP_THRESHOLD) {
        scanline_accumulator -= SAMPLE_STEP_THRESHOLD;
        
        /* Output pre-computed sample from frame buffer */
        uint8_t sample = audio_frame_buffer[audio_sample_idx];
        audio_sample_idx++;
        if (audio_sample_idx >= MAC_AUDIO_BUFFER_SIZE) {
            audio_sample_idx = 0;
        }
        pwm_set_chan_level(g_audio.pwm_slice, AUDIO_PWM_CHANNEL, sample);
    }
}



/* Frame update - called from main loop, prepare next frame of audio */
void audio_frame_update(void)
{
    /* If Mac has disabled sound via VIA, output silence immediately */
    if (!g_audio.state.sound_enabled) {
        memset(audio_frame_buffer, 0x80, MAC_AUDIO_BUFFER_SIZE);
        return;
    }
    
    uint8_t vol = g_audio.state.volume;  /* 0-7, where 7 is full volume */
    uint8_t *snd_buf = get_sound_buffer_address();

    /* Copy from Mac sound buffer */
    for (int i = 0; i < MAC_AUDIO_BUFFER_SIZE; i++) {
        /* Read high byte (offset 0) - samples are here */
        uint8_t sample = snd_buf[i * 2];
        
        /* Apply volume scaling around center (0x80) */
        if (vol == 0) {
            audio_frame_buffer[i] = 0x80;  /* Silence */
        } else if (vol < 7) {
            int16_t centered = (int16_t)sample - 0x80;
            centered = (centered * vol) / 7;
            audio_frame_buffer[i] = (uint8_t)(centered + 0x80);
        } else {
            audio_frame_buffer[i] = sample;  /* Full volume */
        }
    }
}

void audio_via_port_a_write(uint8_t data)
{
    g_audio.state.via_port_a = data;
    g_audio.state.volume = data & VIA_PA_SNDVOL_MASK;
    /* VIA PA bit 3: 0 = alternate buffer, 1 = main buffer */
    g_audio.state.main_buffer = (data & (1 << VIA_PA_SNDPG2)) != 0;
}

void audio_via_port_b_write(uint8_t data)
{
    g_audio.state.via_port_b = data;
    /* VIA PB7: 0 = sound enabled, 1 = disabled (active low) */
    g_audio.state.sound_enabled = (data & (1 << VIA_PB_SNDENB)) == 0;
    /* Note: pwm_reset shares the same bit, but for now we just track it */
    g_audio.state.pwm_reset = (data & (1 << VIA_PB_SNDRES)) == 0;
}

/*==============================================================================
 * Initialization
 *=============================================================================*/

void audio_init(uint8_t *mac_ram, uint32_t ram_size)
{
    (void)mac_ram;
    (void)ram_size;

    memset(&g_audio, 0, sizeof(g_audio));

    g_audio.state.ram_base = mac_ram;
    g_audio.state.ram_size = ram_size;
    g_audio.state.sound_enabled = true;
    g_audio.state.main_buffer = true;
    g_audio.state.volume = 7;
    g_audio.state.sample_index = 0;
    g_audio.current_sample = 0x80;

    audio_init_pwm();

    /* Force VIA Port B to enable sound (bit 7 = 0) */
    audio_via_port_b_write(0x07);  /* Sound enabled, RTC = 0x7 */
}

void audio_enable(bool enable)
{
    if (enable) {
        /* Enable PWM output */
        pwm_set_enabled(g_audio.pwm_slice, true);
    } else {
        pwm_set_enabled(g_audio.pwm_slice, false);
    }
}

const audio_state_t *audio_get_state(void)
{
    return &g_audio.state;
}
