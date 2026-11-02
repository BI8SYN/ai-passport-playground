#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int,unsigned);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*);
void vTaskDelay(TickType_t);
void vTaskDelayUntil(TickType_t*,TickType_t);
TickType_t xTaskGetTickCount(void);
