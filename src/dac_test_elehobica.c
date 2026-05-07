/* dac_test_elehobica.c - Test the elehobica-based PIO I2S implementation
 * 
 * This tests the exact PIO program and initialization sequence from the
 * working elehobica library, adapted for pins 8/6/7.
 */

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

#include "audio_i2s.pio.h"

// Pin definitions (matching hw.h)
#define DIN_PIN     8
#define BCLK_PIN    6
#define LRCK_PIN    7

// Use PIO1 for audio (to avoid conflict with video on PIO0)
#define AUDIO_PIO   pio1
#define AUDIO_SM    0

// Use DMA channels 2 and 3
#define DMA_CHAN0   2
#define DMA_CHAN1   3

// Audio settings
#define SAMPLE_RATE     44100
#define BITS_PER_SAMPLE 16
#define SAMPLES_PER_BUFFER  256

// Two buffers for ping-pong DMA
static int16_t buffer0[SAMPLES_PER_BUFFER * 2];  // Stereo = 2 samples per frame
static int16_t buffer1[SAMPLES_PER_BUFFER * 2];

static uint offset;
static volatile uint8_t active_buffer = 0;

// Generate sine wave table
static int16_t sine_table[256];
static uint32_t phase = 0;
static const uint32_t phase_inc = 0x01000000; // ~440Hz at 44.1kHz

static void generate_sine(int16_t *buf, uint len)
{
    for (uint i = 0; i < len; i++) {
        int16_t sample = sine_table[(phase >> 24) & 0xFF];
        buf[i*2] = sample;      // Left
        buf[i*2+1] = sample;    // Right
        phase += phase_inc;
    }
}

static void __isr dma_irq_handler(void)
{
    if (dma_irqn_get_channel_status(0, DMA_CHAN0)) {
        dma_irqn_acknowledge_channel(0, DMA_CHAN0);
        active_buffer = 1;
        generate_sine(buffer0, SAMPLES_PER_BUFFER);
    }
    
    if (dma_irqn_get_channel_status(0, DMA_CHAN1)) {
        dma_irqn_acknowledge_channel(0, DMA_CHAN1);
        active_buffer = 0;
        generate_sine(buffer1, SAMPLES_PER_BUFFER);
    }
}

int main(void)
{
    stdio_init_all();
    
    printf("\n\n=== Elehobica PIO I2S Test ===\n");
    printf("Pins: DIN=%d, BCLK=%d, LRCK=%d\n", DIN_PIN, BCLK_PIN, LRCK_PIN);
    printf("PIO=%p, SM=%d, DMA=%d/%d\n", AUDIO_PIO, AUDIO_SM, DMA_CHAN0, DMA_CHAN1);
    
    // Generate sine table
    for (int i = 0; i < 256; i++) {
        sine_table[i] = (int16_t)(30000.0 * sin(2.0 * M_PI * i / 256.0));
    }
    
    // Generate initial buffer content
    generate_sine(buffer0, SAMPLES_PER_BUFFER);
    generate_sine(buffer1, SAMPLES_PER_BUFFER);
    
    // Initialize pins
    gpio_init(DIN_PIN);
    gpio_init(BCLK_PIN);
    gpio_init(LRCK_PIN);
    
    // Claim PIO state machine
    pio_sm_claim(AUDIO_PIO, AUDIO_SM);
    
    // Load and initialize PIO program
    offset = pio_add_program(AUDIO_PIO, &audio_i2s_program);
    printf("PIO program loaded at offset %d\n", offset);
    
    audio_i2s_program_init(AUDIO_PIO, AUDIO_SM, offset, DIN_PIN, BCLK_PIN, BITS_PER_SAMPLE);
    printf("PIO program initialized\n");
    
    // Set clock frequency for 44.1kHz
    // Calculate and set clock divider manually
    uint32_t system_clock = clock_get_hz(clk_sys);
    float clkdiv = (float)system_clock / (float)(SAMPLE_RATE * (BITS_PER_SAMPLE + 1) * 2 * 2);
    pio_sm_set_clkdiv(AUDIO_PIO, AUDIO_SM, clkdiv);
    printf("Clock divider: %.4f for %.1f Hz (sys=%u Hz)\n", clkdiv, 
           (float)(SAMPLE_RATE * (BITS_PER_SAMPLE + 1) * 2 * 2), (unsigned)system_clock);
    printf("Clock configured for %d Hz sample rate\n", SAMPLE_RATE);
    
    // Claim DMA channels
    dma_channel_claim(DMA_CHAN0);
    dma_channel_claim(DMA_CHAN1);
    
    // Configure DMA
    dma_channel_config cfg0 = dma_channel_get_default_config(DMA_CHAN0);
    channel_config_set_transfer_data_size(&cfg0, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg0, true);
    channel_config_set_write_increment(&cfg0, false);
    channel_config_set_dreq(&cfg0, pio_get_dreq(AUDIO_PIO, AUDIO_SM, true));
    channel_config_set_chain_to(&cfg0, DMA_CHAN1);
    
    dma_channel_config cfg1 = dma_channel_get_default_config(DMA_CHAN1);
    channel_config_set_transfer_data_size(&cfg1, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg1, true);
    channel_config_set_write_increment(&cfg1, false);
    channel_config_set_dreq(&cfg1, pio_get_dreq(AUDIO_PIO, AUDIO_SM, true));
    channel_config_set_chain_to(&cfg1, DMA_CHAN0);
    
    // Setup DMA
    dma_channel_configure(DMA_CHAN0, &cfg0,
        &AUDIO_PIO->txf[AUDIO_SM],
        buffer0,
        SAMPLES_PER_BUFFER,
        false);
    
    dma_channel_configure(DMA_CHAN1, &cfg1,
        &AUDIO_PIO->txf[AUDIO_SM],
        buffer1,
        SAMPLES_PER_BUFFER,
        false);
    
    // Install and enable IRQ
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_irqn_set_channel_enabled(0, DMA_CHAN0, true);
    dma_irqn_set_channel_enabled(0, DMA_CHAN1, true);
    
    printf("Starting I2S audio...\n");
    dma_channel_start(DMA_CHAN0);
    
    // Main loop - monitor and print status
    uint32_t last_time = 0;
    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_time > 1000) {
            printf("Audio running... active buffer: %d, PIO TX FIFO: %u\n",
                   active_buffer, pio_sm_get_tx_fifo_level(AUDIO_PIO, AUDIO_SM));
            last_time = now;
        }
        tight_loop_contents();
    }
}
