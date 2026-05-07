/* audio_pwm.c - PWM audio on GPIO 15 (no DAC required)
 *
 * Uses RP2350 PWM hardware on GPIO 15 (PWM slice 7, channel B)
 * Samples are copied from Mac RAM at frame rate (~60Hz) into a local buffer,
 * then output via timer interrupt at 22.254kHz sample rate.
 *
 * Note: Unlike audio_scanline.c which outputs from video IRQ, this uses
 * a hardware timer for consistent sample timing independent of video.
 */

#include <stdio.h>
#include <string.h>
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "audio.h"
#include "hw.h"

/*==============================================================================
 * Configuration
 *=============================================================================*/

/* PWM slice and channel for GPIO 15 audio output */
/* GPIO 15 is PWM7 B (slice 7, channel B) */
#define AUDIO_PWM_SLICE     7
#define AUDIO_PWM_CHANNEL   PWM_CHAN_B
#define AUDIO_GPIO          GPIO_AUDIO_PIN

/* Audio sample rate */
#define AUDIO_SAMPLE_RATE   MAC_AUDIO_SAMPLE_RATE  /* 22254 Hz */

/*==============================================================================
 * Data Structures
 *=============================================================================*/

typedef struct {
    /* PWM configuration */
    uint pwm_slice;
    
    /* Audio frame buffer - two frames worth
     * We copy one frame from Mac RAM while the other plays.
     * This decouples the frame-rate Mac sampling from the
     * higher-rate timer output, preventing buffer underruns.
     */
    uint8_t audio_buffer[MAC_AUDIO_SAMPLES_PER_FRAME * 2];
    uint8_t *play_buffer;    /* Frame currently being output by timer */
    uint8_t *fill_buffer;    /* Frame being filled from Mac RAM */
    
    /* Sample index for playback (0-369) */
    volatile uint16_t play_index;
    
    /* Audio state */
    audio_state_t state;
    
    /* Timer for audio updates */
    struct repeating_timer timer;
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
    
    /* Enable PWM output */
    gpio_set_function(AUDIO_GPIO, GPIO_FUNC_PWM);
    
    /* Configure PWM */
    pwm_config cfg = pwm_get_default_config();
    
    /* Set PWM frequency:
     * For 8-bit resolution at 22.254kHz sample rate:
     * PWM frequency = 22254 * 256 = 5.7 MHz
     * At 250MHz: divider = 43.8
     */
    float div = (float)clock_get_hz(clk_sys) / (float)(AUDIO_SAMPLE_RATE * 256);
    pwm_config_set_clkdiv(&cfg, div);
    
    /* Set wrap value for 8-bit resolution (0-255) */
    pwm_config_set_wrap(&cfg, 255);
    
    /* Initialize PWM on the slice (stopped) */
    pwm_init(g_audio.pwm_slice, &cfg, false);
    
    /* Set initial level to 128 (silence/midpoint) */
    pwm_set_chan_level(g_audio.pwm_slice, AUDIO_PWM_CHANNEL, 128);
    
    printf("Audio PWM: slice=%d, gpio=%d, div=%.2f\n", 
           g_audio.pwm_slice, AUDIO_GPIO, div);
}

/*==============================================================================
 * Sound Buffer Management
 *=============================================================================*/

static inline uint8_t *get_sound_buffer_address(void)
{
    uint32_t offset;
    
    if (g_audio.state.main_buffer) {
        offset = g_audio.state.ram_size - MAC_MAIN_SND_BUF_OFFSET * 2;  /* Convert word count to byte offset */
    } else {
        offset = g_audio.state.ram_size - MAC_ALT_SND_BUF_OFFSET * 2;
    }
    
    return g_audio.state.ram_base + offset;
}

/* Copy one frame of samples from Mac RAM to fill buffer */
static void copy_samples_to_buffer(void)
{
    uint8_t *snd_buf = get_sound_buffer_address();
    uint8_t vol = g_audio.state.volume;
    
    for (int i = 0; i < MAC_AUDIO_SAMPLES_PER_FRAME; i++) {
        uint8_t sample;
        
        if (g_audio.state.sound_enabled && vol > 0) {
            /* Read sample from Mac sound buffer
             * Mac stores 8-bit unsigned samples in the upper byte of each 16-bit word
             * Buffer layout: [sample0][pad0][sample1][pad1]...[sample369][pad369]
             * So we read from even byte offsets: 0, 2, 4, ..., 738
             */
            uint8_t raw_sample = snd_buf[i * 2];
            
            /* Apply volume scaling around center (0x80)
             * Mac volume is 0-7. We use (vol+1)>>3 scaling to give
             * smoother attenuation steps. Volume 7 = no attenuation.
             */
            if (vol >= 7) {
                sample = raw_sample;
            } else if (vol == 0) {
                sample = 0x80;  /* Silence */
            } else {
                int16_t centered = (int16_t)raw_sample - 0x80;
                centered = (centered * (vol + 1)) >> 3;
                sample = (uint8_t)(centered + 0x80);
            }
        } else {
            sample = 0x80;  /* Silence */
        }
        
        g_audio.fill_buffer[i] = sample;
    }
}

/*==============================================================================
 * Timer-based Audio Output (22kHz)
 *=============================================================================*/

static bool audio_timer_callback(struct repeating_timer *t)
{
    (void)t;
    
    /* Output current sample from play buffer */
    uint8_t sample = g_audio.play_buffer[g_audio.play_index];
    pwm_set_chan_level(g_audio.pwm_slice, AUDIO_PWM_CHANNEL, sample);
    
    /* Advance sample index */
    g_audio.play_index++;
    if (g_audio.play_index >= MAC_AUDIO_SAMPLES_PER_FRAME) {
        g_audio.play_index = 0;
    }
    
    return true;
}

/*==============================================================================
 * Public API
 *=============================================================================*/

void audio_hblank_callback(void)
{
    /* Not used for PWM audio - we use timer instead */
}

void audio_via_port_a_write(uint8_t data)
{
    g_audio.state.via_port_a = data;
    g_audio.state.volume = data & VIA_PA_SNDVOL_MASK;
    g_audio.state.main_buffer = (data & (1 << VIA_PA_SNDPG2)) != 0;
}

void audio_via_port_b_write(uint8_t data)
{
    g_audio.state.via_port_b = data;
    g_audio.state.sound_enabled = (data & (1 << VIA_PB_SNDENB)) == 0;
}

/* Called at frame rate (~60Hz) to refill buffers */
void audio_frame_update(void)
{
    /* Copy new samples to fill buffer */
    copy_samples_to_buffer();
    
    /* Swap buffers for next frame */
    uint8_t *temp = g_audio.play_buffer;
    g_audio.play_buffer = g_audio.fill_buffer;
    g_audio.fill_buffer = temp;
}

void audio_init(uint8_t *mac_ram, uint32_t ram_size)
{
    memset(&g_audio, 0, sizeof(g_audio));
    
    g_audio.state.ram_base = mac_ram;
    g_audio.state.ram_size = ram_size;
    g_audio.state.sound_enabled = false;
    g_audio.state.main_buffer = true;
    g_audio.state.volume = 7;
    g_audio.state.sample_index = 0;
    g_audio.state.sound_buffer = get_sound_buffer_address();
    
    /* Set up double buffer */
    g_audio.play_buffer = g_audio.audio_buffer;
    g_audio.fill_buffer = &g_audio.audio_buffer[MAC_AUDIO_SAMPLES_PER_FRAME];
    g_audio.play_index = 0;
    
    /* Initialize buffers to silence */
    memset(g_audio.audio_buffer, 0x80, sizeof(g_audio.audio_buffer));
    
    audio_init_pwm();
    
    printf("Audio PWM init: slice=%d (GPIO %d)\n",
           g_audio.pwm_slice, AUDIO_GPIO);
}

void audio_enable(bool enable)
{
    if (enable) {
        /* Enable PWM first */
        pwm_set_enabled(g_audio.pwm_slice, true);
        
        /* Set up repeating timer for audio output at ~22.254kHz
         * Ideal period: 1/22254 Hz = 44.93us
         * We use 45us (closest integer) which gives 22222 Hz - close enough
         * for PWM audio on a simple speaker circuit.
         */
        add_repeating_timer_us(-45, audio_timer_callback, NULL, &g_audio.timer);
        
        printf("Audio: Enabled on GPIO %d (PWM slice %d)\n", 
               AUDIO_GPIO, g_audio.pwm_slice);
    } else {
        cancel_repeating_timer(&g_audio.timer);
        pwm_set_enabled(g_audio.pwm_slice, false);
    }
}

const audio_state_t *audio_get_state(void)
{
    return &g_audio.state;
}
