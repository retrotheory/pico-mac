/* dac_test_left_justified.c - Left-justified format test
 * This matches what works on your DAC with FMT=LOW
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

/* LEFT-JUSTIFIED format - data starts immediately with LRCK edge */
static void output_sample(int16_t sample)
{
    /* Left channel (LRCK high) - data starts immediately */
    gpio_put(LRCK_PIN, 1);
    for (int i = 15; i >= 0; i--) {
        gpio_put(DIN_PIN, (sample >> i) & 1);
        gpio_put(BCLK_PIN, 0);
        for (volatile int j = 0; j < 20; j++);
        gpio_put(BCLK_PIN, 1);
        for (volatile int j = 0; j < 20; j++);
    }
    
    /* Right channel (LRCK low) - data starts immediately */
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
    
    printf("DAC Test - LEFT-JUSTIFIED format\n");
    printf("FMT pin should be LOW for this format on your board\n");
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
    
    printf("Outputting 1kHz tone...\n");
    
    while (true) {
        uint32_t idx = (sample_count * TONE_HZ / SAMPLE_RATE) % 16;
        int16_t sample = sine_table[idx];
        
        output_sample(sample);
        sample_count++;
        
        if ((sample_count % SAMPLE_RATE) == 0) {
            static int led = 0;
            led = !led;
            gpio_put(PICO_DEFAULT_LED_PIN, led);
        }
    }
    
    return 0;
}
