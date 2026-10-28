#include "resistor_render.h"
int rr_ring_x(unsigned bands, unsigned ring) {
    static const int positions[3][6] = {{49, 78, 107, 186}, {49, 72, 95, 118, 186}, {49, 70, 91, 112, 133, 190}};
    if (bands < 4 || bands > 6 || ring < 1 || ring > bands) return -1;
    return positions[bands - 4][ring - 1];
}
const int rr_tile_centers[RR_TILE_COUNT] = {49, 70, 72, 78, 91, 95, 107, 112, 118, 133, 186, 190};
