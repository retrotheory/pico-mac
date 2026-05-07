/* dac_test_two_tones.c - Two alternating tones to prove GPIO audio works
 * Plays 440Hz for 1 second, then 880Hz for 1 second, alternating
 */

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define DIN_PIN     8
#define BCLK_PIN    6
#define LRCK_PIN    7

#define SAMPLE_RATE 22254

// Pre-computed sine table for speed
#define SINE_TABLE_SIZE 256
static int16_t sine_table[SINE_TABLE_SIZE];

static uint32_t phase = 0;

static void init_sine_table(void)
{
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        sine_table[i] = (int16_t)(30000.0 * sin(2.0 * M_PI * i / SINE_TABLE_SIZE));
    }
}

/* Output one I2S sample - left-justified format (same as working dac_test_square) */
static void i2s_output_sample(int16_t sample)
{
    // Left channel (LRCK high)
    gpio_put(LRCK_PIN, 1);
    for (int i = 15; i >= 0; i--) {
        gpio_put(DIN_PIN, (sample >> i) & 1);
        gpio_put(BCLK_PIN, 0);
        for (volatile int j = 0; j < 20; j++);
        gpio_put(BCLK_PIN, 1);
        for (volatile int j = 0; j < 20; j++);
    }
    
    // Right channel (LRCK low)
    gpio_put(LRCK_PIN, 0);
    for (int i = 15; i >= 0; i--) {
        gpio_put(DIN_PIN, (sample >> i) & 1);
        gpio_put(BCLK_PIN, 0);
        for (volatile int j = 0; j < 20; j++);
        gpio_put(BCLK_PIN, 1);
        for (volatile int j = 0; j < 20; j++);
    }
}

/* Generate next sample for given frequency */
static int16_t generate_sample(uint32_t freq_hz)
{
    // Phase accumulator
    phase += (freq_hz * SINE_TABLE_SIZE) / SAMPLE_RATE;
    phase &= (SINE_TABLE_SIZE - 1); // Wrap at 256
    
    return sine_table[phase];
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);
    
    printf("\n\n========================================\n");
    printf("Two Tone Test - GPIO I2S\n");
    printf("========================================\n");
    printf("Pins: DIN=%d, BCLK=%d, LRCK=%d\n", DIN_PIN, BCLK_PIN, LRCK_PIN);
    printf("FMT pin should be HIGH for left-justified\n\n");
    
    // Init GPIO (exact same as working dac_test_square)
    gpio_init(DIN_PIN);
    gpio_init(BCLK_PIN);
    gpio_init(LRCK_PIN);
    gpio_set_dir(DIN_PIN, GPIO_OUT);
    gpio_set_dir(BCLK_PIN, GPIO_OUT);
    gpio_set_dir(LRCK_PIN, GPIO_OUT);
    gpio_put(DIN_PIN, 0);
    gpio_put(BCLK_PIN, 0);
    gpio_put(LRCK_PIN, 0);
    
    // Init sine table
    init_sine_table();
    
    printf("Playing alternating tones:\n");
    printf("  Tone 1: 440 Hz (A4)\n");
    printf("  Tone 2: 880 Hz (A5)\n");
    printf("  Each for 1 second\n\n");
    
    uint32_t tone_duration = SAMPLE_RATE; // 1 second worth of samples
    uint32_t sample_count = 0;
    uint32_t current_freq = 440; // Start with 440Hz
    
    while (true) {
        // Generate and output sample
        int16_t sample = generate_sample(current_freq);
        i2s_output_sample(sample);
        
        sample_count++;
        
        // Switch tone every second
        if (sample_count >= tone_duration) {
            sample_count = 0;
            current_freq = (current_freq == 440) ? 880 : 440;
            phase = 0; // Reset phase for clean switch
            
            // Blink LED to show tone switch
            static int led = 0;
            led = !led;
            gpio_put(PICO_DEFAULT_LED_PIN, led);
        }
    }
    
    return 0;
}
