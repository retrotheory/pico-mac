/* pwm_timer_test.c - Combine working PWM with timer (no Mac emu)
 * 
 * This is exactly like pwm_fixed_test but uses timer to update duty cycle
 * to verify timer+PWM combination works
 */

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/time.h"

#define TEST_PIN 2
#define PWM_SLICE 1
#define PWM_CHAN PWM_CHAN_A

static uint8_t samples[] = {0x20, 0xE0};  /* Low, High */
static int idx = 0;

static bool timer_callback(struct repeating_timer *t) {
    /* Output sample */
    pwm_set_chan_level(PWM_SLICE, PWM_CHAN, samples[idx]);
    idx = (idx + 1) % 2;
    return true;
}

int main() {
    stdio_init_all();
    
    printf("PWM Timer Test on GPIO %d\n", TEST_PIN);
    
    /* Set up PWM exactly like working test */
    gpio_set_function(TEST_PIN, GPIO_FUNC_PWM);
    
    pwm_config cfg = pwm_get_default_config();
    float div = (float)clock_get_hz(clk_sys) / (1000.0f * 256.0f);
    pwm_config_set_clkdiv(&cfg, div);
    pwm_config_set_wrap(&cfg, 255);
    
    pwm_init(PWM_SLICE, &cfg, false);
    pwm_set_chan_level(PWM_SLICE, PWM_CHAN, 128);  /* Start at midpoint */
    pwm_set_enabled(PWM_SLICE, true);
    
    printf("PWM enabled, starting timer at 1kHz...\n");
    
    /* Set up timer at 1kHz to alternate samples */
    struct repeating_timer timer;
    add_repeating_timer_us(-1000, timer_callback, NULL, &timer);
    
    while (1) {
        tight_loop_contents();
    }
}
