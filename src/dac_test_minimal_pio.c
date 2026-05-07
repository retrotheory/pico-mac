/* Minimal PIO I2S test - just output a walking pattern */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"

#include "audio_i2s.pio.h"

#define DIN_PIN     8
#define BCLK_PIN    6

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);
    
    printf("\n\nMinimal PIO Test\n");
    printf("================\n\n");
    
    // Use default clock (125MHz on Pico)
    uint32_t sysclk = clock_get_hz(clk_sys);
    printf("System clock: %u MHz\n", sysclk / 1000000);
    
    // Claim and setup PIO
    pio_sm_claim(pio1, 0);
    uint offset = pio_add_program(pio1, &audio_i2s_program);
    printf("Program at offset %d\n", offset);
    
    // Init with 32-bit
    audio_i2s_program_init(pio1, 0, offset, DIN_PIN, BCLK_PIN, 32);
    
    // Set clock: 44100 * 33 * 4 = 5.82MHz needed
    // 125MHz / 21.5 = 5.81MHz
    float clkdiv = (float)sysclk / (44100.0f * 33.0f * 4.0f);
    pio_sm_set_clkdiv(pio1, 0, clkdiv);
    printf("Clock divider: %.2f\n\n", clkdiv);
    
    // Manually push pattern to FIFO (bypass DMA)
    printf("Pushing pattern to PIO FIFO...\n");
    
    // Pattern: alternating 0xAAAAAAAA and 0x55555555
    // This should create toggling bits on DIN
    for (int i = 0; i < 8; i++) {
        pio_sm_put(pio1, 0, (i & 1) ? 0xAAAAAAAA : 0x55555555);
    }
    
    printf("FIFO level: %u\n", pio_sm_get_tx_fifo_level(pio1, 0));
    printf("PIO should be running...\n");
    printf("Check scope on pins 6 (BCLK), 7 (LRCK), 8 (DIN)\n\n");
    
    // Keep refilling FIFO
    uint32_t last_fifo = 8;
    while (true) {
        uint32_t fifo = pio_sm_get_tx_fifo_level(pio1, 0);
        if (fifo < 4) {
            // Refill
            static uint32_t val = 0;
            pio_sm_put(pio1, 0, val);
            val = ~val;
        }
        
        if (fifo != last_fifo) {
            printf("FIFO: %u\n", fifo);
            last_fifo = fifo;
        }
        
        sleep_us(10);
    }
}
