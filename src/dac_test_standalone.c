/* dac_test_standalone.c - Use elehobica library compiled directly
 * Compiles audio_i2s.c directly into this test
 */

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

// Define pins BEFORE including library
#define PICO_AUDIO_I2S_DATA_PIN 8
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 6
#define PICO_AUDIO_I2S_PIO 1  // Use PIO1 to avoid video conflict
#define PICO_AUDIO_I2S_DMA_IRQ 0

// Include the library source directly
#include "audio_i2s.c"  // This will compile the library inline

#define SINE_WAVE_TABLE_LEN 2048
#define SAMPLES_PER_BUFFER 1156

static const uint32_t PIN_DCDC_PSM_CTRL = 23;

audio_buffer_pool_t *ap;
static bool decode_flg = false;
static constexpr int32_t DAC_ZERO = 1;

static audio_format_t audio_format = {
    .sample_freq = 44100,
    .pcm_format = AUDIO_PCM_FORMAT_S32,
    .channel_count = AUDIO_CHANNEL_STEREO
};

static audio_buffer_format_t producer_format = {
    .format = &audio_format,
    .sample_stride = 8
};

static audio_i2s_config_t i2s_config = {
    .data_pin = PICO_AUDIO_I2S_DATA_PIN,
    .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
    .dma_channel0 = 2,  // Use 2,3 instead of 0,1
    .dma_channel1 = 3,
    .pio_sm = 0
};

static int16_t sine_wave_table[SINE_WAVE_TABLE_LEN];
uint32_t step0 = 0x200000;
uint32_t pos0 = 0;
const uint32_t pos_max = 0x10000 * SINE_WAVE_TABLE_LEN;
uint vol = 20;

audio_buffer_pool_t *i2s_audio_init(uint32_t sample_freq)
{
    audio_format.sample_freq = sample_freq;

    audio_buffer_pool_t *producer_pool = audio_new_producer_pool(&producer_format, 3, SAMPLES_PER_BUFFER);
    ap = producer_pool;

    bool __unused ok;
    const audio_format_t *output_format;

    output_format = audio_i2s_setup(&audio_format, &audio_format, &i2s_config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(producer_pool);
    assert(ok);
    { // initial buffer data
        audio_buffer_t *ab = take_audio_buffer(producer_pool, true);
        int32_t *samples = (int32_t *) ab->buffer->bytes;
        for (uint i = 0; i < ab->max_sample_count; i++) {
            samples[i*2+0] = DAC_ZERO;
            samples[i*2+1] = DAC_ZERO;
        }
        ab->sample_count = ab->max_sample_count;
        give_audio_buffer(producer_pool, ab);
    }
    audio_i2s_set_enabled(true);

    decode_flg = true;
    return producer_pool;
}

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("\n\n========================================\n");
    printf("I2S - Using elehobica library directly\n");
    printf("========================================\n");
    printf("Pins: data=%d, clock_base=%d\n", PICO_AUDIO_I2S_DATA_PIN, PICO_AUDIO_I2S_CLOCK_PIN_BASE);

    // Set PLL_USB 96MHz
    pll_init(pll_usb, 1, 1536 * MHZ, 4, 4);
    clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 96 * MHZ, 48 * MHZ);
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                   CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 96 * MHZ, 96 * MHZ);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, 96 * MHZ, 96 * MHZ);
    stdio_init_all();
    sleep_ms(100);

    // DCDC PSM control
    gpio_init(PIN_DCDC_PSM_CTRL);
    gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, 1);

    for (int i = 0; i < SINE_WAVE_TABLE_LEN; i++) {
        sine_wave_table[i] = 32767 * cosf(i * 2 * (float) (M_PI / SINE_WAVE_TABLE_LEN));
    }

    printf("Initializing...\n");
    ap = i2s_audio_init(44100);
    printf("Audio started!\n\n");

    while (true) {
        int c = getchar_timeout_us(0);
        if (c >= 0) {
            if (c == '-' && vol) vol--;
            if ((c == '=' || c == '+') && vol < 256) vol++;
            if (c == 'q') break;
            printf("vol = %d      \r", (int)vol);
        }
    }
    puts("\n");
    return 0;
}

void decode()
{
    audio_buffer_t *buffer = take_audio_buffer(ap, false);
    if (buffer == NULL) { return; }
    int32_t *samples = (int32_t *) buffer->buffer->bytes;
    for (uint i = 0; i < buffer->max_sample_count; i++) {
        int32_t value0 = (vol * sine_wave_table[pos0 >> 16u]) << 8u;
        samples[i*2+0] = value0 + (value0 >> 16u);
        samples[i*2+1] = value0 + (value0 >> 16u);
        pos0 += step0;
        if (pos0 >= pos_max) pos0 -= pos_max;
    }
    buffer->sample_count = buffer->max_sample_count;
    give_audio_buffer(ap, buffer);
}

// Callback for library
void i2s_callback_func()
{
    if (decode_flg) {
        decode();
    }
}
