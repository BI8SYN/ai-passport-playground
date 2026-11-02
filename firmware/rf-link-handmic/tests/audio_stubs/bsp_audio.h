#pragma once
#include "esp_err.h"
#include <stddef.h>
esp_err_t bsp_audio_init(void);
esp_err_t bsp_audio_set_format(int,int,int);
esp_err_t bsp_audio_read(void*,size_t);
esp_err_t bsp_audio_write(const void*,size_t);
void bsp_audio_set_volume(unsigned);
