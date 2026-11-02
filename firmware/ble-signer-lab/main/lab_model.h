#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define LAB_TIMEOUT_MS 10000
#define LAB_MESSAGE_MAX 192
#define LAB_KEY_BYTES 32
#define LAB_SIG_BYTES 64
typedef struct {
    bool bound, candidate, pending;
    uint8_t key[32], proposed[32];
    uint32_t id, passed, rejected, timed_out;
    uint64_t started_ms, elapsed_ms;
    char message[LAB_MESSAGE_MAX];
} lab_model_t;
bool lab_offer(lab_model_t *s, const uint8_t key[32]);
bool lab_confirm(lab_model_t *s);
bool lab_request(lab_model_t *s, const char *message, uint64_t now);
bool lab_result(lab_model_t *s, uint32_t id, bool valid, uint64_t now);
bool lab_tick(lab_model_t *s, uint64_t now);
void lab_disconnect(lab_model_t *s);
void lab_clear(lab_model_t *s);
bool lab_unhex(const char *text, uint8_t *out, size_t bytes);
void lab_hex(const uint8_t *in, size_t bytes, char *out);
