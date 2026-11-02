#pragma once
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
int xSemaphoreTake(SemaphoreHandle_t,TickType_t);
void xSemaphoreGive(SemaphoreHandle_t);
