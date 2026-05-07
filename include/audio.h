/* audio.h - Picomac Audio Driver
 *
 * Audio output driver interface for Macintosh 128K/Plus emulation on RP2350.
 * Multiple implementations: PWM (GPIO 15), I2S DAC (PCM5102), scanline-sync PWM
 *
 * Copyright 2024
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>

/* Mac Sound System Constants */
#define MAC_AUDIO_SAMPLE_RATE       22254       /* Hz - horizontal blanking rate */
#define MAC_AUDIO_BUFFER_SIZE       370         /* Words (740 bytes) */
#define MAC_AUDIO_SAMPLES_PER_FRAME 370         /* One per scanline */

/* Sound buffer offsets from end of RAM (in WORDS) */
/* The 68000 uses 16-bit words. Sound buffer offsets are word counts, not bytes.
 * Main buffer is 0x180 words from end = 0x300 bytes
 * Alt buffer is 0x2F80 words from end = 0x5F00 bytes
 */
#define MAC_MAIN_SND_BUF_OFFSET     0x0180      /* Main buffer: 0x180 words from end */
#define MAC_ALT_SND_BUF_OFFSET      0x2F80      /* Alt buffer: 0x2F80 words from end */

/* VIA Port B bit definitions */
#define VIA_PB_SNDENB               7           /* 0 = sound enabled, 1 = disabled */
#define VIA_PB_SNDRES               7           /* 0 = reset PWM counters (silence) */

/* VIA Port A bit definitions */
#define VIA_PA_SNDVOL_MASK          0x07        /* Bits 0-2: volume (0-7) */
#define VIA_PA_SNDPG2               3           /* 0 = alternate buffer, 1 = main */

/* Audio state structure */
typedef struct {
    uint8_t *ram_base;              /* Base of emulated Mac RAM */
    uint32_t ram_size;              /* Size of emulated RAM */
    
    /* Current sound buffer pointer (calculated each frame) */
    volatile uint8_t *sound_buffer;
    
    /* VIA state tracking */
    volatile uint8_t via_port_a;    /* Cached VIA Port A value */
    volatile uint8_t via_port_b;    /* Cached VIA Port B value */
    
    /* Audio control state */
    volatile bool sound_enabled;    /* Based on VIA PB7 */
    volatile bool pwm_reset;        /* /SNDRES active (PB7 low) */
    volatile bool main_buffer;      /* true = main, false = alternate */
    volatile uint8_t volume;        /* 0-7 volume level from VIA */
    
    /* Current sample index (0-369, advances with each scanline) */
    volatile uint16_t sample_index;
} audio_state_t;

/* Initialize audio subsystem */
void audio_init(uint8_t *mac_ram, uint32_t ram_size);

/* Enable/disable audio output */
void audio_enable(bool enable);

/* Called each horizontal blanking interval (scanline) */
void audio_hblank_callback(void);

/* VIA integration - call when VIA Port A is written */
void audio_via_port_a_write(uint8_t data);

/* VIA integration - call when VIA Port B is written */
void audio_via_port_b_write(uint8_t data);

/* Get current audio state (for debugging) */
const audio_state_t *audio_get_state(void);

/* Main loop hook - call at approximately 60.15 Hz (frame rate) */
void audio_frame_update(void);

#endif /* AUDIO_H */
