/* dac_test_pio_working.c - PIO I2S based on elehobica's working library
 *
 * Uses proper PIO I2S implementation
 * Left-justified format, FMT=LOW
 * Two tones: 500Hz and 1kHz alternating
 */

#include <stdio.h>
#include <math.h>
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "pio_i2s.pio.h"

#define DIN_PIN     8
#define BCLK_PIN    6
#define I2S_PIO     pio1
#define I2S_SM      0
#define I2S_DMA     2

#define SAMPLE_RATE 22254

/* Stereo buffer: 32-bit words, left in upper 16, right in lower 16 */
static uint32_t tone_1k[22254];
static uint32_t tone_500[22254];

static void generate_tones(void)
{
    for (int i = 0; i < 22254; i++) {
        /* 1kHz sine */
        float phase1 = 2.0f * 3.14159f * 1000.0f * i / SAMPLE_RATE;
        int16_t sample1 = (int16_t)(30000.0f * sinf(phase1));
        
        /* 500Hz sine */
        float phase2 = 2.0f * 3.14159f * 500.0f * i / SAMPLE_RATE;
        int16_t sample2 = (int16_t)(30000.0f * sinf(phase2));
        
        /* Pack stereo: left << 16 | right (mono = same sample both channels) */
        tone_1k[i] = ((uint32_t)(uint16_t)sample1 << 16) | (uint16_t)sample1;
        tone_500[i] = ((uint32_t)(uint16_t)sample2 << 16) | (uint16_t)sample2;
    }
}

int main()
{
    stdio_init_all();
    
    printf("PIO I2S Working Test\n");
    printf("FMT=LOW for left-justified format\n");
    printf("GPIO%d=BCK, GPIO%d=LRCK, GPIO%d=DIN\n", BCLK_PIN, BCLK_PIN+1, DIN_PIN);
    
    generate_tones();
    
    /* Load PIO program */
    uint offset = pio_add_program(I2S_PIO, &i2s_program);
    float clkdiv = i2s_calculate_clkdiv(SAMPLE_RATE, clock_get_hz(clk_sys));
    
    i2s_program_init(I2S_PIO, I2S_SM, offset, DIN_PIN, BCLK_PIN);
    pio_sm_set_clkdiv(I2S_PIO, I2S_SM, clkdiv);
    
    /* Setup DMA for 32-bit stereo samples */
    dma_channel_config cfg = dma_channel_get_default_config(I2S_DMA);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(I2S_PIO, I2S_SM, true));
    
    printf("Playing 1kHz / 500Hz alternating tones...\n");
    
    /* Pre-load FIFO to prevent SM from stalling */
    for (int i = 0; i < 4; i++) {
        pio_sm_put_blocking(I2S_PIO, I2S_SM, tone_1k[i]);
    }
    
    int which_tone = 0;
    uint32_t samples_played = 4;  /* Already sent 4 samples */
    
    while (true) {
        uint32_t *current = which_tone ? tone_1k : tone_500;
        printf("Tone: %s\n", which_tone ? "1kHz" : "500Hz");
        
        /* Start DMA from position 4 (after pre-load) */
        dma_channel_configure(
            I2S_DMA,
            &cfg,
            &I2S_PIO->txf[I2S_SM],
            &current[4],
            (SAMPLE_RATE * 2) - 4,  /* 2 seconds minus pre-load */
            true
        );
        
        /* Wait for completion */
        dma_channel_wait_for_finish_blocking(I2S_DMA);
        
        which_tone = !which_tone;
        
        /* Blink LED */
        static int led = 0;
        led = !led;
        gpio_put(PICO_DEFAULT_LED_PIN, led);
    }
    
    return 0;
}
