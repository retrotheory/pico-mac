/* dac_test_pio_two_tones.c - PIO-based I2S with two tones
 *
 * Uses PIO for precise timing - left-justified format
 * FMT=LOW for your DAC board
 * Two tones: 500Hz and 1kHz, alternating
 */

#include <stdio.h>
#include <math.h>
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "pio_i2s.pio.h"

#define DIN_PIN     8
#define BCLK_PIN    6
#define LRCK_PIN    7
#define I2S_PIO     pio1
#define I2S_SM      0
#define I2S_DMA     2

#define SAMPLE_RATE 22254

/* Two tone buffers - 32-bit samples (16-bit data in upper 16 bits) */
static uint32_t tone_1k[22254];   /* 1 second of 1kHz */
static uint32_t tone_500[22254];  /* 1 second of 500Hz */

/* Generate sine table - 32-bit samples with 16-bit data in upper bits */
static void generate_tones(void)
{
    for (int i = 0; i < 22254; i++) {
        /* 1kHz sine - upper 16 bits */
        float phase1 = 2.0f * 3.14159f * 1000.0f * i / SAMPLE_RATE;
        int16_t sample1 = (int16_t)(30000.0f * sinf(phase1));
        tone_1k[i] = ((uint32_t)(uint16_t)sample1) << 16;
        
        /* 500Hz sine - upper 16 bits */
        float phase2 = 2.0f * 3.14159f * 500.0f * i / SAMPLE_RATE;
        int16_t sample2 = (int16_t)(30000.0f * sinf(phase2));
        tone_500[i] = ((uint32_t)(uint16_t)sample2) << 16;
    }
}

int main()
{
    stdio_init_all();
    
    printf("PIO I2S Two-Tone Test\n");
    printf("FMT pin = LOW (left-justified format)\n");
    printf("GPIO%d=BCK, GPIO%d=LRCK, GPIO%d=DIN\n", BCLK_PIN, LRCK_PIN, DIN_PIN);
    
    generate_tones();
    
    /* Load PIO program */
    uint offset = pio_add_program(I2S_PIO, &pio_i2s_program);
    float clkdiv = pio_i2s_calculate_clkdiv(SAMPLE_RATE, clock_get_hz(clk_sys));
    
    pio_i2s_program_init(I2S_PIO, I2S_SM, offset, DIN_PIN, BCLK_PIN, clkdiv);
    
    /* Setup DMA for 32-bit samples */
    dma_channel_config cfg = dma_channel_get_default_config(I2S_DMA);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(I2S_PIO, I2S_SM, true));
    
    printf("Playing alternating tones: 1kHz / 500Hz\n");
    
    int which_tone = 0;
    while (true) {
        /* Alternate between tones every 2 seconds */
        uint32_t *current = which_tone ? tone_1k : tone_500;
        printf("Playing %s tone...\n", which_tone ? "1kHz" : "500Hz");
        
        /* Configure and start DMA */
        dma_channel_configure(
            I2S_DMA,
            &cfg,
            &I2S_PIO->txf[I2S_SM],
            current,
            SAMPLE_RATE * 2,  /* 2 seconds */
            true
        );
        
        /* Enable PIO */
        pio_sm_set_enabled(I2S_PIO, I2S_SM, true);
        
        /* Wait for DMA to complete */
        dma_channel_wait_for_finish_blocking(I2S_DMA);
        
        /* Switch tone */
        which_tone = !which_tone;
        
        /* Blink LED */
        static int led = 0;
        led = !led;
        gpio_put(PICO_DEFAULT_LED_PIN, led);
    }
    
    return 0;
}
