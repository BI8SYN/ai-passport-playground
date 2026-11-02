#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HM_ADPCM_SAMPLES 160
#define HM_ADPCM_BYTES 84
/* Independent mono IMA blocks: LE predictor, index, reserved, low nibble first.
 * The predictor is sample 0; 159 codes follow, final high nibble must be zero. */
void hm_adpcm_encode(const int16_t pcm[HM_ADPCM_SAMPLES], uint8_t block[HM_ADPCM_BYTES]);
bool hm_adpcm_valid(const uint8_t *block, size_t length);
bool hm_adpcm_decode(const uint8_t *block, size_t length, int16_t pcm[HM_ADPCM_SAMPLES]);
