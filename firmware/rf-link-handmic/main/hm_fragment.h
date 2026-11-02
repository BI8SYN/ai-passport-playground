#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HM_CONTROL_MAX 512
#define HM_FRAGMENT_HEADER 8

typedef enum { HM_FRAGMENT_MORE, HM_FRAGMENT_COMPLETE, HM_FRAGMENT_DUPLICATE, HM_FRAGMENT_BAD } hm_fragment_result_t;
typedef struct {
    uint32_t id;
    uint16_t total, used;
    uint64_t started_ms;
    bool active, complete;
    uint32_t dropped;
    uint8_t bytes[HM_CONTROL_MAX];
    uint16_t fragment_lengths[HM_CONTROL_MAX];
} hm_fragment_t;
void hm_fragment_reset(hm_fragment_t *rx);
bool hm_fragment_expire(hm_fragment_t *rx, uint64_t now_ms);
hm_fragment_result_t hm_fragment_push(hm_fragment_t *rx, const uint8_t *value, size_t len, uint64_t now_ms);
/* Returns one complete GATT value size, or zero on invalid input. */
size_t hm_fragment_encode(uint8_t *value, size_t capacity, uint32_t transfer_id, uint16_t offset, const uint8_t *json, size_t total, uint16_t value_limit);
