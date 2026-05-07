/* dac_test_elehobica_exact.c - Exact copy of working elehobica approach
 *
 * Uses their PIO program and 32-bit S32 samples at 44100 Hz
 */

#include <stdio.h>
#include <math.h>
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"
#include "pico/stdlib.h"

#include "audio_i2s.pio.h"

#define DIN_PIN     8
#define BCLK_PIN    6
#define SAMPLE_RATE 44100  /* Their sample rate */

/* S32 stereo buffer */
static int32_t tone_1k[SAMPLE_RATE * 2];
static int32_t tone_500[SAMPLE_RATE * 2];

static void generate_tones(void)
{
    for (int i = 0; i < SAMPLE_RATE * 2; i++) {
        /* 1kHz sine - full 32-bit scale */
        float phase1 = 2.0f * M_PI * 1000.0f * i / SAMPLE_RATE;
        int32_t sample1 = (int32_t)(0x3FFFFFFF * sinf(phase1));
        tone_1k[i] = sample1;
        
        /* 500Hz sine */
        float phase2 = 2.0f * M_PI * 500.0f * i / SAMPLE_RATE;
        int32_t sample2 = (int32_t)(0x3FFFFFFF * sinf(phase2));
        tone_500[i] = sample2;
    }
}

/* Clock setup like their sample */
static void setup_clocks(void)
{
    // Set PLL_USB 96MHz
    pll_init(pll_usb, 1, 1536 * MHZ, 4, 4);
    clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 96 * MHZ, 48 * MHZ);
    
    // Change clk_sys to be 96MHz
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 96 * MHZ, 96 * MHZ);
    
    // CLK peri is clocked from clk_sys
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, 96 * MHZ, 96 * MHZ);
}

int main()
{
    stdio_init_all();
    setup_clocks();
    stdio_init_all();  // Reinit after clock change
    
    printf("Elehobica EXACT Test\n");
    printf("GPIO%d=BCK, GPIO%d=LRCK, GPIO%d=DIN\n", BCLK_PIN, BCLK_PIN+1, DIN_PIN);
    
    generate_tones();
    
    PIO pio = pio0;  /* They use pio0 */
    uint sm = 0;
    uint offset = pio_add_program(pio, &audio_i2s_program);
    
    /* EXACT init from their library */
    audio_i2s_program_init(pio, sm, offset, DIN_PIN, BCLK_PIN, 32);  /* 32-bit resolution */
    
    /* Set clock divider - their calculation */
    uint32_t system_clock_frequency = clock_get_hz(clk_sys);
    uint32_t divider = system_clock_frequency * 1 * 2 / SAMPLE_RATE;  /* For 32-bit stereo */
    pio_sm_set_clkdiv_int_frac(pio, sm, divider >> 8u, divider & 0xffu);
    
    /* Setup DMA */
    uint dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, true));
    
    printf("Playing tones at %d Hz...\n", SAMPLE_RATE);
    
    int which_tone = 0;
    while (true) {
        int32_t *current = which_tone ? tone_1k : tone_500;
        printf("Tone: %s\n", which_tone ? "1kHz" : "500Hz");
        
        dma_channel_configure(dma_chan, &cfg, &pio->txf[sm], current, SAMPLE_RATE * 2, true);
        dma_channel_wait_for_finish_blocking(dma_chan);
        
        which_tone = !which_tone;
        gpio_put(PICO_DEFAULT_LED_PIN, which_tone);
    }
    
    return 0;
}
