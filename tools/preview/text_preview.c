/* Host preview of the UI fonts: PPM on stdout. Usage: text_preview [1|2]. Created by Jonas Lee. */
#include "uifont.h"
extern void *malloc(unsigned long);
extern long write(int, const void *, unsigned long);
extern int atoi(const char *);
int main(int argc, char **argv)
{
    int d = argc > 1 ? atoi(argv[1]) : 1;
    int W = 560 * d, H = 470 * d;
    uint32_t *px = malloc((unsigned long)W * H * 4);
    GfxSurface s = { px, W, H, W, 0, 0, W, H };
    for (int i = 0; i < W * H; i++) px[i] = 0xDCEBF6;
    for (int y = H / 2; y < H; y++) for (int x = 0; x < W; x++) px[y * W + x] = 0x1B2B44;
    text_set_density(d);
    const char *sample = "Finder  File  Edit  View  Window \xE2\x80\x94 ojjyOS by Jonas Lee \xE2\x80\xA6";
    static const TextStyle st[] = { TEXT_SMALL, TEXT_BODY, TEXT_BODY_MEDIUM, TEXT_BODY_BOLD, TEXT_TITLE, TEXT_HEADLINE };
    for (int half = 0; half < 2; half++) {
        int y = 10 + half * 235;
        uint32_t c = half ? 0xF2F6FA : 0x1A2230;
        for (unsigned i = 0; i < sizeof(st) / sizeof(st[0]); i++) { text_draw(&s, 12, y, sample, st[i], c); y += text_line_height(st[i]) + 4; }
        text_draw(&s, 12, y, "Large title 28", TEXT_LARGE, c); y += text_line_height(TEXT_LARGE);
        text_draw(&s, 300, y - 80, "10:42", TEXT_DISPLAY, c);
        text_draw_fit(&s, 12, y + 4, "A long file name that will not fit in the column.txt", TEXT_BODY, c, 220);
    }
    uint8_t *rgb = malloc((unsigned long)W * H * 3);
    for (int i = 0; i < W * H; i++) { rgb[i * 3] = px[i] >> 16; rgb[i * 3 + 1] = px[i] >> 8; rgb[i * 3 + 2] = px[i]; }
    char hdr[40]; int n = 0; const char *p = "P6\n"; while (*p) hdr[n++] = *p++;
    int v[2] = { W, H };
    for (int k = 0; k < 2; k++) { char t[12]; int m = 0, x = v[k]; do { t[m++] = '0' + x % 10; x /= 10; } while (x); while (m) hdr[n++] = t[--m]; hdr[n++] = k ? '\n' : ' '; }
    p = "255\n"; while (*p) hdr[n++] = *p++;
    write(1, hdr, n); write(1, rgb, (unsigned long)W * H * 3);
    return 0;
}
