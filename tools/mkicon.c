/*
 * mkicon - Create simple RGBA icons for ojjyOS
 *
 * Usage: mkicon <type> <output.raw>
 *
 * Types: about, settings, terminal, folder, finder, textedit, notes, preview, file, calendar
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ICON_SIZE   32
#define ICON_BYTES  (ICON_SIZE * ICON_SIZE * 4)

static uint8_t icon[ICON_BYTES];

/* Set pixel (RGBA) */
static void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (x < 0 || x >= ICON_SIZE || y < 0 || y >= ICON_SIZE) return;
    int idx = (y * ICON_SIZE + x) * 4;
    icon[idx + 0] = r;
    icon[idx + 1] = g;
    icon[idx + 2] = b;
    icon[idx + 3] = a;
}

/* Draw filled circle */
static void fill_circle(int cx, int cy, int r, uint8_t red, uint8_t g, uint8_t b, uint8_t a)
{
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                set_pixel(cx + x, cy + y, red, g, b, a);
            }
        }
    }
}

/* Draw filled rectangle */
static void fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            set_pixel(px, py, r, g, b, a);
        }
    }
}

/* Draw rounded rectangle */
static void fill_rounded_rect(int x, int y, int w, int h, int r,
                               uint8_t red, uint8_t g, uint8_t b, uint8_t a)
{
    /* Main body */
    fill_rect(x + r, y, w - 2*r, h, red, g, b, a);
    fill_rect(x, y + r, w, h - 2*r, red, g, b, a);

    /* Corners */
    fill_circle(x + r, y + r, r, red, g, b, a);
    fill_circle(x + w - r - 1, y + r, r, red, g, b, a);
    fill_circle(x + r, y + h - r - 1, r, red, g, b, a);
    fill_circle(x + w - r - 1, y + h - r - 1, r, red, g, b, a);
}

/* About icon - info "i" in a circle */
static void create_about_icon(void)
{
    /* Blue gradient background circle */
    fill_circle(16, 16, 14, 58, 157, 212, 255);   /* Ocean blue */
    fill_circle(16, 16, 12, 70, 170, 220, 255);

    /* White "i" */
    /* Dot */
    fill_circle(16, 9, 2, 255, 255, 255, 255);
    /* Stem */
    fill_rect(14, 13, 4, 10, 255, 255, 255, 255);
    /* Serifs */
    fill_rect(12, 13, 8, 2, 255, 255, 255, 255);
    fill_rect(12, 21, 8, 2, 255, 255, 255, 255);
}

/* Settings icon - gear */
static void create_settings_icon(void)
{
    /* Gray background */
    fill_circle(16, 16, 14, 100, 100, 110, 255);

    /* Gear teeth (simplified) */
    fill_rect(14, 2, 4, 6, 80, 80, 90, 255);
    fill_rect(14, 24, 4, 6, 80, 80, 90, 255);
    fill_rect(2, 14, 6, 4, 80, 80, 90, 255);
    fill_rect(24, 14, 6, 4, 80, 80, 90, 255);

    /* Inner circle */
    fill_circle(16, 16, 8, 60, 60, 70, 255);
    fill_circle(16, 16, 4, 100, 100, 110, 255);
}

/* Terminal icon */
static void create_terminal_icon(void)
{
    /* Dark background with rounded corners */
    fill_rounded_rect(2, 2, 28, 28, 4, 40, 40, 50, 255);

    /* Prompt ">" */
    for (int i = 0; i < 6; i++) {
        set_pixel(8 + i, 12 + i, 100, 255, 100, 255);
        set_pixel(8 + i, 18 - i, 100, 255, 100, 255);
    }

    /* Cursor */
    fill_rect(18, 14, 8, 3, 200, 200, 200, 255);
}

/* Folder icon */
static void create_folder_icon(void)
{
    /* Tab */
    fill_rounded_rect(2, 6, 10, 6, 2, 100, 150, 220, 255);

    /* Main folder body */
    fill_rounded_rect(2, 10, 28, 18, 3, 80, 140, 210, 255);

    /* Highlight */
    fill_rect(4, 12, 24, 2, 100, 160, 230, 255);
}

/* Finder icon - split face */
static void create_finder_icon(void)
{
    fill_rounded_rect(2, 2, 28, 28, 6, 90, 170, 220, 255);
    fill_rect(16, 2, 14, 28, 60, 130, 200, 255);

    fill_rect(8, 10, 4, 2, 30, 60, 90, 255);
    fill_rect(20, 10, 4, 2, 30, 60, 90, 255);
    fill_rect(12, 20, 8, 2, 30, 60, 90, 255);
}

/* TextEdit icon - paper with lines */
static void create_textedit_icon(void)
{
    fill_rounded_rect(6, 4, 20, 24, 3, 240, 246, 252, 255);
    fill_rect(10, 9, 12, 2, 120, 170, 210, 255);
    fill_rect(10, 13, 12, 2, 120, 170, 210, 255);
    fill_rect(10, 17, 12, 2, 120, 170, 210, 255);
    fill_rect(10, 21, 8, 2, 120, 170, 210, 255);

    fill_rect(6, 4, 20, 4, 200, 220, 240, 255);
}

/* Notes icon - yellow note */
static void create_notes_icon(void)
{
    fill_rounded_rect(4, 4, 24, 24, 4, 248, 220, 120, 255);
    fill_rect(6, 10, 20, 2, 120, 90, 40, 255);
    fill_rect(6, 14, 20, 2, 120, 90, 40, 255);
    fill_rect(6, 18, 16, 2, 120, 90, 40, 255);
}

/* Preview icon - blue photo tile */
static void create_preview_icon(void)
{
    fill_rounded_rect(3, 3, 26, 26, 5, 80, 150, 220, 255);
    fill_circle(12, 12, 4, 200, 230, 255, 255);
    fill_rect(6, 18, 20, 6, 40, 110, 170, 255);
}

/* File icon - white document */
static void create_file_icon(void)
{
    fill_rounded_rect(6, 4, 20, 24, 3, 245, 246, 252, 255);
    fill_rect(10, 10, 12, 2, 140, 170, 200, 255);
    fill_rect(10, 14, 10, 2, 140, 170, 200, 255);
    fill_rect(10, 18, 8, 2, 140, 170, 200, 255);
}

/* Calendar icon - date card */
static void create_calendar_icon(void)
{
    fill_rounded_rect(5, 4, 22, 24, 3, 245, 248, 252, 255);
    fill_rect(5, 4, 22, 6, 60, 140, 210, 255);
    fill_rect(8, 14, 4, 4, 30, 60, 90, 255);
    fill_rect(14, 14, 4, 4, 30, 60, 90, 255);
    fill_rect(20, 14, 4, 4, 30, 60, 90, 255);
    fill_rect(12, 20, 8, 4, 30, 60, 90, 255);
}

/* Wallpaper gradient */
static void create_wallpaper(void)
{
    /* Simple blue gradient */
    for (int y = 0; y < ICON_SIZE; y++) {
        uint8_t b = 180 + (y * 2);
        uint8_t g = 100 + y;
        for (int x = 0; x < ICON_SIZE; x++) {
            set_pixel(x, y, 50, g, b, 255);
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <type> <output.raw>\n", argv[0]);
        fprintf(stderr, "Types: about, settings, terminal, folder, wallpaper\n");
        return 1;
    }

    const char *type = argv[1];
    const char *output = argv[2];

    /* Clear icon */
    memset(icon, 0, ICON_BYTES);

    /* Create icon based on type */
    if (strcmp(type, "about") == 0) {
        create_about_icon();
    } else if (strcmp(type, "settings") == 0) {
        create_settings_icon();
    } else if (strcmp(type, "terminal") == 0) {
        create_terminal_icon();
    } else if (strcmp(type, "folder") == 0) {
        create_folder_icon();
    } else if (strcmp(type, "finder") == 0) {
        create_finder_icon();
    } else if (strcmp(type, "textedit") == 0) {
        create_textedit_icon();
    } else if (strcmp(type, "notes") == 0) {
        create_notes_icon();
    } else if (strcmp(type, "preview") == 0) {
        create_preview_icon();
    } else if (strcmp(type, "file") == 0) {
        create_file_icon();
    } else if (strcmp(type, "calendar") == 0) {
        create_calendar_icon();
    } else if (strcmp(type, "wallpaper") == 0) {
        create_wallpaper();
    } else {
        fprintf(stderr, "Unknown type: %s\n", type);
        return 1;
    }

    /* Write output */
    FILE *f = fopen(output, "wb");
    if (!f) {
        fprintf(stderr, "Cannot create: %s\n", output);
        return 1;
    }
    fwrite(icon, 1, ICON_BYTES, f);
    fclose(f);

    printf("Created %s icon: %s (%d bytes)\n", type, output, ICON_BYTES);
    return 0;
}
