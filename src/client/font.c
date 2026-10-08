/* Tiny 3x5 pixel font for on-screen labels and stats (no font library needed).
 * Each glyph is 5 rows of 3 bits; lowercase is drawn as uppercase. */
#include "client.h"

#include <ctype.h>

#define G(a, b, c, d, e) (((a) << 12) | ((b) << 9) | ((c) << 6) | ((d) << 3) | (e))

static unsigned glyph(char ch) {
    switch (toupper((unsigned char)ch)) {
    case '0': return G(7, 5, 5, 5, 7); case '1': return G(2, 6, 2, 2, 7);
    case '2': return G(7, 1, 7, 4, 7); case '3': return G(7, 1, 7, 1, 7);
    case '4': return G(5, 5, 7, 1, 1); case '5': return G(7, 4, 7, 1, 7);
    case '6': return G(7, 4, 7, 5, 7); case '7': return G(7, 1, 2, 2, 2);
    case '8': return G(7, 5, 7, 5, 7); case '9': return G(7, 5, 7, 1, 7);
    case 'A': return G(2, 5, 7, 5, 5); case 'B': return G(6, 5, 6, 5, 6);
    case 'C': return G(3, 4, 4, 4, 3); case 'D': return G(6, 5, 5, 5, 6);
    case 'E': return G(7, 4, 6, 4, 7); case 'F': return G(7, 4, 6, 4, 4);
    case 'G': return G(3, 4, 5, 5, 3); case 'H': return G(5, 5, 7, 5, 5);
    case 'I': return G(7, 2, 2, 2, 7); case 'J': return G(1, 1, 1, 5, 2);
    case 'K': return G(5, 5, 6, 5, 5); case 'L': return G(4, 4, 4, 4, 7);
    case 'M': return G(5, 7, 7, 5, 5); case 'N': return G(6, 5, 5, 5, 5);
    case 'O': return G(2, 5, 5, 5, 2); case 'P': return G(6, 5, 6, 4, 4);
    case 'Q': return G(2, 5, 5, 6, 3); case 'R': return G(6, 5, 6, 5, 5);
    case 'S': return G(3, 4, 2, 1, 6); case 'T': return G(7, 2, 2, 2, 2);
    case 'U': return G(5, 5, 5, 5, 7); case 'V': return G(5, 5, 5, 5, 2);
    case 'W': return G(5, 5, 7, 7, 5); case 'X': return G(5, 5, 2, 5, 5);
    case 'Y': return G(5, 5, 2, 2, 2); case 'Z': return G(7, 1, 2, 4, 7);
    case '.': return G(0, 0, 0, 0, 2); case ':': return G(0, 2, 0, 2, 0);
    case '%': return G(5, 1, 2, 4, 5); case '(': return G(1, 2, 2, 2, 1);
    case ')': return G(4, 2, 2, 2, 4); case '-': return G(0, 0, 7, 0, 0);
    case '>': return G(4, 2, 1, 2, 4); case '_': return G(0, 0, 0, 0, 7);
    case '/': return G(1, 1, 2, 4, 4); case '!': return G(2, 2, 2, 0, 2);
    case ',': return G(0, 0, 0, 2, 4); case '+': return G(0, 2, 7, 2, 0);
    case '?': return G(7, 1, 2, 0, 2); case '=': return G(0, 7, 0, 7, 0);
    default:  return 0;
    }
}

int font_width(const char *text, int scale) {
    int n = 0;
    while (text[n]) n++;
    return n ? n * 4 * scale - scale : 0;
}

void font_draw(SDL_Renderer *r, int x, int y, int scale, const char *text) {
    for (; *text; text++, x += 4 * scale) {
        unsigned bits = glyph(*text);
        int row, col;
        for (row = 0; row < 5; row++)
            for (col = 0; col < 3; col++)
                if (bits & (1u << ((4 - row) * 3 + (2 - col)))) {
                    SDL_Rect px = {x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(r, &px);
                }
    }
}

/* Burn text into the luma plane of an I420 frame (used by the test pattern). */
void font_draw_into_i420(uint8_t *Y, int stride, int w, int h, int x, int y, int scale, const char *text) {
    for (; *text; text++, x += 4 * scale) {
        unsigned bits = glyph(*text);
        int row, col, dx, dy;
        for (row = 0; row < 5; row++)
            for (col = 0; col < 3; col++) {
                uint8_t v = (bits & (1u << ((4 - row) * 3 + (2 - col)))) ? 235 : 16;
                for (dy = 0; dy < scale; dy++)
                    for (dx = 0; dx < scale; dx++) {
                        int px = x + col * scale + dx, py = y + row * scale + dy;
                        if (px >= 0 && px < w && py >= 0 && py < h) Y[py * stride + px] = v;
                    }
            }
    }
}
