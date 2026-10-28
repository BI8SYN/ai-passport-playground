#include "resistor_render.h"
#include <assert.h>
#include <string.h>

extern const uint8_t rr_textures[] __asm__("_binary_resistor_textures_bin_start");
#define BODY_BYTES (RR_IMAGE_WIDTH * RR_IMAGE_HEIGHT * sizeof(uint16_t))
#define TILE_BYTES (RR_TILE_WIDTH * RR_TILE_HEIGHT * sizeof(uint16_t))
static void paint_ring(uint16_t *pixels, const rr_state_t *s, unsigned ring) {
    int x = rr_ring_x(s->bands, ring), tile = 0;
    while (tile < RR_TILE_COUNT && rr_tile_centers[tile] != x) tile++;
    assert(tile < RR_TILE_COUNT);
    const uint8_t *source = rr_textures + 2 * BODY_BYTES + (tile * 12 + rr_color(s, ring)) * TILE_BYTES;
    for (unsigned y = RR_TILE_Y; y < RR_TILE_Y + RR_TILE_HEIGHT; y++) {
        memcpy(pixels + y * RR_IMAGE_WIDTH + x - 5, source, RR_TILE_WIDTH * sizeof(uint16_t));
        source += RR_TILE_WIDTH * sizeof(uint16_t);
    }
}
void rr_render_resistor(uint16_t *pixels, const rr_state_t *s) {
    assert(rr_valid(s));
    memcpy(pixels, rr_textures + (s->bands == 4 ? 0 : BODY_BYTES), BODY_BYTES);
    for (unsigned ring = 1; ring <= s->bands; ring++) paint_ring(pixels, s, ring);
}
bool rr_render_update(uint16_t *pixels, const rr_state_t *old, const rr_state_t *s, rr_rect_t *damage) {
    if (!old || old->bands != s->bands) {
        rr_render_resistor(pixels, s);
        *damage = (rr_rect_t){0, 0, RR_IMAGE_WIDTH - 1, RR_IMAGE_HEIGHT - 1};
        return true;
    }
    bool changed = false;
    for (unsigned ring = 1; ring <= s->bands; ring++) {
        if (rr_color(old, ring) == rr_color(s, ring)) continue;
        paint_ring(pixels, s, ring);
        int x = rr_ring_x(s->bands, ring);
        if (!changed) *damage = (rr_rect_t){x - 5, RR_TILE_Y, x + 5, RR_TILE_Y + RR_TILE_HEIGHT - 1};
        else {
            if (x - 5 < damage->x1) damage->x1 = x - 5;
            if (x + 5 > damage->x2) damage->x2 = x + 5;
        }
        changed = true;
    }
    return changed;
}
