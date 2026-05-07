/* audio.c - Picomac Audio Driver Implementation
 *
 * RP2350 audio output using PIO and DMA for PWM generation.
 * Implements the Macintosh 128K/Plus sound hardware.
 *
 * The audio system:
 * - PIO generates PWM output at 22.254 kHz
 * - DMA feeds samples from Mac RAM buffer to PIO FIFO
 * - Double-buffered DMA for continuous playback
 * - VIA integration for volume and buffer selection
 *
 * Copyright 2024
 */

#include <stdio.h>
#include <string.h>
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "audio.h"
#include "hw.h"
#include "pio_audio.pio.h"

/*==============================================================================
 * Configuration
 *=============================================================================*/

/* GPIO pin for audio output - using a pin not used by video */
#ifndef GPIO_AUDIO_PIN
#define GPIO_AUDIO_PIN      20
#endif

/* PIO instance and state machine */
#define AUDIO_PIO           pio1
#define AUDIO_SM            0
#define AUDIO_DMA_CHANNEL   2   /* Use channel 2 (0-1 used by video) */

/* Audio sample rate */
#define AUDIO_SAMPLE_RATE   MAC_AUDIO_SAMPLE_RATE  /* 22254 Hz */

/*==============================================================================
 * Data Structures
 *=============================================================================*/

typedef struct {
    /* PIO/DMA resources */
    uint pio_sm;
    uint dma_chan;
    uint pio_offset;
    
    /* DMA buffer - holds samples for one frame */
    uint8_t dma_buffer[MAC_AUDIO_BUFFER_SIZE * 2];  /* Double buffer */
    volatile uint8_t *active_buffer;
    volatile uint8_t *back_buffer;
    
    /* Audio state */
    audio_state_t state;
    
    /* Volume attenuation table (3-bit volume, 0-7) */
    /* Volume 7 = full, 0 = silence */
    uint8_t vol_table[8];
} audio_driver_t;

/*==============================================================================
 * Global State
 *=============================================================================*/

static audio_driver_t g_audio;

/*==============================================================================
 * PIO/DMA Initialization
 *=============================================================================*/

/* Calculate system clock divider for PIO
 * 
 * The PIO needs to run at: sample_rate × cycles_per_sample
 * For 8-bit PWM at 22254 Hz with 10 cycles per sample: 22254 × 10 = 222.54 kHz
 * At 250 MHz system clock: divider = 250000000 / 222540 = ~1123
 */
static float calculate_pio_clkdiv(void)
{
    uint32_t sys_clk = clock_get_hz(clk_sys);
    printf("Audio: System clock = %lu Hz\n", (unsigned long)sys_clk);
    
    /* Target: our PIO program uses about 10 cycles per sample */
    float target_freq = (float)AUDIO_SAMPLE_RATE * 10.0f;
    float divider = (float)sys_clk / target_freq;
    printf("Audio: PIO clock divider = %f\n", divider);
    return divider;
}

/* Initialize PIO state machine for PWM audio */
static void audio_init_pio(void)
{
    /* Load the PIO program */
    g_audio.pio_offset = pio_add_program(AUDIO_PIO, &pio_pwm_audio_program);
    
    /* Calculate clock divider */
    float clkdiv = calculate_pio_clkdiv();
    
    /* Initialize the PIO program */
    pio_pwm_audio_program_init(AUDIO_PIO, AUDIO_SM, g_audio.pio_offset, 
                               GPIO_AUDIO_PIN, clkdiv);
    
    g_audio.pio_sm = AUDIO_SM;
}

/* DMA IRQ handler - triggered when DMA transfer completes */
static void __not_in_flash_func(audio_dma_irq_handler)(void)
{
    if (dma_channel_get_irq0_status(g_audio.dma_chan)) {
        dma_channel_acknowledge_irq0(g_audio.dma_chan);
        
        /* Swap buffers */
        volatile uint8_t *temp = g_audio.active_buffer;
        g_audio.active_buffer = g_audio.back_buffer;
        g_audio.back_buffer = temp;
        
        /* Refill the back buffer with next frame's samples */
        /* This happens asynchronously while DMA plays current buffer */
    }
}

/* Initialize DMA channel for audio samples */
static void audio_init_dma(void)
{
    g_audio.dma_chan = AUDIO_DMA_CHANNEL;
    
    /* Claim the DMA channel */
    dma_channel_claim(g_audio.dma_chan);
    
    /* Configure DMA channel */
    dma_channel_config cfg = dma_channel_get_default_config(g_audio.dma_chan);
    
    /* Transfer 8-bit samples to PIO FIFO */
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    
    /* Increment read address, don't increment write */
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    
    /* Use DREQ pacing - one transfer per PIO FIFO request */
    channel_config_set_dreq(&cfg, pio_get_dreq(AUDIO_PIO, AUDIO_SM, true));
    
    /* Enable IRQ on completion */
    dma_channel_set_irq0_enabled(g_audio.dma_chan, true);
    
    /* Set up IRQ handler */
    irq_set_exclusive_handler(DMA_IRQ_1, audio_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
    
    /* Initialize buffers */
    memset((void *)g_audio.dma_buffer, 0x80, sizeof(g_audio.dma_buffer));
    g_audio.active_buffer = g_audio.dma_buffer;
    g_audio.back_buffer = &g_audio.dma_buffer[MAC_AUDIO_BUFFER_SIZE];
}

/*==============================================================================
 * Sound Buffer Management
 *=============================================================================*/

/* Calculate current sound buffer address based on VIA state */
static inline uint8_t *get_sound_buffer_address(void)
{
    uint32_t offset;
    
    if (g_audio.state.main_buffer) {
        offset = g_audio.state.ram_size - MAC_MAIN_SND_BUF_OFFSET;
    } else {
        offset = g_audio.state.ram_size - MAC_ALT_SND_BUF_OFFSET;
    }
    
    return g_audio.state.ram_base + offset;
}

/* Apply volume attenuation to a sample
 * 
 * The Mac uses a 3-bit volume (0-7) where:
 * - 7 = full volume (no attenuation)
 * - 0 = silence
 * 
 * Attenuation is applied by bit-shifting (integer divide)
 */
static inline uint8_t apply_volume(uint8_t sample, uint8_t vol)
{
    /* Center sample around 128 (Mac audio is unsigned 8-bit) */
    int16_t centered = (int16_t)sample - 128;
    
    /* Apply volume (vol 7 = full, vol 0 = silence) */
    /* Use integer multiply for better performance on M33 */
    /* centered * (vol + 1) / 8 gives us 8 levels */
    centered = (centered * (vol + 1)) >> 3;
    
    /* Add back offset and clamp */
    int16_t result = centered + 128;
    if (result < 0) result = 0;
    if (result > 255) result = 255;
    
    return (uint8_t)result;
}

/* Copy and process samples from Mac RAM to DMA buffer
 * 
 * Called to refill the back buffer while DMA plays the front buffer.
 * Applies volume attenuation and handles /SNDRES mute.
 */
static void refill_dma_buffer(uint8_t *dest)
{
    uint8_t *src = get_sound_buffer_address();
    uint8_t vol = g_audio.state.volume;
    
    /* If PWM reset is active (/SNDRES low), output silence */
    if (g_audio.state.pwm_reset || !g_audio.state.sound_enabled) {
        memset(dest, 0x80, MAC_AUDIO_BUFFER_SIZE);
        return;
    }
    
    /* Copy samples with volume applied */
    /* Use DSP-optimized loop on Cortex-M33 */
    uint32_t i;
    for (i = 0; i < MAC_AUDIO_BUFFER_SIZE; i++) {
        /* Mac sound buffer stores samples in high byte of each 16-bit word */
        /* 68000 is big-endian: high byte is at even address (i * 2) */
        uint8_t sample = src[i * 2];  /* FIXED: was i * 2 + 1 */
        dest[i] = apply_volume(sample, vol);
    }
}

/*==============================================================================
 * HBlank / Scanline Interface
 *=============================================================================*/

/* Called once per horizontal blanking interval (scanline)
 * 
 * This is the key timing interface. On a real Mac, the sound hardware
 * reads one sample from the buffer during each HBLANK period.
 * 
 * We accumulate samples and use DMA pacing rather than strict HBLANK
 * timing, but we track the sample index to maintain sync.
 */
void audio_hblank_callback(void)
{
    /* Increment sample index */
    g_audio.state.sample_index++;
    
    /* Wrap at frame boundary (370 samples per frame) */
    if (g_audio.state.sample_index >= MAC_AUDIO_SAMPLES_PER_FRAME) {
        g_audio.state.sample_index = 0;
        
        /* Frame boundary - update sound buffer pointer */
        g_audio.state.sound_buffer = get_sound_buffer_address();
    }
}

/*==============================================================================
 * VIA Integration
 *=============================================================================*/

/* Called when VIA Port A is written
 * 
 * Port A bits:
 *   [2:0] - Volume level (0-7)
 *   [3]   - Sound buffer select (0 = alternate, 1 = main)
 *   [4]   - Screen buffer select
 *   [5]   - Head select (floppy)
 *   [6]   - SCSI control
 *   [7]   - SCC control
 */
void audio_via_port_a_write(uint8_t data)
{
    g_audio.state.via_port_a = data;
    
    /* Extract volume (bits 0-2) */
    g_audio.state.volume = data & VIA_PA_SNDVOL_MASK;
    
    /* Extract buffer select (bit 3) - inverted: 1 = main, 0 = alt */
    g_audio.state.main_buffer = (data & (1 << VIA_PA_SNDPG2)) != 0;
    
    /* Update buffer pointer */
    g_audio.state.sound_buffer = get_sound_buffer_address();
}

/* Called when VIA Port B is written
 * 
 * Port B bits:
 *   [7]   - /SNDENB (0 = sound enabled, 1 = disabled)
 *   [7]   - /SNDRES (when low, resets PWM counters)
 *   [6]   - H4 (horizontal blank signal)
 *   [0-5] - RTC and keyboard data
 */
void audio_via_port_b_write(uint8_t data)
{
    uint8_t old_data = g_audio.state.via_port_b;
    g_audio.state.via_port_b = data;

    /* Sound enable bit - inverted logic (0 = enabled) */
    g_audio.state.sound_enabled = (data & (1 << VIA_PB_SNDENB)) == 0;

    /* PWM reset - when PB7 is low, counters are held at 0 (silence) */
    g_audio.state.pwm_reset = (data & (1 << VIA_PB_SNDRES)) == 0;

    /* /SNDRES enforcement: force immediate silence when PB7 goes low */
    /* This is required for games like Lode Runner that use swMode */
    if (g_audio.state.pwm_reset) {
        /* Force PIO to output silence (center value 0x80) */
        pio_sm_set_enabled(AUDIO_PIO, g_audio.pio_sm, false);
        /* Clear FIFOs to prevent old samples from playing */
        pio_sm_clear_fifos(AUDIO_PIO, g_audio.pio_sm);
    } else if (g_audio.state.sound_enabled && !pio_sm_is_enabled(AUDIO_PIO, g_audio.pio_sm)) {
        /* Re-enable if sound should be playing */
        pio_sm_set_enabled(AUDIO_PIO, g_audio.pio_sm, true);
    }
}

/*==============================================================================
 * Frame Update (60.15 Hz)
 *=============================================================================*/

/* Called at frame rate (~60.15 Hz) to update DMA buffers
 * 
 * This is the main entry point from the emulation loop.
 * It refills the audio buffer for the next frame.
 */
void audio_frame_update(void)
{
    /* Refill the back buffer */
    refill_dma_buffer((uint8_t *)g_audio.back_buffer);
    
    /* Check if DMA needs to be restarted or reconfigured */
    if (!dma_channel_is_busy(g_audio.dma_chan)) {
        /* DMA completed, swap buffers and restart */
        volatile uint8_t *temp = g_audio.active_buffer;
        g_audio.active_buffer = g_audio.back_buffer;
        g_audio.back_buffer = temp;
        
        /* Start DMA transfer */
        dma_channel_config cfg = dma_channel_get_default_config(g_audio.dma_chan);
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
        channel_config_set_read_increment(&cfg, true);
        channel_config_set_write_increment(&cfg, false);
        channel_config_set_dreq(&cfg, pio_get_dreq(AUDIO_PIO, AUDIO_SM, true));
        
        dma_channel_configure(
            g_audio.dma_chan,
            &cfg,
            &AUDIO_PIO->txf[AUDIO_SM],      /* Destination: PIO FIFO */
            g_audio.active_buffer,           /* Source: audio buffer */
            MAC_AUDIO_BUFFER_SIZE,           /* Count: one frame of samples */
            true                             /* Start immediately */
        );
    }
}

/*==============================================================================
 * Initialization
 *=============================================================================*/

void audio_init(uint8_t *mac_ram, uint32_t ram_size)
{
    memset(&g_audio, 0, sizeof(g_audio));
    
    /* Store RAM info */
    g_audio.state.ram_base = mac_ram;
    g_audio.state.ram_size = ram_size;
    
    /* Initialize default state */
    g_audio.state.sound_enabled = false;
    g_audio.state.main_buffer = true;  /* Start with main buffer */
    g_audio.state.volume = 7;          /* Full volume */
    g_audio.state.sample_index = 0;
    
    /* Calculate initial buffer address */
    g_audio.state.sound_buffer = get_sound_buffer_address();
    
    /* Initialize volume table */
    for (int i = 0; i < 8; i++) {
        g_audio.vol_table[i] = (uint8_t)(((i + 1) * 255) / 8);
    }
    
    /* Initialize hardware */
    audio_init_pio();
    audio_init_dma();
    
    printf("Audio init: PIO SM=%d, DMA chan=%d, sample rate=%d Hz\n",
           g_audio.pio_sm, g_audio.dma_chan, AUDIO_SAMPLE_RATE);
    printf("  Sound buffer main: RAM+0x%lx, alt: RAM+0x%lx\n",
           (unsigned long)(ram_size - MAC_MAIN_SND_BUF_OFFSET),
           (unsigned long)(ram_size - MAC_ALT_SND_BUF_OFFSET));
}

void audio_enable(bool enable)
{
    if (enable) {
        /* Fill buffer with test tone (1kHz square wave approx) for debugging */
        /* This allows scope verification without VIA integration */
        /* At 22.254kHz sample rate, 22 samples = ~1kHz */
        for (int i = 0; i < MAC_AUDIO_BUFFER_SIZE; i++) {
            /* Simple square wave: alternate between high and low every 11 samples */
            g_audio.dma_buffer[i] = ((i / 11) & 1) ? 0xE0 : 0x20;
        }
        
        /* Pre-load some samples into PIO FIFO before enabling SM */
        /* This prevents the SM from stalling immediately */
        for (int i = 0; i < 4 && i < MAC_AUDIO_BUFFER_SIZE; i++) {
            pio_sm_put(AUDIO_PIO, g_audio.pio_sm, g_audio.dma_buffer[i]);
        }
        
        /* Start DMA - this will continue feeding the FIFO */
        dma_channel_config cfg = dma_channel_get_default_config(g_audio.dma_chan);
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
        channel_config_set_read_increment(&cfg, true);
        channel_config_set_write_increment(&cfg, false);
        channel_config_set_dreq(&cfg, pio_get_dreq(AUDIO_PIO, AUDIO_SM, true));
        
        dma_channel_configure(
            g_audio.dma_chan,
            &cfg,
            &AUDIO_PIO->txf[AUDIO_SM],
            g_audio.active_buffer,
            MAC_AUDIO_BUFFER_SIZE,
            true  /* Start immediately */
        );
        
        /* Now enable the state machine */
        pio_sm_set_enabled(AUDIO_PIO, g_audio.pio_sm, true);
        
        printf("Audio: Started with test tone on GPIO %d\n", GPIO_AUDIO_PIN);
        printf("  Sample rate: %d Hz, PIO clock divider: %f\n", 
               AUDIO_SAMPLE_RATE, calculate_pio_clkdiv());
    } else {
        pio_sm_set_enabled(AUDIO_PIO, g_audio.pio_sm, false);
        dma_channel_abort(g_audio.dma_chan);
        pio_sm_clear_fifos(AUDIO_PIO, g_audio.pio_sm);
    }
}

const audio_state_t *audio_get_state(void)
{
    return &g_audio.state;
}
