/* dac_test_i2s_proper.c - Proper I2S format test for PCM5102
 * 
 * I2S format: Data starts one BCLK cycle AFTER LRCK edge
 * FMT pin should be LOW (GND) for I2S mode
 */

#include <stdio.h>
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define DIN_PIN     8
#define BCLK_PIN    6
#define LRCK_PIN    7

#define SAMPLE_RATE 22254
#define TONE_HZ     1000

static const int16_t sine_table[16] = {
    0, 12539, 23170, 30273, 32767, 30273, 23170, 12539,
    0, -12539, -23170, -30273, -32767, -30273, -23170, -12539
};

static uint32_t sample_count = 0;

/* Output one I2S sample in PROPER I2S format
 * I2S: Data starts one BCLK cycle after LRCK edge
 */
static void i2s_output_proper(int16_t sample)
{
    /* Left channel - LRCK low, but data starts after one BCLK */
    gpio_put(LRCK_PIN, 0);
    
    /* One dummy BCLK cycle (LRCK just changed, no data yet) */
    gpio_put(DIN_PIN, 0);
    gpio_put(BCLK_PIN, 0);
    for (volatile int j = 0; j < 20; j++);
    gpio_put(BCLK_PIN, 1);
    for (volatile int j = 0; j < 20; j++);
    
    /* Now output 16 bits of data */
    for (int i = 15; i >= 0; i--) {
        gpio_put(DIN_PIN, (sample >> i) & 1);
        gpio_put(BCLK_PIN, 0);
        for (volatile int j = 0; j < 20; j++);
        gpio_put(BCLK_PIN, 1);
        for (volatile int j = 0; j < 20; j++);
    }
    
    /* Right channel - LRCK high, data starts after one BCLK */
    gpio_put(LRCK_PIN, 1);
    
    /* One dummy BCLK cycle */
    gpio_put(DIN_PIN, 0);
    gpio_put(BCLK_PIN, 0);
    for (volatile int j = 0; j < 20; j++);
    gpio_put(BCLK_PIN, 1);
    for (volatile int j = 0; j < 20; j++);
    
    /* Now output 16 bits */
    for (int i = 15; i >= 0; i--) {
        gpio_put(DIN_PIN, (sample >> i) & 1);
        gpio_put(BCLK_PIN, 0);
        for (volatile int j = 0; j < 20; j++);
        gpio_put(BCLK_PIN, 1);
        for (volatile int j = 0; j < 20; j++);
    }
}

int main()
{
    stdio_init_all();
    
    printf("DAC Test - PROPER I2S format\n");
    printf("FMT pin should be LOW (GND) for I2S mode\n");
    printf("Wiring: GPIO%d=BCK, GPIO%d=LRCK, GPIO%d=DIN\n", BCLK_PIN, LRCK_PIN, DIN_PIN);
    
    gpio_init(DIN_PIN);
    gpio_init(BCLK_PIN);
    gpio_init(LRCK_PIN);
    gpio_set_dir(DIN_PIN, GPIO_OUT);
    gpio_set_dir(BCLK_PIN, GPIO_OUT);
    gpio_set_dir(LRCK_PIN, GPIO_OUT);
    gpio_put(DIN_PIN, 0);
    gpio_put(BCLK_PIN, 0);
    gpio_put(LRCK_PIN, 0);
    
    printf("Outputting 1kHz tone in I2S format...\n");
    
    while (true) {
        /* Generate 1kHz tone */
        uint32_t idx = (sample_count * TONE_HZ / SAMPLE_RATE) % 16;
        int16_t sample = sine_table[idx];
        
        i2s_output_proper(sample);
        sample_count++;
        
        /* Blink LED */
        if ((sample_count % SAMPLE_RATE) == 0) {
            static int led = 0;
            led = !led;
            gpio_put(PICO_DEFAULT_LED_PIN, led);
        }
    }
    
    return 0;
}
