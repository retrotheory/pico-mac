/* Test Mac-style audio with GPIO bit-bang */
#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define DIN_PIN     8
#define BCLK_PIN    6
#define LRCK_PIN    7

// Simulated Mac audio buffer (370 samples at ~22kHz)
#define SAMPLES_PER_FRAME 370
static uint8_t fake_mac_buffer[SAMPLES_PER_FRAME * 2];

static void output_i2s_sample(int16_t left, int16_t right)
{
    // Left (LRCK high)
    gpio_put(LRCK_PIN, 1);
    for (int i = 15; i >= 0; i--) {
        gpio_put(DIN_PIN, (left >> i) & 1);
        gpio_put(BCLK_PIN, 0);
        for (volatile int j = 0; j < 8; j++);
        gpio_put(BCLK_PIN, 1);
        for (volatile int j = 0; j < 8; j++);
    }
    // Right (LRCK low)
    gpio_put(LRCK_PIN, 0);
    for (int i = 15; i >= 0; i--) {
        gpio_put(DIN_PIN, (right >> i) & 1);
        gpio_put(BCLK_PIN, 0);
        for (volatile int j = 0; j < 8; j++);
        gpio_put(BCLK_PIN, 1);
        for (volatile int j = 0; j < 8; j++);
    }
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);
    
    printf("\n\nMac-style GPIO Audio Test\n");
    printf("=========================\n");
    printf("Pins: DIN=%d, BCLK=%d, LRCK=%d\n\n", DIN_PIN, BCLK_PIN, LRCK_PIN);
    
    // Init GPIO
    gpio_init(DIN_PIN);
    gpio_init(BCLK_PIN);
    gpio_init(LRCK_PIN);
    gpio_set_dir(DIN_PIN, GPIO_OUT);
    gpio_set_dir(BCLK_PIN, GPIO_OUT);
    gpio_set_dir(LRCK_PIN, GPIO_OUT);
    gpio_put(DIN_PIN, 0);
    gpio_put(BCLK_PIN, 0);
    gpio_put(LRCK_PIN, 0);
    
    // Generate test pattern in fake Mac buffer (440Hz sine)
    for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
        float phase = 2.0f * 3.14159f * 440.0f * i / 22254.0f;
        int16_t sample = (int16_t)(100.0f * sin(phase)); // Reduced volume
        fake_mac_buffer[i * 2] = (uint8_t)(sample >> 8) + 128; // Convert to unsigned
    }
    
    printf("Outputting 440Hz tone...\n");
    printf("Press any key to stop\n\n");
    
    uint32_t sample_idx = 0;
    while (getchar_timeout_us(0) < 0) {
        // Get sample from fake Mac buffer
        uint8_t mac_sample = fake_mac_buffer[sample_idx * 2];
        int16_t centered = (int16_t)mac_sample - 128;
        centered = centered << 8; // Expand to 16-bit
        
        // Output stereo
        output_i2s_sample(centered, centered);
        
        sample_idx++;
        if (sample_idx >= SAMPLES_PER_FRAME) {
            sample_idx = 0;
        }
    }
    
    printf("Stopped.\n");
    return 0;
}
