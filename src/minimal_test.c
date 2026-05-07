/* minimal_test.c - Minimal GPIO toggle to verify scope connection
 * 
 * This toggles GPIO 22 at a visible rate with 50% duty cycle.
 * If this doesn't show on scope, check probe/ground connection.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define TEST_PIN 22

int main() {
    // Init pin
    gpio_init(TEST_PIN);
    gpio_set_dir(TEST_PIN, GPIO_OUT);
    
    // Fast toggle - should see ~2.5kHz square wave
    while (1) {
        gpio_put(TEST_PIN, 1);
        for (volatile int i = 0; i < 100; i++);
        gpio_put(TEST_PIN, 0);
        for (volatile int i = 0; i < 100; i++);
    }
}
