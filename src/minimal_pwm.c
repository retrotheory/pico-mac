/* minimal_pwm.c - Absolute minimal PWM test
 * This just sets up PWM on GPIO 22 at 1kHz with 50% duty cycle
 * No DMA, no complex setup - just raw PWM output
 */

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#define TEST_PIN 22
#define PWM_SLICE  3  /* GPIO 22 = PWM3 B */
#define PWM_CHAN   PWM_CHAN_B

int main() {
    stdio_init_all();
    
    printf("Minimal PWM test starting on GPIO %d\n", TEST_PIN);
    
    /* Configure GPIO for PWM */
    gpio_set_function(TEST_PIN, GPIO_FUNC_PWM);
    
    /* Set up PWM config */
    pwm_config cfg = pwm_get_default_config();
    
    /* Run at 1kHz with 8-bit resolution */
    /* Wrap = 255, so frequency = clock / (div * 256) */
    /* For 1kHz at 250MHz: div = 250000000 / (1000 * 256) = 976.5 */
    float div = (float)clock_get_hz(clk_sys) / (1000.0f * 256.0f);
    pwm_config_set_clkdiv(&cfg, div);
    pwm_config_set_wrap(&cfg, 255);
    
    /* Initialize but don't start yet */
    pwm_init(PWM_SLICE, &cfg, false);
    
    /* Set 50% duty cycle (128 out of 256) */
    pwm_set_chan_level(PWM_SLICE, PWM_CHAN, 128);
    
    /* Enable PWM */
    pwm_set_enabled(PWM_SLICE, true);
    
    printf("PWM started: slice=%d, chan=%d, div=%.1f\n", PWM_SLICE, PWM_CHAN, div);
    printf("You should see ~1kHz square wave on GPIO %d\n", TEST_PIN);
    
    /* Stay alive */
    while (1) {
        tight_loop_contents();
    }
}
