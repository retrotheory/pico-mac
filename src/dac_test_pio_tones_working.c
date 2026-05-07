/* dac_test_pio_tones_working.c - PIO version of working two-tone test
 *
 * Converts the working GPIO bit-bang to PIO
 */

#include <stdio.h>
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"

#include "pio_i2s.pio.h"

#define DIN_PIN     8
#define BCLK_PIN    6
#define SAMPLE_RATE 22254

/* Generate 2 seconds of each tone as 32-bit stereo samples */
static uint32_t tone_1k[SAMPLE_RATE * 2];
static uint32_t tone_500[SAMPLE_RATE * 2];

static void generate_tones(void)
{
    printf("Generating tones...\n");
    
    for (int i = 0; i < SAMPLE_RATE * 2; i++) {
        /* 1kHz: toggle every 11 samples */
        int16_t sample_1k = ((i / 11) & 1) ? 20000 : -20000;
        /* Pack stereo: left << 16 | right (same for mono) */
        tone_1k[i] = ((uint32_t)(uint16_t)sample_1k << 16) | (uint16_t)sample_1k;
        
        /* 500Hz: toggle every 22 samples */
        int16_t sample_500 = ((i / 22) & 1) ? 20000 : -20000;
        tone_500[i] = ((uint32_t)(uint16_t)sample_500 << 16) | (uint16_t)sample_500;
    }
}

int main()
{
    stdio_init_all();
    
    printf("PIO Two-Tone Test\n");
    printf("FMT=LOW for your DAC\n");
    printf("GPIO%d=BCK, GPIO%d=LRCK, GPIO%d=DIN\n\n", 
           BCLK_PIN, BCLK_PIN+1, DIN_PIN);
    
    generate_tones();
    
    /* Setup PIO */
    PIO pio = pio1;
    uint sm = 0;
    uint offset = pio_add_program(pio, &i2s_program);
    
    i2s_program_init(pio, sm, offset, DIN_PIN, BCLK_PIN);
    
    /* Set clock divider for 22.254kHz sample rate */
    float div = (float)clock_get_hz(clk_sys) / (float)(SAMPLE_RATE * 32 * 2);
    pio_sm_set_clkdiv(pio, sm, div);
    
    /* Setup DMA */
    uint dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, true));
    
    printf("Starting tone playback...\n");
    
    int tone_select = 0;
    while (true) {
        uint32_t *current_tone = tone_select ? tone_500 : tone_1k;
        printf("Playing %s tone...\n", tone_select ? "500Hz" : "1kHz");
        
        /* Start DMA */
        dma_channel_configure(
            dma_chan,
            &cfg,
            &pio->txf[sm],
            current_tone,
            SAMPLE_RATE * 2,
            true
        );
        
        /* Wait for completion */
        dma_channel_wait_for_finish_blocking(dma_chan);
        
        /* Switch tone */
        tone_select = !tone_select;
        
        /* Blink LED */
        static int led = 0;
        led = !led;
        gpio_put(PICO_DEFAULT_LED_PIN, led);
    }
    
    return 0;
}
