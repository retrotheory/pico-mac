/* dac_test_square.c - Square wave test for PCM5102
 * Outputs 500Hz square wave - easier to verify on scope
 */

#include <stdio.h>
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define DIN_PIN     8
#define BCLK_PIN    6
#define LRCK_PIN    7

#define SAMPLE_RATE 22254
#define TONE_HZ     500

static uint32_t sample_count = 0;

/* Output one I2S sample - left-justified format (simpler) */
static void i2s_output_left_justified(int16_t sample)
{
    /* In left-justified mode:
     * LRCK high = left, LRCK low = right (opposite of I2S)
     * Data starts immediately after LRCK edge
     */
    
    /* Left channel (LRCK high) */
    gpio_put(LRCK_PIN, 1);
    for (int i = 15; i >= 0; i--) {
        gpio_put(DIN_PIN, (sample >> i) & 1);
        gpio_put(BCLK_PIN, 0);
        for (volatile int j = 0; j < 20; j++);
        gpio_put(BCLK_PIN, 1);
        for (volatile int j = 0; j < 20; j++);
    }
    
    /* Right channel (LRCK low) */
    gpio_put(LRCK_PIN, 0);
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
    
    printf("DAC Test - 500Hz square wave (Left-Justified format)\n");
    printf("Wiring: GPIO%d=BCK, GPIO%d=LRCK, GPIO%d=DIN\n", BCLK_PIN, LRCK_PIN, DIN_PIN);
    printf("FMT pin should be HIGH for left-justified mode\n");
    
    /* Init GPIO */
    gpio_init(DIN_PIN);
    gpio_init(BCLK_PIN);
    gpio_init(LRCK_PIN);
    gpio_set_dir(DIN_PIN, GPIO_OUT);
    gpio_set_dir(BCLK_PIN, GPIO_OUT);
    gpio_set_dir(LRCK_PIN, GPIO_OUT);
    gpio_put(DIN_PIN, 0);
    gpio_put(BCLK_PIN, 0);
    gpio_put(LRCK_PIN, 0);
    
    printf("Outputting square wave...\n");
    
    /* Output samples in loop */
    while (true) {
        /* Generate square wave */
        uint32_t idx = (sample_count * TONE_HZ / SAMPLE_RATE) % 2;
        int16_t sample = idx ? 20000 : -20000;  /* Square wave */
        
        i2s_output_left_justified(sample);
        sample_count++;
        
        /* Blink LED slowly */
        if ((sample_count % SAMPLE_RATE) == 0) {
            static int led = 0;
            led = !led;
            gpio_put(PICO_DEFAULT_LED_PIN, led);
        }
    }
    
    return 0;
}
