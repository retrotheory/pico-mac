/* minimal_pwm_strong.c - PWM with max drive strength
 * 
 * Same as minimal_pwm but with 12mA drive strength and direct GPIO toggle
 * to verify the pin can drive full 3.3V
 */

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#define TEST_PIN 22
#define PWM_SLICE  3
#define PWM_CHAN   PWM_CHAN_B

int main() {
    stdio_init_all();
    
    printf("Strong PWM test on GPIO %d\n", TEST_PIN);
    
    /* First test: Direct GPIO toggle to verify full voltage swing */
    printf("Phase 1: Direct GPIO toggle for 2 seconds...\n");
    gpio_init(TEST_PIN);
    gpio_set_dir(TEST_PIN, GPIO_OUT);
    
    /* Set max drive strength (12mA) */
    gpio_set_drive_strength(TEST_PIN, GPIO_DRIVE_STRENGTH_12MA);
    
    /* Toggle at 1kHz for 2 seconds */
    for (int i = 0; i < 2000; i++) {
        gpio_put(TEST_PIN, 1);
        sleep_us(500);
        gpio_put(TEST_PIN, 0);
        sleep_us(500);
    }
    
    printf("Phase 2: PWM output...\n");
    
    /* Now switch to PWM */
    gpio_set_function(TEST_PIN, GPIO_FUNC_PWM);
    
    pwm_config cfg = pwm_get_default_config();
    float div = (float)clock_get_hz(clk_sys) / (1000.0f * 256.0f);
    pwm_config_set_clkdiv(&cfg, div);
    pwm_config_set_wrap(&cfg, 255);
    
    pwm_init(PWM_SLICE, &cfg, false);
    pwm_set_chan_level(PWM_SLICE, PWM_CHAN, 128);  /* 50% duty */
    pwm_set_enabled(PWM_SLICE, true);
    
    printf("PWM started with 12mA drive strength\n");
    printf("You should now see full 3.3V swing on GPIO %d\n", TEST_PIN);
    
    while (1) {
        tight_loop_contents();
    }
}
