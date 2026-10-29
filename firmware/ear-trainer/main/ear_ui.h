#pragma once
#include "ear_controller.h"
#include "lvgl.h"
LV_FONT_DECLARE(ear_font_16);
void et_ui_create(void);
void et_ui_refresh(const et_app_t *a,int64_t now);
