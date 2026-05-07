/* audio_i2s.c - PIO+DMA I2S with Snow-style prebuffering
 *
 * Uses intermediate software buffer to prevent underruns.
 * Prebuffering mode: waits for buffer to fill before playing.
 */

#include <stdio.h>
#include <string.h>
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "audio.h"
#include "hw.h"
#include "audio_i2s.pio.h"

/*==============================================================================
 * Configuration
 *=============================================================================*/

#define AUDIO_PIO           pio1
#define AUDIO_SM            0

#define I2S_DATA_PIN        GPIO_I2S_DIN_PIN
#define I2S_BCLK_PIN        GPIO_I2S_BCLK_PIN
#define I2S_LRCLK_PIN       GPIO_I2S_LRCLK_PIN

#define SAMPLES_PER_BUFFER  MAC_AUDIO_BUFFER_SIZE   /* 370 */

/* Prebuffering: aim to keep 2-3 frames ahead (740-1110 samples) */
#define PREBUFFER_MIN_SAMPLES   (SAMPLES_PER_BUFFER * 2)
#define PREBUFFER_MAX_SAMPLES   (SAMPLES_PER_BUFFER * 4)
#define SOFTWARE_BUFFER_SIZE    (SAMPLES_PER_BUFFER * 6)  /* 2220 samples */

/*==============================================================================
 * State
 *=============================================================================*/

static audio_state_t audio_st;
static bool audio_running = false;

static uint dma_chan_a;
static uint dma_chan_b;

/* Hardware DMA buffers ( ping-pong ) */
static uint32_t dma_buf_a[SAMPLES_PER_BUFFER];
static uint32_t dma_buf_b[SAMPLES_PER_BUFFER];

/* Software ring buffer - holds prebuffered audio */
static uint32_t sw_buffer[SOFTWARE_BUFFER_SIZE];
static volatile uint32_t sw_write_idx = 0;
static volatile uint32_t sw_read_idx = 0;
static volatile uint32_t sw_count = 0;

static volatile bool prebuffering = true;
static volatile bool buf_a_ready = false;
static volatile bool buf_b_ready = false;

/*==============================================================================
 * Helpers
 *=============================================================================*/

static inline uint8_t *get_sound_buffer(void)
{
    if (audio_st.main_buffer)
        return audio_st.ram_base + (audio_st.ram_size - 0x300);
    else
        return audio_st.ram_base + (audio_st.ram_size - 0x5F00);
}

static inline uint32_t mac_sample_to_i2s(uint8_t sample, uint8_t vol)
{
    int16_t centered = (int16_t)sample - 128;
    if (vol == 0)
        centered = 0;
    else if (vol < 7)
        centered = (centered * (vol + 1)) >> 3;
    // Convert to signed 16-bit with reduced gain to prevent DAC clipping
    // Multiply by 254 then >>1 is equivalent to *127, giving ~50% headroom
    int16_t s16 = (centered * 254) >> 1;
    return ((uint32_t)(uint16_t)s16 << 16) | (uint16_t)s16;
}

/* Fill software buffer from Mac RAM */
static void refill_software_buffer(void)
{
    uint8_t vol = audio_st.volume;
    uint8_t *src = get_sound_buffer();

    if (!audio_st.sound_enabled || vol == 0) {
        // Fill with silence
        while (sw_count < PREBUFFER_MAX_SAMPLES) {
            sw_buffer[sw_write_idx] = 0;
            sw_write_idx = (sw_write_idx + 1) % SOFTWARE_BUFFER_SIZE;
            sw_count++;
        }
        return;
    }

    // Fill up to max
    while (sw_count < PREBUFFER_MAX_SAMPLES) {
        // Get sample from Mac buffer (circular)
        uint16_t idx = audio_st.sample_index;
        uint8_t sample = src[idx * 2];
        sw_buffer[sw_write_idx] = mac_sample_to_i2s(sample, vol);
        
        sw_write_idx = (sw_write_idx + 1) % SOFTWARE_BUFFER_SIZE;
        sw_count++;
        
        audio_st.sample_index++;
        if (audio_st.sample_index > 369)
            audio_st.sample_index = 0;
    }
}

/* Copy samples from software buffer to DMA buffer */
static void copy_to_dma_buffer(uint32_t *dma_buf)
{
    for (int i = 0; i < SAMPLES_PER_BUFFER; i++) {
        if (sw_count > 0) {
            dma_buf[i] = sw_buffer[sw_read_idx];
            sw_read_idx = (sw_read_idx + 1) % SOFTWARE_BUFFER_SIZE;
            sw_count--;
        } else {
            // Underrun - play silence
            dma_buf[i] = 0;
        }
    }
}

/* Get software buffer fill level */
static inline uint32_t get_sw_buffer_count(void)
{
    return sw_count;
}

/*==============================================================================
 * DMA configuration helpers
 *=============================================================================*/

static void configure_channel_no_chain(uint chan, uint32_t *buf)
{
    dma_channel_config cfg = dma_channel_get_default_config(chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(AUDIO_PIO, AUDIO_SM, true));
    channel_config_set_chain_to(&cfg, chan);

    dma_channel_configure(chan, &cfg,
                          &AUDIO_PIO->txf[AUDIO_SM], buf,
                          SAMPLES_PER_BUFFER, false);
}

static inline void patch_chain_to(uint chan, uint target)
{
    uint32_t ctrl = dma_hw->ch[chan].ctrl_trig;
    ctrl &= ~DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS;
    ctrl |= (target << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB);
    dma_hw->ch[chan].al1_ctrl = ctrl;
}

/*==============================================================================
 * Public API: VIA integration
 *=============================================================================*/

void audio_via_port_a_write(uint8_t data)
{
    audio_st.via_port_a = data;
    audio_st.volume = data & VIA_PA_SNDVOL_MASK;
    audio_st.main_buffer = (data & (1 << VIA_PA_SNDPG2)) != 0;
}

void audio_via_port_b_write(uint8_t data)
{
    audio_st.via_port_b = data;
    audio_st.sound_enabled = (data & (1 << VIA_PB_SNDENB)) == 0;
}

/*==============================================================================
 * Public API: frame update & hblank
 *=============================================================================*/

void __not_in_flash_func(audio_hblank_callback)(void) { }

void audio_frame_update(void)
{
    if (!audio_running)
        return;

    /* Always refill software buffer first */
    refill_software_buffer();

    /* Check if we need to prebuffer */
    if (prebuffering) {
        if (get_sw_buffer_count() >= PREBUFFER_MIN_SAMPLES) {
            // Done prebuffering, can start/resume playback
            prebuffering = false;
        } else {
            // Not enough samples yet, keep filling
            return;
        }
    }

    /* If buffer gets too low, re-enter prebuffering */
    if (get_sw_buffer_count() < SAMPLES_PER_BUFFER) {
        prebuffering = true;
        return;
    }

    /* Now handle DMA buffer refills */
    bool a_busy = dma_channel_is_busy(dma_chan_a);
    bool b_busy = dma_channel_is_busy(dma_chan_b);

    if (!a_busy && !buf_a_ready) {
        copy_to_dma_buffer(dma_buf_a);
        configure_channel_no_chain(dma_chan_a, dma_buf_a);
        buf_a_ready = true;
        if (b_busy) {
            patch_chain_to(dma_chan_b, dma_chan_a);
        }
    }

    if (!b_busy && !buf_b_ready) {
        copy_to_dma_buffer(dma_buf_b);
        configure_channel_no_chain(dma_chan_b, dma_buf_b);
        buf_b_ready = true;
        if (a_busy) {
            patch_chain_to(dma_chan_a, dma_chan_b);
        }
    }

    /* Both stopped - restart */
    if (!a_busy && !b_busy) {
        dma_channel_abort(dma_chan_a);
        dma_channel_abort(dma_chan_b);

        copy_to_dma_buffer(dma_buf_a);
        copy_to_dma_buffer(dma_buf_b);

        configure_channel_no_chain(dma_chan_a, dma_buf_a);
        configure_channel_no_chain(dma_chan_b, dma_buf_b);

        buf_a_ready = false;
        buf_b_ready = true;

        patch_chain_to(dma_chan_a, dma_chan_b);
        dma_channel_start(dma_chan_a);
    }

    /* Mark buffer as playing once DMA starts */
    if (a_busy && buf_a_ready)
        buf_a_ready = false;
    if (b_busy && buf_b_ready)
        buf_b_ready = false;
}

/*==============================================================================
 * Initialization
 *=============================================================================*/

void audio_init(uint8_t *mac_ram, uint32_t ram_size)
{
    memset(&audio_st, 0, sizeof(audio_st));
    memset(sw_buffer, 0, sizeof(sw_buffer));
    sw_write_idx = 0;
    sw_read_idx = 0;
    sw_count = 0;
    prebuffering = true;

    audio_st.ram_base = mac_ram;
    audio_st.ram_size = ram_size;
    audio_st.sound_enabled = true;
    audio_st.main_buffer = true;
    audio_st.volume = 7;

    printf("Audio (PIO+DMA I2S, prebuffered):\n");

    /* PIO setup */
    uint pio_off = pio_add_program(AUDIO_PIO, &audio_i2s_program);

    uint32_t sys_clk = clock_get_hz(clk_sys);
    float pio_clkdiv = (float)sys_clk / (float)(MAC_AUDIO_SAMPLE_RATE * 64);
    printf("  sys_clk=%lu, clkdiv=%.2f\n", (unsigned long)sys_clk, pio_clkdiv);

    pio_gpio_init(AUDIO_PIO, I2S_DATA_PIN);
    pio_gpio_init(AUDIO_PIO, I2S_BCLK_PIN);
    pio_gpio_init(AUDIO_PIO, I2S_LRCLK_PIN);

    audio_i2s_program_init(AUDIO_PIO, AUDIO_SM, pio_off,
                           I2S_DATA_PIN, I2S_BCLK_PIN);
    pio_sm_set_clkdiv(AUDIO_PIO, AUDIO_SM, pio_clkdiv);

    /* DMA setup */
    dma_chan_a = dma_claim_unused_channel(true);
    dma_chan_b = dma_claim_unused_channel(true);
    printf("  DMA: A=%u, B=%u\n", dma_chan_a, dma_chan_b);

    memset(dma_buf_a, 0, sizeof(dma_buf_a));
    memset(dma_buf_b, 0, sizeof(dma_buf_b));

    printf("  Prebuffer: %d-%d samples\n", PREBUFFER_MIN_SAMPLES, PREBUFFER_MAX_SAMPLES);
    printf("  Software buffer: %d samples\n", SOFTWARE_BUFFER_SIZE);
}

void audio_enable(bool enable)
{
    if (enable) {
        // Pre-fill software buffer first
        refill_software_buffer();

        // Fill DMA buffers
        copy_to_dma_buffer(dma_buf_a);
        copy_to_dma_buffer(dma_buf_b);

        configure_channel_no_chain(dma_chan_a, dma_buf_a);
        configure_channel_no_chain(dma_chan_b, dma_buf_b);

        buf_a_ready = false;
        buf_b_ready = true;
        prebuffering = (get_sw_buffer_count() < PREBUFFER_MIN_SAMPLES);

        /* Enable PIO, let A chain to B, start A */
        pio_sm_set_enabled(AUDIO_PIO, AUDIO_SM, true);
        patch_chain_to(dma_chan_a, dma_chan_b);
        dma_channel_start(dma_chan_a);
        audio_running = true;

        printf("  Audio ENABLED (prebuffering: %s)\n", prebuffering ? "yes" : "no");
    } else {
        audio_running = false;
        dma_channel_abort(dma_chan_a);
        dma_channel_abort(dma_chan_b);
        pio_sm_set_enabled(AUDIO_PIO, AUDIO_SM, false);
        pio_sm_clear_fifos(AUDIO_PIO, AUDIO_SM);
        prebuffering = true;
    }
}

const audio_state_t *audio_get_state(void)
{
    return &audio_st;
}
