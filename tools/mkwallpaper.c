/*
 * mkwallpaper - Create simple RGBA wallpapers for ojjyOS
 *
 * Usage: mkwallpaper <width> <height> <style> <output.raw>
 *
 * Styles: tahoe_light, tahoe_dark, gradient_blue
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static uint8_t *pixels;
static int width, height;

/* Set pixel (RGBA) */
static void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    int idx = (y * width + x) * 4;
    pixels[idx + 0] = r;
    pixels[idx + 1] = g;
    pixels[idx + 2] = b;
    pixels[idx + 3] = a;
}

/* Interpolate between two colors */
static void lerp_color(uint8_t *out, uint8_t r1, uint8_t g1, uint8_t b1,
                       uint8_t r2, uint8_t g2, uint8_t b2, float t)
{
    out[0] = (uint8_t)(r1 + (r2 - r1) * t);
    out[1] = (uint8_t)(g1 + (g2 - g1) * t);
    out[2] = (uint8_t)(b1 + (b2 - b1) * t);
}

/* Tahoe light wallpaper - creamy gradient with liquid wave */
static void create_tahoe_light(void)
{
    for (int y = 0; y < height; y++) {
        float t = (float)y / height;
        t = t * t * (3 - 2 * t);

        uint8_t top[3];
        uint8_t mid[3];
        uint8_t base[3];

        lerp_color(top, 15, 132, 204, 94, 176, 225, t);
        lerp_color(mid, 94, 176, 225, 240, 224, 202, t);
        lerp_color(base, top[0], top[1], top[2], mid[0], mid[1], mid[2], t);

        for (int x = 0; x < width; x++) {
            float wave1 = height * 0.34f + sinf(x * 0.005f) * 50.0f;
            float wave2 = height * 0.55f + sinf(x * 0.0036f + 1.4f) * 70.0f;

            float blend = 0.0f;
            float highlight = 0.0f;

            if (y > wave1) {
                blend = 0.35f;
                if (fabsf(y - wave1) < 14.0f) {
                    highlight = 0.45f;
                }
            }
            if (y > wave2) {
                blend = fmaxf(blend, 0.55f);
            }

            uint8_t r = base[0];
            uint8_t g = base[1];
            uint8_t b = base[2];

            if (blend > 0.0f) {
                uint8_t wave[3];
                lerp_color(wave, 15, 114, 197, 11, 63, 140, blend);
                r = (uint8_t)(r + (wave[0] - r) * blend);
                g = (uint8_t)(g + (wave[1] - g) * blend);
                b = (uint8_t)(b + (wave[2] - b) * blend);
            }
            if (highlight > 0.0f) {
                r = (uint8_t)(r + (132 - r) * highlight);
                g = (uint8_t)(g + (205 - g) * highlight);
                b = (uint8_t)(b + (200 - b) * highlight);
            }

            set_pixel(x, y, r, g, b, 255);
        }
    }
}

/* Tahoe dark wallpaper - deep blue with liquid highlight */
static void create_tahoe_dark(void)
{
    for (int y = 0; y < height; y++) {
        float t = (float)y / height;
        t = t * t * (3 - 2 * t);

        uint8_t base[3];
        lerp_color(base, 8, 14, 26, 14, 30, 58, t);
        if (t > 0.6f) {
            uint8_t dusk[3];
            lerp_color(dusk, 14, 30, 58, 38, 52, 74, (t - 0.6f) / 0.4f);
            base[0] = dusk[0];
            base[1] = dusk[1];
            base[2] = dusk[2];
        }

        for (int x = 0; x < width; x++) {
            float wave = height * 0.42f + sinf(x * 0.0045f + 1.1f) * 48.0f;
            float highlight = (fabsf(y - wave) < 16.0f) ? 0.35f : 0.0f;
            float deep = (y > wave) ? 0.45f : 0.0f;

            uint8_t r = base[0];
            uint8_t g = base[1];
            uint8_t b = base[2];

            if (deep > 0.0f) {
                uint8_t deepc[3];
                lerp_color(deepc, 10, 38, 82, 22, 78, 140, deep);
                r = (uint8_t)(r + (deepc[0] - r) * deep);
                g = (uint8_t)(g + (deepc[1] - g) * deep);
                b = (uint8_t)(b + (deepc[2] - b) * deep);
            }
            if (highlight > 0.0f) {
                r = (uint8_t)(r + (84 - r) * highlight);
                g = (uint8_t)(g + (148 - g) * highlight);
                b = (uint8_t)(b + (170 - b) * highlight);
            }

            set_pixel(x, y, r, g, b, 255);
        }
    }
}

/* Simple blue gradient */
static void create_gradient_blue(void)
{
    for (int y = 0; y < height; y++) {
        float t = (float)y / height;
        uint8_t color[3];
        lerp_color(color, 29, 90, 156,   /* Royal blue */
                         58, 157, 212,   /* Ocean */
                         t);

        for (int x = 0; x < width; x++) {
            set_pixel(x, y, color[0], color[1], color[2], 255);
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <width> <height> <style> <output.raw>\n", argv[0]);
        fprintf(stderr, "Styles: tahoe_light, tahoe_dark, gradient_blue\n");
        return 1;
    }

    width = atoi(argv[1]);
    height = atoi(argv[2]);
    const char *style = argv[3];
    const char *output = argv[4];

    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        fprintf(stderr, "Invalid dimensions: %dx%d\n", width, height);
        return 1;
    }

    size_t size = width * height * 4;
    pixels = malloc(size);
    if (!pixels) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    memset(pixels, 0, size);

    /* Create wallpaper based on style */
    if (strcmp(style, "tahoe_light") == 0) {
        create_tahoe_light();
    } else if (strcmp(style, "tahoe_dark") == 0) {
        create_tahoe_dark();
    } else if (strcmp(style, "gradient_blue") == 0) {
        create_gradient_blue();
    } else {
        fprintf(stderr, "Unknown style: %s\n", style);
        free(pixels);
        return 1;
    }

    /* Write output */
    FILE *f = fopen(output, "wb");
    if (!f) {
        fprintf(stderr, "Cannot create: %s\n", output);
        free(pixels);
        return 1;
    }

    /* Write header: width, height, then RGBA data */
    uint32_t header[2] = { width, height };
    fwrite(header, sizeof(header), 1, f);
    fwrite(pixels, 1, size, f);
    fclose(f);

    printf("Created %s wallpaper: %s (%dx%d, %zu bytes)\n",
           style, output, width, height, size + sizeof(header));

    free(pixels);
    return 0;
}
