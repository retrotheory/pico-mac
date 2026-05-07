/* led_and_pin_test.c - Verify code runs and test multiple pins
 * 
 * 1. Flashes the onboard LED (pin 25) so you know code is running
 * 2. Slowly toggles GPIO 22 (1Hz) so you can measure with multimeter
 * 3. Also tries GPIO 15 as alternative test pin
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define LED_PIN     25  /* Onboard LED */
#define TEST_PIN_1  22  /* Original audio pin */
#define TEST_PIN_2  15  /* Alternative pin */

int main() {
    stdio_init_all();
    
    /* Init LED */
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);  /* LED ON */
    
    /* Init test pins */
    gpio_init(TEST_PIN_1);
    gpio_set_dir(TEST_PIN_1, GPIO_OUT);
    gpio_set_drive_strength(TEST_PIN_1, GPIO_DRIVE_STRENGTH_12MA);
    
    gpio_init(TEST_PIN_2);
    gpio_set_dir(TEST_PIN_2, GPIO_OUT);
    gpio_set_drive_strength(TEST_PIN_2, GPIO_DRIVE_STRENGTH_12MA);
    
    printf("LED and Pin Test\n");
    printf("LED should be blinking\n");
    printf("GPIO 22 and 15 toggling at 1Hz\n");
    printf("Use multimeter to measure DC voltage (should be ~1.6V)\n");
    
    int state = 0;
    
    while (1) {
        /* Toggle all pins */
        gpio_put(LED_PIN, state);
        gpio_put(TEST_PIN_1, state);
        gpio_put(TEST_PIN_2, state);
        
        state = !state;
        sleep_ms(500);  /* 1Hz toggle */
    }
}
