/* dac_test_exact.c - EXACT copy of elehobica sine_wave
 *
 * Key insight: Uses CHAINED DMA - only start channel 0
 */

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"

#include "audio_i2s.pio.h"

#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

#define DIN_PIN     8
#define BCLK_PIN    6
#define LRCK_PIN    7

#define AUDIO_PIO   pio1
#define AUDIO_SM    0
#define DMA_CHAN0   2
#define DMA_CHAN1   3

#define SAMPLE_RATE     44100
#define BITS_PER_SAMPLE 32
#define SAMPLES_PER_BUFFER 256

static int32_t buffer0[SAMPLES_PER_BUFFER * 2];
static int32_t buffer1[SAMPLES_PER_BUFFER * 2];

static volatile uint32_t irq_count = 0;

static int32_t sine_table[256];
static uint32_t phase = 0;

#define DAC_ZERO 1

static dma_channel_config dma_cfg0;
static dma_channel_config dma_cfg1;

static void generate_sine(int32_t *buf, uint len)
{
    for (uint i = 0; i < len; i++) {
        int32_t sample = sine_table[(phase >> 24) & 0xFF];
        buf[i*2] = sample;
        buf[i*2+1] = sample;
        phase += 0x01000000;
    }
}

// Refill buffer and reconfigure DMA
static void refill_and_restart(uint8_t dma_channel, int32_t *buffer)
{
    generate_sine(buffer, SAMPLES_PER_BUFFER);
    
    dma_channel_config *cfg = (dma_channel == DMA_CHAN0) ? &dma_cfg0 : &dma_cfg1;
    
    dma_channel_configure(
        dma_channel,
        cfg,
        &AUDIO_PIO->txf[AUDIO_SM],
        buffer,
        SAMPLES_PER_BUFFER * 2,
        false  // Don't trigger - chaining will trigger it
    );
}

static void __isr dma_irq_handler(void)
{
    if (dma_irqn_get_channel_status(0, DMA_CHAN0)) {
        dma_irqn_acknowledge_channel(0, DMA_CHAN0);
        irq_count++;
        refill_and_restart(DMA_CHAN0, buffer0);
    }
    
    if (dma_irqn_get_channel_status(0, DMA_CHAN1)) {
        dma_irqn_acknowledge_channel(0, DMA_CHAN1);
        refill_and_restart(DMA_CHAN1, buffer1);
    }
}

int main(void)
{
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    for (int i = 0; i < 3; i++) {
        gpio_put(PICO_DEFAULT_LED_PIN, 1); sleep_ms(100);
        gpio_put(PICO_DEFAULT_LED_PIN, 0); sleep_ms(100);
    }

    stdio_init_all();
    sleep_ms(2000);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);

    printf("\n\n========================================\n");
    printf("I2S DAC - Chained DMA (like elehobica)\n");
    printf("========================================\n\n");

    // 96MHz clock
    pll_init(pll_usb, 1, 1536 * MHZ, 4, 4);
    clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 96 * MHZ, 48 * MHZ);
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                   CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 96 * MHZ, 96 * MHZ);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, 96 * MHZ, 96 * MHZ);
    stdio_init_all(); sleep_ms(100);

    uint32_t sysclk = clock_get_hz(clk_sys);
    printf("Clock: %u Hz (%.1f MHz)\n\n", (unsigned)sysclk, sysclk/1e6f);

    // Sine table
    for (int i = 0; i < 256; i++) {
        int16_t s16 = (int16_t)(32767.0 * sin(2.0 * M_PI * i / 256.0));
        sine_table[i] = ((int32_t)s16 << 16) | (s16 & 0xFFFF);
    }

    // Init buffers
    for (int i = 0; i < SAMPLES_PER_BUFFER; i++) {
        buffer0[i*2] = DAC_ZERO; buffer0[i*2+1] = DAC_ZERO;
        buffer1[i*2] = DAC_ZERO; buffer1[i*2+1] = DAC_ZERO;
    }
    generate_sine(buffer0, SAMPLES_PER_BUFFER);
    generate_sine(buffer1, SAMPLES_PER_BUFFER);

    // PIO setup
    pio_sm_claim(AUDIO_PIO, AUDIO_SM);
    uint offset = pio_add_program(AUDIO_PIO, &audio_i2s_program);
    audio_i2s_program_init(AUDIO_PIO, AUDIO_SM, offset, DIN_PIN, BCLK_PIN, BITS_PER_SAMPLE);

    // Clock: elehobica formula
    uint32_t divider = (sysclk * 2) / SAMPLE_RATE;
    float clkdiv = (float)(divider >> 8);
    pio_sm_set_clkdiv(AUDIO_PIO, AUDIO_SM, clkdiv);
    
    printf("PIO: offset=%d, divider_raw=%u, clkdiv=%.2f\n", offset, divider, clkdiv);
    printf("PIO freq: %.3f MHz\n\n", (sysclk / clkdiv) / 1e6f);

    // DMA setup with CHAINING
    dma_channel_claim(DMA_CHAN0);
    dma_channel_claim(DMA_CHAN1);

    // Channel 0 config
    dma_cfg0 = dma_channel_get_default_config(DMA_CHAN0);
    channel_config_set_transfer_data_size(&dma_cfg0, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg0, true);
    channel_config_set_write_increment(&dma_cfg0, false);
    channel_config_set_dreq(&dma_cfg0, pio_get_dreq(AUDIO_PIO, AUDIO_SM, true));
    channel_config_set_chain_to(&dma_cfg0, DMA_CHAN1);  // Chain to channel 1

    // Channel 1 config  
    dma_cfg1 = dma_channel_get_default_config(DMA_CHAN1);
    channel_config_set_transfer_data_size(&dma_cfg1, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg1, true);
    channel_config_set_write_increment(&dma_cfg1, false);
    channel_config_set_dreq(&dma_cfg1, pio_get_dreq(AUDIO_PIO, AUDIO_SM, true));
    channel_config_set_chain_to(&dma_cfg1, DMA_CHAN0);  // Chain back to channel 0

    // Initial config (no trigger)
    dma_channel_configure(DMA_CHAN0, &dma_cfg0,
        &AUDIO_PIO->txf[AUDIO_SM], buffer0, SAMPLES_PER_BUFFER * 2, false);
    
    dma_channel_configure(DMA_CHAN1, &dma_cfg1,
        &AUDIO_PIO->txf[AUDIO_SM], buffer1, SAMPLES_PER_BUFFER * 2, false);

    // IRQ
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_irqn_set_channel_enabled(0, DMA_CHAN0, true);
    dma_irqn_set_channel_enabled(0, DMA_CHAN1, true);

    printf("Starting audio (only DMA0, chained to DMA1)...\n\n");
    
    // CRITICAL: Only start channel 0! Channel 1 starts via chaining
    dma_channel_start(DMA_CHAN0);

    // Monitor
    uint32_t last_irq = 0;
    while (true) {
        sleep_ms(500);
        uint32_t fifo = pio_sm_get_tx_fifo_level(AUDIO_PIO, AUDIO_SM);
        bool busy0 = dma_channel_is_busy(DMA_CHAN0);
        bool busy1 = dma_channel_is_busy(DMA_CHAN1);
        printf("IRQs: %u (+%u) FIFO: %u DMA0: %s DMA1: %s  \n",
               (unsigned)irq_count, (unsigned)(irq_count - last_irq),
               fifo, busy0 ? "BUSY" : "IDLE", busy1 ? "BUSY" : "IDLE");
        last_irq = irq_count;
    }
}
