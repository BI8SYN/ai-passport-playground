#include "resistor_render.h"
#include <math.h>
#include <stdlib.h>

static const unsigned pigment[] = {
    0x171719, 0x784126, 0xD92E27, 0xED7B19, 0xEBC336, 0x28784A,
    0x245CB5, 0x8242A5, 0x858783, 0xE4E2D6, 0xBA963E, 0xB4BEC4
};
static float clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static uint16_t rgb(unsigned color, float light, float reflection) {
    unsigned r = (unsigned)clamp(((color >> 16) & 255) * light + reflection, 0, 255);
    unsigned g = (unsigned)clamp(((color >> 8) & 255) * light + reflection, 0, 255);
    unsigned b = (unsigned)clamp((color & 255) * light + reflection, 0, 255);
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}
/* Lathed coating profile: rounded shoulders, narrower barrel, rounded ends. */
static float radius_at(int x) {
    if (x < 28 || x > 212) return 0;
    if (x < 43) { float t = (x - 43) / 15.f; return 26 * sqrtf(1 - t * t); }
    if (x > 197) { float t = (x - 197) / 15.f; return 26 * sqrtf(1 - t * t); }
    float t = x < 65 ? (x - 47) / 18.f : (193 - x) / 18.f;
    t = clamp(t, 0, 1);
    return 26 - 5 * t * t * (3 - 2 * t);
}
void rr_art_render(uint16_t *pixels, const rr_state_t *s, bool paint_bands) {
    const float cy = 36;
    for (int x = 0; x < RR_IMAGE_WIDTH; x++) {
        float radius = radius_at(x);
        int ring = -1;
        for (unsigned i = 1; paint_bands && i <= s->bands; i++)
            if (abs(x - rr_ring_x(s->bands, i)) <= 5) { ring = (int)i; break; }
        unsigned color = ring > 0 ? pigment[rr_color(s, (unsigned)ring)] : s->bands == 4 ? 0xCEB78A : 0x99B9C8;
        bool metal = ring > 0 && rr_color(s, (unsigned)ring) >= RR_GOLD;
        for (int y = 0; y < RR_IMAGE_HEIGHT; y++) {
            uint16_t pixel = rgb(0x111A24, 1, 0);
            float dy = y - cy;
            /* A soft contact shadow makes the component sit above the background. */
            if (radius > 0 && fabsf(dy - 7) < radius + 5) {
                float edge = clamp((radius + 5 - fabsf(dy - 7)) / 9, 0, 1);
                pixel = rgb(0x111A24, 1 - .5f * edge, 0);
            }
            if (x >= 9 && x <= 230) {
                float bend = x < 28 ? (28 - x) * -.035f : x > 212 ? (x - 212) * .055f : 0;
                float wire = dy - bend;
                if (fabsf(wire) <= 2.2f) {
                    float reflection = 35 + 165 * clamp(1 - fabsf(wire + .65f) / 2.5f, 0, 1);
                    pixel = rgb(0xA5AAB0, .3f, reflection);
                }
            }
            if (radius > 0 && fabsf(dy) <= radius) {
                float v = dy / radius, z = sqrtf(fmaxf(0, 1 - v * v));
                float lambert = fmaxf(0, .86f * z - .5f * v);
                float shine = fmaxf(0, .955f * z - .30f * v);
                /* Integer powers avoid costly libm pow on the C3 soft-float CPU. */
                for (unsigned power = 0; power < 5; power++) shine *= shine;
                if (metal) shine *= shine;
                float light = .34f + .68f * lambert;
                /* End faces and pigment edges retain the same cylindrical lighting. */
                if (x > 198) light *= .88f;
                float grain = (((x * 17 + y * 31) % 7) - 3) * .55f;
                pixel = rgb(color, light, shine * (metal ? 65 : ring > 0 ? 9 : 12) + grain);
                float coverage = clamp(radius - fabsf(dy) + .5f, 0, 1);
                if (coverage < 1) {
                    uint16_t background = rgb(0x111A24, 1, 0);
                    unsigned r = (unsigned)(((pixel >> 11) & 31) * coverage + ((background >> 11) & 31) * (1 - coverage));
                    unsigned g = (unsigned)(((pixel >> 5) & 63) * coverage + ((background >> 5) & 63) * (1 - coverage));
                    unsigned b = (unsigned)((pixel & 31) * coverage + (background & 31) * (1 - coverage));
                    pixel = (uint16_t)(r << 11 | g << 5 | b);
                }
            }
            pixels[y * RR_IMAGE_WIDTH + x] = pixel;
        }
    }
}
