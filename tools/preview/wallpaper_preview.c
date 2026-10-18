/* Host preview: render the procedural wallpaper as a binary PPM on stdout.
 * Usage: wallpaper_preview W H [l|d]. Created by Jonas Lee. */
#include "ui/wallpaper.h"
extern void *malloc(unsigned long);
extern long write(int, const void *, unsigned long);
extern int atoi(const char *);
int main(int argc, char **argv)
{
    int w = argc > 2 ? atoi(argv[1]) : 1440, h = argc > 2 ? atoi(argv[2]) : 900;
    bool dark = argc > 3 && argv[3][0] == 'd';
    uint32_t *px = malloc((unsigned long)w * h * 4);
    uint8_t *rgb = malloc((unsigned long)w * h * 3);
    wallpaper_render_tahoe(px, w, h, dark);
    for (int i = 0; i < w * h; i++) { rgb[i * 3] = px[i] >> 16; rgb[i * 3 + 1] = px[i] >> 8; rgb[i * 3 + 2] = px[i]; }
    char hdr[48]; int n = 0;
    const char *p = "P6\n"; while (*p) hdr[n++] = *p++;
    int v[2] = { w, h };
    for (int k = 0; k < 2; k++) { char t[12]; int m = 0, x = v[k]; do { t[m++] = '0' + x % 10; x /= 10; } while (x); while (m) hdr[n++] = t[--m]; hdr[n++] = k ? '\n' : ' '; }
    p = "255\n"; while (*p) hdr[n++] = *p++;
    write(1, hdr, n); write(1, rgb, (unsigned long)w * h * 3);
    return 0;
}
