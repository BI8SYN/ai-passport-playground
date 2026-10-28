#pragma once
#include "resistor_model.h"
#define RR_IMAGE_WIDTH 240
#define RR_IMAGE_HEIGHT 76
/* RGB565 image stored outside LVGL's heap; 36,480 bytes. */
void rr_render_resistor(uint16_t *pixels, const rr_state_t *state);
int rr_ring_x(unsigned bands, unsigned ring);
#define RR_TILE_COUNT 12
#define RR_TILE_WIDTH 11
#define RR_TILE_Y 10
#define RR_TILE_HEIGHT 53
extern const int rr_tile_centers[RR_TILE_COUNT];
typedef struct { int x1, y1, x2, y2; } rr_rect_t;
bool rr_render_update(uint16_t *pixels, const rr_state_t *old, const rr_state_t *state, rr_rect_t *damage);
