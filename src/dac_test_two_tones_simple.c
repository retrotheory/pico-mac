/* dac_test_two_tones_simple.c - Simple two-tone test like working dac_test_square
 *
 * Just alternates between two tones every 2 seconds
 * Uses simple GPIO bit-banging (proven to work)
 */

#include <stdio.h>
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define DIN_PIN     8
#define BCLK_PIN    6
#define LRCK_PIN    7

#define SAMPLE_RATE 22254

/* Square wave tables */
static const int16_t tone_1k[] = { 30000, -30000 };  /* 1kHz = toggle every 11 samples @ 22kHz */
static const int16_t tone_500[] = { 30000, -30000 }; /* 500Hz = toggle every 22 samples */

static void output_sample(int16_t sample)
{
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
    
    printf("Two-Tone Test - Simple GPIO bit-banging\n");
    printf("FMT=LOW (left-justified on your DAC)\n");
    printf("GPIO%d=BCK, GPIO%d=LRCK, GPIO%d=DIN\n\n", BCLK_PIN, LRCK_PIN, DIN_PIN);
    
    gpio_init(DIN_PIN);
    gpio_init(BCLK_PIN);
    gpio_init(LRCK_PIN);
    gpio_set_dir(DIN_PIN, GPIO_OUT);
    gpio_set_dir(BCLK_PIN, GPIO_OUT);
    gpio_set_dir(LRCK_PIN, GPIO_OUT);
    gpio_put(DIN_PIN, 0);
    gpio_put(BCLK_PIN, 0);
    gpio_put(LRCK_PIN, 0);
    
    printf("Alternating: 1kHz tone / 500Hz tone every 2 seconds\n");
    
    uint32_t sample_count = 0;
    int tone_select = 0;  /* 0 = 1kHz, 1 = 500Hz */
    uint32_t samples_per_tone = SAMPLE_RATE * 2;  /* 2 seconds */
    
    while (true) {
        /* Generate square wave */
        int16_t sample;
        if (tone_select == 0) {
            /* 1kHz: toggle every 11 samples */
            sample = ((sample_count / 11) & 1) ? 20000 : -20000;
        } else {
            /* 500Hz: toggle every 22 samples */
            sample = ((sample_count / 22) & 1) ? 20000 : -20000;
        }
        
        output_sample(sample);
        sample_count++;
        
        /* Switch tone every 2 seconds */
        if (sample_count >= samples_per_tone) {
            sample_count = 0;
            tone_select = !tone_select;
            printf("Switching to %s tone\n", tone_select ? "500Hz" : "1kHz");
            
            /* Blink LED */
            static int led = 0;
            led = !led;
            gpio_put(PICO_DEFAULT_LED_PIN, led);
        }
    }
    
    return 0;
}
