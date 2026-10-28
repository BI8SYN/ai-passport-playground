#include "resistor_render.h"
#include <assert.h>
#include <stdio.h>
void rr_art_render(uint16_t *, const rr_state_t *, bool);
static uint16_t pixels[RR_IMAGE_WIDTH * RR_IMAGE_HEIGHT];
static void word(FILE *file, uint16_t value) { fputc(value & 255, file); fputc(value >> 8, file); }
int main(int argc, char **argv) {
    assert(argc == 2); FILE *out = fopen(argv[1], "wb"); assert(out);
    rr_state_t s; rr_init(&s);
    for (unsigned count = 4; count <= 5; count++) {
        s.bands = count; rr_art_render(pixels, &s, false);
        for (unsigned i = 0; i < RR_IMAGE_WIDTH * RR_IMAGE_HEIGHT; i++) word(out, pixels[i]);
    }
    for (unsigned tile = 0; tile < RR_TILE_COUNT; tile++) {
        unsigned bands = 0;
        for (unsigned count = 4; count <= 6 && !bands; count++)
            for (unsigned ring = 1; ring <= count; ring++)
                if (rr_ring_x(count, ring) == rr_tile_centers[tile]) { bands = count; break; }
        assert(bands);
        for (unsigned c = RR_BLACK; c <= RR_SILVER; c++) {
            s.bands = bands;
            s.digits[0] = s.digits[1] = s.digits[2] = s.multiplier = s.tolerance = s.tcr = (rr_color_t)c;
            rr_art_render(pixels, &s, true);
            for (int y = RR_TILE_Y; y < RR_TILE_Y + RR_TILE_HEIGHT; y++)
                for (int x = rr_tile_centers[tile] - 5; x <= rr_tile_centers[tile] + 5; x++)
                    word(out, pixels[y * RR_IMAGE_WIDTH + x]);
        }
    }
    assert(!ferror(out)); assert(!fclose(out));
}
