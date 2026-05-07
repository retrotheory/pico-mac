/* gpio_test.c - Simple GPIO toggle test for audio pin
 * 
 * This is a minimal test that just toggles the audio GPIO at ~1kHz
 * to verify scope connection and hardware.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hw.h"

int main() {
    stdio_init_all();
    
    // Configure audio pin as output
    gpio_init(GPIO_AUDIO_PIN);
    gpio_set_dir(GPIO_AUDIO_PIN, GPIO_OUT);
    
    printf("GPIO Test: Toggling pin %d at ~1kHz\n", GPIO_AUDIO_PIN);
    
    while (1) {
        gpio_put(GPIO_AUDIO_PIN, 1);
        sleep_us(500);  // 500us high
        gpio_put(GPIO_AUDIO_PIN, 0);
        sleep_us(500);  // 500us low
        // Period = 1ms = 1kHz
    }
}
