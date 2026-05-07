/* dac_test.c - Standalone test for PCM5102 DAC
 * Outputs simple tones to verify wiring
 */

#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

/* Pin definitions - match hw.h */
#define DIN_PIN     8
#define BCLK_PIN    6
#define LRCK_PIN    7

/* Tone frequency */
#define TONE_HZ     1000
#define SAMPLE_RATE 22254

/* Simple sine table (16 samples) */
static const int16_t sine_table[16] = {
    0, 12539, 23170, 30273, 32767, 30273, 23170, 12539,
    0, -12539, -23170, -30273, -32767, -30273, -23170, -12539
};

static volatile uint32_t sample_count = 0;

/* Output one I2S sample */
static void i2s_output(int16_t sample)
{
    /* Left channel (LRCK low) */
    gpio_put(LRCK_PIN, 0);
    for (int i = 15; i >= 0; i--) {
        gpio_put(DIN_PIN, (sample >> i) & 1);
        gpio_put(BCLK_PIN, 0);
        __asm volatile ("nop\n nop\n nop\n nop\n nop\n nop\n");
        gpio_put(BCLK_PIN, 1);
        __asm volatile ("nop\n nop\n nop\n nop\n nop\n nop\n");
    }
    
    /* Right channel (LRCK high) - same sample for mono */
    gpio_put(LRCK_PIN, 1);
    for (int i = 15; i >= 0; i--) {
        gpio_put(DIN_PIN, (sample >> i) & 1);
        gpio_put(BCLK_PIN, 0);
        __asm volatile ("nop\n nop\n nop\n nop\n nop\n nop\n");
        gpio_put(BCLK_PIN, 1);
        __asm volatile ("nop\n nop\n nop\n nop\n nop\n nop\n");
    }
}

/* Timer IRQ - output samples at SAMPLE_RATE */
static void __not_in_flash_func(timer_irq)(void)
{
    hw_clear_bits(&timer_hw->intr, 1u << 0);
    timer_hw->alarm[0] = timer_hw->timerawl + (1000000 / SAMPLE_RATE);
    
    /* Generate 1kHz tone from sine table */
    uint32_t idx = (sample_count * TONE_HZ / SAMPLE_RATE) % 16;
    int16_t sample = sine_table[idx];
    
    i2s_output(sample);
    sample_count++;
}

int main()
{
    stdio_init_all();
    
    printf("DAC Test - 1kHz sine wave\n");
    printf("Wiring: GPIO%d=BCK, GPIO%d=LRCK, GPIO%d=DIN\n", BCLK_PIN, LRCK_PIN, DIN_PIN);
    
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
    
    /* Setup timer */
    hw_set_bits(&timer_hw->inte, 1u << 0);
    irq_set_exclusive_handler(TIMER0_IRQ_0, timer_irq);
    irq_set_enabled(TIMER0_IRQ_0, true);
    
    /* Start timer */
    uint64_t target = timer_hw->timerawl + (1000000 / SAMPLE_RATE);
    timer_hw->alarm[0] = (uint32_t)target;
    
    printf("Tone output started...\n");
    
    /* Main loop - just blink LED */
    while (true) {
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(500);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        sleep_ms(500);
    }
    
    return 0;
}
