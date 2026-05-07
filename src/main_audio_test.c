/* main_audio_test.c - Mac emulation with audio, NO VIDEO
 * Tests if audio works without video interference
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"

#include "audio.h"
#include "hw.h"
#include "umac.h"
#include "disc.h"

// Static RAM and ROM buffers
static uint8_t mac_ram[464 * 1024];  // 464K Mac

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);
    
    printf("\n\n========================================\n");
    printf("Mac Plus Audio Test (NO VIDEO)\n");
    printf("========================================\n\n");
    
    // Init umac
    disc_descr_t discs[DISC_NUM_DRIVES] = {0};
    if (umac_init(mac_ram, NULL, discs) < 0) {
        printf("Failed to init umac!\n");
        return 1;
    }
    
    printf("Mac RAM: 464K\n");
    
    // Init audio (I2S version)
    audio_init(mac_ram, 464 * 1024);
    audio_enable(true);
    
    printf("\nMac emulation starting...\n");
    printf("You should hear the boot chime\n\n");
    
    // Run Mac emulation main loop
    umac_loop();
    
    return 0;
}
