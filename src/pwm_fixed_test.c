/* pwm_fixed_test.c - Simplest possible PWM test on GPIO 2
 * 
 * This sets up PWM on GPIO 2 and toggles the duty cycle slowly
 * so you can see it on a multimeter or scope.
 */

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#define TEST_PIN 2
#define PWM_SLICE 1
#define PWM_CHAN PWM_CHAN_A

int main() {
    stdio_init_all();
    
    printf("PWM Fixed Test on GPIO %d\n", TEST_PIN);
    
    /* Configure GPIO for PWM */
    gpio_set_function(TEST_PIN, GPIO_FUNC_PWM);
    
    /* Set up PWM at 1kHz frequency */
    pwm_config cfg = pwm_get_default_config();
    float div = (float)clock_get_hz(clk_sys) / (1000.0f * 256.0f);
    pwm_config_set_clkdiv(&cfg, div);
    pwm_config_set_wrap(&cfg, 255);
    
    pwm_init(PWM_SLICE, &cfg, false);
    
    /* Start PWM */
    pwm_set_enabled(PWM_SLICE, true);
    
    printf("PWM started on slice %d, channel %s\n", PWM_SLICE, 
           (PWM_CHAN == PWM_CHAN_A) ? "A" : "B");
    printf("Toggling duty cycle: 25%% -> 75%% -> 25%%...\n");
    printf("You should see voltage toggle between ~0.8V and ~2.5V\n");
    
    /* Slowly toggle duty cycle */
    while (1) {
        /* 25% duty (64/256) - should read ~0.8V average */
        pwm_set_chan_level(PWM_SLICE, PWM_CHAN, 64);
        sleep_ms(1000);
        
        /* 75% duty (192/256) - should read ~2.5V average */
        pwm_set_chan_level(PWM_SLICE, PWM_CHAN, 192);
        sleep_ms(1000);
    }
}
