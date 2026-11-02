#pragma once
#include <stdint.h>
#define HM_SPECTRUM_SAMPLES 256
#define HM_SPECTRUM_BANDS 16
typedef struct {
    float window[256], twiddle_re[128], twiddle_im[128], re[256], im[256];
    uint8_t levels[16], peaks[16], hold[16];
} hm_spectrum_t;
/* One owner, no heap. Sample rate is fixed 8k. Init precomputes trig tables. */
void hm_spectrum_init(hm_spectrum_t *);
void hm_spectrum_reset(hm_spectrum_t *);
void hm_spectrum_analyze(hm_spectrum_t *,const int16_t pcm[256],uint8_t target[16]);
void hm_spectrum_step(hm_spectrum_t *,const uint8_t target[16],uint8_t levels[16],uint8_t peaks[16]);
