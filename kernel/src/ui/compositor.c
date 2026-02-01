/*
 * ojjyOS v3 Kernel - Tahoe Compositor
 */

#include "compositor.h"
#include "theme.h"
#include "services.h"
#include "../framebuffer.h"
#include "../font.h"
#include "../string.h"
#include "../timer.h"
#include "../fs/vfs.h"
#include "../drivers/rtc.h"
#include "../serial.h"

#define COMPOSITOR_MAX_WINDOWS  8
#define WALLPAPER_MAX_W         1024
#define WALLPAPER_MAX_H         1024

#define MENU_BAR_HEIGHT         26
#define DOCK_HEIGHT             72
#define DOCK_ICON_BASE          36
#define DOCK_ICON_MAX           56
#define DOCK_HOVER_RADIUS       90
#define SPOTLIGHT_WIDTH         520
#define SPOTLIGHT_HEIGHT        58
#define CONTROL_CENTER_WIDTH    260
#define CONTROL_CENTER_HEIGHT   220
#define LAUNCHPAD_ICON_SIZE     56
#define LAUNCHPAD_COLS          4
#define LAUNCHPAD_ROWS          3
#define SEARCH_QUERY_MAX        48
#define PREVIEW_THUMB_W         120
#define PREVIEW_THUMB_H         80
#define PREVIEW_RAW_MAX         (640 * 480 * 4)

static uint32_t comp_width = 0;
static uint32_t comp_height = 0;

static CompositorWindow windows[COMPOSITOR_MAX_WINDOWS];
static int window_count = 0;

static const ThemeTokens *theme = NULL;
static bool dark_mode = false;

static bool wallpaper_loaded = false;
static uint32_t wallpaper_w = 0;
static uint32_t wallpaper_h = 0;
static uint8_t wallpaper_data[WALLPAPER_MAX_W * WALLPAPER_MAX_H * 4];

static bool dragging = false;
static int drag_index = -1;
static int drag_dx = 0;
static int drag_dy = 0;
static int cursor_x = 0;
static int cursor_y = 0;

static char active_app_name[32] = "Finder";

typedef enum {
    APP_DEMO = 0,
    APP_FINDER,
    APP_SETTINGS,
    APP_TERMINAL,
    APP_TEXTEDIT,
    APP_NOTES,
    APP_PREVIEW,
    APP_CALENDAR,
    APP_ABOUT,
    APP_COUNT
} AppType;

typedef enum {
    FINDER_VIEW_ICON = 0,
    FINDER_VIEW_LIST
} FinderViewMode;

typedef struct {
    char name[64];
    VfsFileType type;
    uint64_t size;
} FinderEntry;

#define FINDER_MAX_ENTRIES 64

typedef struct {
    char path[128];
    FinderViewMode view_mode;
    int entry_count;
    int selected;
    FinderEntry entries[FINDER_MAX_ENTRIES];
    char search[32];
    char history[8][128];
    int history_count;
    int history_pos;
    bool rename_mode;
    char rename_buffer[64];
    char clip_path[128];
    bool clip_cut;
    char preview_path[128];
    char preview_name[64];
    char preview_type[16];
    uint64_t preview_size;
    char preview_lines[3][64];
    bool preview_ready;
    bool drag_active;
    char drag_path[128];
    int drag_hover_index;
    bool needs_refresh;
} FinderState;

typedef enum {
    SETTINGS_PAGE_APPEARANCE = 0,
    SETTINGS_PAGE_WALLPAPER,
    SETTINGS_PAGE_DOCK,
    SETTINGS_PAGE_KEYBOARD,
    SETTINGS_PAGE_MOUSE,
    SETTINGS_PAGE_ABOUT,
    SETTINGS_PAGE_COUNT
} SettingsPage;

typedef struct {
    SettingsPage page;
} SettingsStateUi;

#define TERMINAL_MAX_LINES 48
#define TERMINAL_LINE_MAX  80

typedef struct {
    char lines[TERMINAL_MAX_LINES][TERMINAL_LINE_MAX];
    int line_count;
    char input[TERMINAL_LINE_MAX];
    int input_len;
    char cwd[128];
    char history[16][TERMINAL_LINE_MAX];
    int history_count;
    int history_pos;
} TerminalState;

#define TEXTEDIT_MAX_LINES 64
#define TEXTEDIT_LINE_MAX  80

typedef struct {
    char lines[TEXTEDIT_MAX_LINES][TEXTEDIT_LINE_MAX];
    int line_count;
    int cursor_line;
    int cursor_col;
    char file_path[128];
    char status[48];
    bool dirty;
    bool sel_active;
    int sel_line;
    int sel_start;
    int sel_end;
    bool sel_dragging;
} TextEditState;

static char text_clipboard[TEXTEDIT_LINE_MAX];

typedef struct {
    char current[64];
    char options[2][64];
    bool loaded;
    uint8_t light_thumb[120 * 80 * 4];
    uint8_t dark_thumb[120 * 80 * 4];
} PreviewState;

typedef enum {
    CAL_VIEW_MONTH = 0,
    CAL_VIEW_WEEK,
    CAL_VIEW_DAY,
    CAL_VIEW_AGENDA,
} CalendarView;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    bool all_day;
    char title[48];
    char location[48];
    char notes[64];
} CalendarEvent;

#define CALENDAR_MAX_EVENTS 64

typedef struct {
    CalendarView view;
    int year;
    int month;
    int day;
    int selected_day;
    CalendarEvent events[CALENDAR_MAX_EVENTS];
    int event_count;
    int selected_event;
    bool edit_mode;
    int edit_field;
    char edit_buffer[48];
    bool edit_error;
    bool loaded;
} CalendarState;

typedef struct {
    AppType type;
    FinderState finder;
    SettingsStateUi settings;
    TerminalState terminal;
    TextEditState textedit;
    TextEditState notes;
    PreviewState preview;
    CalendarState calendar;
} AppWindowState;

static AppWindowState app_states[COMPOSITOR_MAX_WINDOWS];
static int active_window_index = -1;
static int app_window_index[APP_COUNT];
static char last_opened_path[128] = "";
static uint8_t icon_folder[BUNDLE_ICON_BYTES];
static uint8_t icon_file[BUNDLE_ICON_BYTES];
static bool icon_folder_loaded = false;
static bool icon_file_loaded = false;
static CompositorWmHooks wm_hooks = {0};

typedef enum {
    OVERLAY_NONE = 0,
    OVERLAY_SPOTLIGHT,
    OVERLAY_LAUNCHPAD,
    OVERLAY_CONTROL_CENTER,
    OVERLAY_MISSION_CONTROL,
    OVERLAY_APP_SWITCHER,
} OverlayMode;

static OverlayMode overlay = OVERLAY_NONE;
static bool spotlight_active = false;
static char spotlight_query[SEARCH_QUERY_MAX];
static SearchResult spotlight_results[SEARCH_RESULTS_MAX];
static int spotlight_count = 0;
static int spotlight_selected = 0;

static bool app_switcher_active = false;
static int app_switcher_index = 0;

static bool launchpad_active = false;
static bool control_center_active = false;
static bool mission_control_active = false;

static int anim_spotlight = 0;
static int anim_launchpad = 0;
static int anim_control_center = 0;
static int anim_mission_control = 0;
static int anim_app_switcher = 0;

static uint64_t last_frame_ms = 0;

static uint8_t overlay_alpha(uint8_t base, int anim);
static int overlay_offset(int anim, int max_offset);
static AppType app_type_from_bundle(const char *bundle_id);
static int app_open_window(AppType type, const char *title);
static void finder_refresh(FinderState *state);
static void draw_icon_scaled(const uint8_t *pixels, int src_size, int x, int y, int size);
static void draw_image_scaled(const uint8_t *pixels, int src_w, int src_h, int x, int y, int w, int h);
static void textedit_load_file(TextEditState *edit, const char *path);
static size_t str_append(char *dest, size_t size, const char *src);
static const char *str_find(const char *haystack, const char *needle);
static void terminal_make_prompt(const TerminalState *term, char *out, size_t size);
static bool str_contains_ci(const char *haystack, const char *needle);
static bool preview_load_thumbnail(const char *path, uint8_t *out);
static void normalize_path(char *path, size_t size);
static bool calendar_load(CalendarState *cal);
static void calendar_save(CalendarState *cal);
static int days_in_month(int year, int month);
static int weekday_of_date(int year, int month, int day);
static void textedit_normalize_selection(TextEditState *edit);

static inline Color blend(Color bg, Color fg, uint8_t alpha)
{
    return fb_blend(bg, fg, alpha);
}

static inline Color lerp_color(Color a, Color b, uint8_t t)
{
    uint8_t ar = (a >> 16) & 0xFF;
    uint8_t ag = (a >> 8) & 0xFF;
    uint8_t ab = (a >> 0) & 0xFF;
    uint8_t br = (b >> 16) & 0xFF;
    uint8_t bg = (b >> 8) & 0xFF;
    uint8_t bb = (b >> 0) & 0xFF;

    uint8_t rr = (uint8_t)((ar * (255 - t) + br * t) / 255);
    uint8_t rg = (uint8_t)((ag * (255 - t) + bg * t) / 255);
    uint8_t rb = (uint8_t)((ab * (255 - t) + bb * t) / 255);

    return RGB(rr, rg, rb);
}

static int tri_wave(int x, int period, int amplitude)
{
    int t = x % period;
    int half = period / 2;
    int v = (t < half) ? t : (period - t);
    int centered = (v * amplitude) / half;
    return centered - (amplitude / 2);
}

static Color wallpaper_sample_procedural(int x, int y)
{
    uint8_t t = (uint8_t)((y * 255) / (int)comp_height);
    Color top = theme->sky_top;
    Color mid = theme->sky_mid;
    Color bottom = theme->deep_ocean;

    Color base;
    if (t < 120) {
        base = lerp_color(top, mid, (uint8_t)((t * 255) / 120));
    } else if (t < 190) {
        base = lerp_color(mid, theme->horizon, (uint8_t)(((t - 120) * 255) / 70));
    } else {
        base = lerp_color(theme->horizon, bottom, (uint8_t)(((t - 190) * 255) / 65));
    }

    int wave_y = (int)(comp_height * 0.34) + tri_wave(x, 480, 50);
    int crest = wave_y - 8;

    if (y > crest && y < crest + 22) {
        Color highlight = blend(base, theme->glass_aqua, 110);
        return highlight;
    }

    if (y > wave_y) {
        Color deep = blend(base, theme->wave_blue, 120);
        return deep;
    }

    return base;
}

static Color wallpaper_sample(int x, int y)
{
    if (!wallpaper_loaded || wallpaper_w == 0 || wallpaper_h == 0) {
        return wallpaper_sample_procedural(x, y);
    }

    int sx = (x * (int)wallpaper_w) / (int)comp_width;
    int sy = (y * (int)wallpaper_h) / (int)comp_height;

    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= (int)wallpaper_w) sx = (int)wallpaper_w - 1;
    if (sy >= (int)wallpaper_h) sy = (int)wallpaper_h - 1;

    uint32_t idx = (sy * wallpaper_w + sx) * 4;
    uint8_t r = wallpaper_data[idx + 0];
    uint8_t g = wallpaper_data[idx + 1];
    uint8_t b = wallpaper_data[idx + 2];

    return RGB(r, g, b);
}

static Color blur_sample(int x, int y, int radius)
{
    int step = (radius >= 14) ? 3 : 2;
    int r = 0, g = 0, b = 0, count = 0;

    for (int dy = -radius; dy <= radius; dy += step) {
        for (int dx = -radius; dx <= radius; dx += step) {
            Color c = wallpaper_sample(x + dx, y + dy);
            r += (c >> 16) & 0xFF;
            g += (c >> 8) & 0xFF;
            b += (c >> 0) & 0xFF;
            count++;
        }
    }

    if (count == 0) return COLOR_BLACK;
    r /= count;
    g /= count;
    b /= count;
    return RGB(r, g, b);
}

static bool point_in_rounded_rect(int px, int py, int x, int y, int w, int h, int r)
{
    if (px < x || py < y || px >= x + w || py >= y + h) return false;
    if (r <= 0) return true;

    int rx = x + r;
    int ry = y + r;
    int rx2 = x + w - r - 1;
    int ry2 = y + h - r - 1;

    if ((px >= rx && px <= rx2) || (py >= ry && py <= ry2)) {
        return true;
    }

    int cx = (px < rx) ? rx : rx2;
    int cy = (py < ry) ? ry : ry2;
    int dx = px - cx;
    int dy = py - cy;

    return (dx * dx + dy * dy) <= (r * r);
}

static void draw_rounded_rect_blend(int x, int y, int w, int h, int r, Color color, uint8_t alpha)
{
    int x1 = MAX(0, x);
    int y1 = MAX(0, y);
    int x2 = MIN((int)comp_width, x + w);
    int y2 = MIN((int)comp_height, y + h);

    for (int py = y1; py < y2; py++) {
        for (int px = x1; px < x2; px++) {
            if (!point_in_rounded_rect(px, py, x, y, w, h, r)) {
                continue;
            }
            Color bg = fb_get_pixel(px, py);
            fb_put_pixel(px, py, blend(bg, color, alpha));
        }
    }
}

static void draw_shadow(int x, int y, int w, int h, int radius)
{
    static const uint8_t shadow_levels[] = { 28, 18, 10 };
    static const int shadow_spread[] = { 6, 10, 14 };

    for (int i = 0; i < 3; i++) {
        int spread = shadow_spread[i];
        uint8_t alpha = shadow_levels[i];
        draw_rounded_rect_blend(x - spread, y - spread, w + spread * 2, h + spread * 2,
                                radius + spread, theme->shadow, alpha);
    }
}

static void draw_finder_window(const CompositorWindow *win, FinderState *state, int content_x, int content_y, int content_w, int content_h)
{
    if (state->needs_refresh) {
        finder_refresh(state);
    }

    int sidebar_w = 150;
    int preview_w = 180;
    draw_rounded_rect_blend(content_x, content_y, sidebar_w, content_h, 12, theme->dock_tint, 130);
    fb_draw_string(content_x + 12, content_y + 12, "Favorites", theme->text_muted, theme->dock_tint);
    fb_draw_string(content_x + 12, content_y + 32, "Applications", theme->text, theme->dock_tint);
    fb_draw_string(content_x + 12, content_y + 52, "System", theme->text, theme->dock_tint);
    fb_draw_string(content_x + 12, content_y + 72, "Users", theme->text, theme->dock_tint);

    int main_x = content_x + sidebar_w + 10;
    int main_w = content_w - sidebar_w - preview_w - 20;

    draw_rounded_rect_blend(main_x, content_y, main_w, content_h, 12, theme->dock_tint, 90);
    fb_draw_string(main_x + 12, content_y + 12, state->path[0] ? state->path : "/",
                   theme->text_muted, theme->dock_tint);

    int preview_x = main_x + main_w + 10;
    draw_rounded_rect_blend(preview_x, content_y, preview_w, content_h, 12, theme->dock_tint, 110);
    fb_draw_string(preview_x + 12, content_y + 12, "Preview", theme->text_muted, theme->dock_tint);
    if (state->preview_ready) {
        fb_draw_string(preview_x + 12, content_y + 32, state->preview_name, theme->text, theme->dock_tint);
        fb_draw_string(preview_x + 12, content_y + 50, state->preview_type, theme->text_muted, theme->dock_tint);
        char sizebuf[24];
        if (state->preview_size > 0) {
            utoa(state->preview_size, sizebuf, 10);
            fb_draw_string(preview_x + 12, content_y + 68, sizebuf, theme->text_muted, theme->dock_tint);
        }
        for (int i = 0; i < 3; i++) {
            if (state->preview_lines[i][0]) {
                fb_draw_string(preview_x + 12, content_y + 90 + i * 16,
                               state->preview_lines[i], theme->text_muted, theme->dock_tint);
            }
        }
    } else {
        fb_draw_string(preview_x + 12, content_y + 32, "No selection", theme->text_muted, theme->dock_tint);
    }

    if (state->view_mode == FINDER_VIEW_LIST) {
        fb_draw_string(main_x + 16, content_y + 28, "Name", theme->text_muted, theme->dock_tint);
        fb_draw_string(main_x + 220, content_y + 28, "Type", theme->text_muted, theme->dock_tint);
        fb_draw_string(main_x + 300, content_y + 28, "Size", theme->text_muted, theme->dock_tint);

        int row_y = content_y + 44;
        for (int i = 0; i < state->entry_count && i < 16; i++) {
            FinderEntry *entry = &state->entries[i];
            if (i == state->selected) {
                draw_rounded_rect_blend(main_x + 6, row_y - 2, main_w - 12, 18, 8, theme->accent, 50);
            } else if (state->drag_active && i == state->drag_hover_index && entry->type == VFS_TYPE_DIR) {
                draw_rounded_rect_blend(main_x + 6, row_y - 2, main_w - 12, 18, 8, theme->accent_soft, 40);
            }
            fb_draw_string(main_x + 16, row_y + 2, entry->name, theme->text, theme->dock_tint);

            const char *type = "File";
            if (entry->type == VFS_TYPE_DIR) type = "Folder";
            if (entry->type == VFS_TYPE_BUNDLE) type = "App";
            fb_draw_string(main_x + 220, row_y + 2, type, theme->text_muted, theme->dock_tint);

            char sizebuf[16];
            if (entry->type == VFS_TYPE_FILE) {
                utoa(entry->size, sizebuf, 10);
            } else {
                strcpy(sizebuf, "--");
            }
            fb_draw_string(main_x + 300, row_y + 2, sizebuf, theme->text_muted, theme->dock_tint);

            row_y += 18;
        }
    } else {
        int cols = 4;
        int icon = 40;
        int gap = 18;
        int start_x = main_x + 16;
        int start_y = content_y + 40;
        for (int i = 0; i < state->entry_count && i < 12; i++) {
            int col = i % cols;
            int row = i / cols;
            int ix = start_x + col * (icon + gap);
            int iy = start_y + row * (icon + 28);
            FinderEntry *entry = &state->entries[i];
            bool drew_icon = false;
            if (state->drag_active && i == state->drag_hover_index && entry->type == VFS_TYPE_DIR) {
                draw_rounded_rect_blend(ix - 2, iy - 2, icon + 4, icon + 4, 8, theme->accent_soft, 40);
            }
            if (entry->type == VFS_TYPE_BUNDLE) {
                char path[256];
                vfs_join_path(path, sizeof(path), state->path, entry->name);
                Bundle bundle;
                if (bundle_load(path, &bundle) == 0) {
                    int app_idx = app_registry_find_by_bundle_id(bundle.manifest.bundle_id);
                    AppInfo *app = app_registry_get(app_idx);
                    if (app && app->icon.valid) {
                        draw_icon_scaled(app->icon.pixels, BUNDLE_ICON_SIZE, ix, iy, icon);
                        drew_icon = true;
                    }
                }
            } else if (entry->type == VFS_TYPE_DIR) {
                if (icon_folder_loaded) {
                    draw_icon_scaled(icon_folder, BUNDLE_ICON_SIZE, ix, iy, icon);
                    drew_icon = true;
                }
            } else if (entry->type == VFS_TYPE_FILE) {
                if (icon_file_loaded) {
                    draw_icon_scaled(icon_file, BUNDLE_ICON_SIZE, ix, iy, icon);
                    drew_icon = true;
                }
            }
            if (!drew_icon) {
                fb_fill_rect(ix, iy, icon, icon, theme->accent_soft);
            }
            fb_draw_string(ix - 2, iy + icon + 8, state->entries[i].name, theme->text, theme->dock_tint);
        }
    }

    int toolbar_y = win->y + 36;
    fb_fill_rect(content_x + 8, toolbar_y - 26, 18, 18, theme->accent_soft);
    fb_fill_rect(content_x + 30, toolbar_y - 26, 18, 18, theme->accent_soft);
    fb_draw_string(content_x + 62, toolbar_y - 22, "View", theme->text_muted, theme->dock_tint);

    int search_x = main_x + main_w - 150;
    fb_fill_rect(search_x, toolbar_y - 26, 140, 18, theme->dock_tint);
    if (state->rename_mode) {
        fb_draw_string(search_x + 6, toolbar_y - 22, state->rename_buffer,
                       theme->text, theme->dock_tint);
        fb_draw_string(search_x - 70, toolbar_y - 22, "Rename:",
                       theme->text_muted, theme->dock_tint);
    } else {
        fb_draw_string(search_x + 6, toolbar_y - 22,
                       state->search[0] ? state->search : "Search",
                       theme->text_muted, theme->dock_tint);
    }

    if (state->drag_active) {
        fb_draw_string(cursor_x + 10, cursor_y + 10, vfs_basename(state->drag_path),
                       theme->text, theme->dock_tint);
    }
}

static void draw_settings_window(SettingsStateUi *state, int content_x, int content_y, int content_w, int content_h)
{
    int sidebar_w = 160;
    draw_rounded_rect_blend(content_x, content_y, sidebar_w, content_h, 12, theme->dock_tint, 130);

    const char *items[] = {
        "Appearance",
        "Wallpaper",
        "Dock & Menu",
        "Keyboard",
        "Mouse",
        "About"
    };

    for (int i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        int item_y = content_y + 16 + i * 20;
        if (state->page == (SettingsPage)i) {
            draw_rounded_rect_blend(content_x + 6, item_y - 2, sidebar_w - 12, 18, 8, theme->accent, 40);
        }
        fb_draw_string(content_x + 12, item_y, items[i], theme->text, theme->dock_tint);
    }

    int main_x = content_x + sidebar_w + 10;
    int main_w = content_w - sidebar_w - 10;
    draw_rounded_rect_blend(main_x, content_y, main_w, content_h, 12, theme->dock_tint, 90);

    SettingsState *settings = settings_get();

    if (state->page == SETTINGS_PAGE_APPEARANCE) {
        fb_draw_string(main_x + 16, content_y + 16, "Appearance", theme->text, theme->dock_tint);
        fb_draw_string(main_x + 16, content_y + 42,
                       settings->dark_mode ? "Dark" : "Light", theme->text_muted, theme->dock_tint);
        fb_fill_rect(main_x + 100, content_y + 38, 40, 14, settings->dark_mode ? theme->accent : theme->accent_soft);
        fb_draw_string(main_x + 16, content_y + 64,
                       settings->time_24h ? "Time: 24-hour" : "Time: 12-hour",
                       theme->text_muted, theme->dock_tint);
        fb_fill_rect(main_x + 140, content_y + 60, 40, 14, settings->time_24h ? theme->accent : theme->accent_soft);
    } else if (state->page == SETTINGS_PAGE_WALLPAPER) {
        fb_draw_string(main_x + 16, content_y + 16, "Wallpaper", theme->text, theme->dock_tint);
        fb_fill_rect(main_x + 16, content_y + 48, 80, 50, theme->accent_soft);
        fb_draw_string(main_x + 20, content_y + 104, "Tahoe Light", theme->text_muted, theme->dock_tint);
        fb_fill_rect(main_x + 120, content_y + 48, 80, 50, theme->accent);
        fb_draw_string(main_x + 124, content_y + 104, "Tahoe Dark", theme->text_muted, theme->dock_tint);
    } else if (state->page == SETTINGS_PAGE_DOCK) {
        fb_draw_string(main_x + 16, content_y + 16, "Dock & Menu Bar", theme->text, theme->dock_tint);
        fb_draw_string(main_x + 16, content_y + 42, "Dock Size", theme->text_muted, theme->dock_tint);
        fb_fill_rect(main_x + 120, content_y + 38, settings->dock_size, 10, theme->accent_soft);
        fb_draw_string(main_x + 16, content_y + 64, "Magnification", theme->text_muted, theme->dock_tint);
        fb_fill_rect(main_x + 120, content_y + 60, settings->dock_magnify, 10, theme->accent);
    } else if (state->page == SETTINGS_PAGE_KEYBOARD) {
        fb_draw_string(main_x + 16, content_y + 16, "Keyboard", theme->text, theme->dock_tint);
        fb_draw_string(main_x + 16, content_y + 42,
                       settings->shortcuts_enabled ? "Shortcuts: On" : "Shortcuts: Off",
                       theme->text_muted, theme->dock_tint);
    } else if (state->page == SETTINGS_PAGE_MOUSE) {
        fb_draw_string(main_x + 16, content_y + 16, "Mouse/Trackpad", theme->text, theme->dock_tint);
        fb_draw_string(main_x + 16, content_y + 42, "Tracking Speed", theme->text_muted, theme->dock_tint);
        fb_fill_rect(main_x + 140, content_y + 38, settings->mouse_speed * 20, 10, theme->accent_soft);
    } else if (state->page == SETTINGS_PAGE_ABOUT) {
        fb_draw_string(main_x + 16, content_y + 16, "About", theme->text, theme->dock_tint);
        fb_draw_string(main_x + 16, content_y + 42, "ojjyOS v3", theme->text_muted, theme->dock_tint);
        fb_draw_string(main_x + 16, content_y + 60, "Created by Jonas Lee", theme->text_muted, theme->dock_tint);
    }
}

static void draw_terminal_window(TerminalState *term, int content_x, int content_y, int content_w, int content_h)
{
    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 10, theme->midnight, 200);

    int line_y = content_y + 10;
    for (int i = 0; i < term->line_count && i < TERMINAL_MAX_LINES; i++) {
        fb_draw_string(content_x + 10, line_y, term->lines[i], COLOR_TEXT_LIGHT, theme->midnight);
        line_y += 16;
    }

    char prompt[TERMINAL_LINE_MAX + 32];
    char prefix[64];
    terminal_make_prompt(term, prefix, sizeof(prefix));
    prompt[0] = '\0';
    str_append(prompt, sizeof(prompt), prefix);
    str_append(prompt, sizeof(prompt), " ");
    str_append(prompt, sizeof(prompt), term->input);
    fb_draw_string(content_x + 10, content_y + content_h - 20, prompt, COLOR_TEXT_LIGHT, theme->midnight);
}

static void draw_textedit_window(TextEditState *edit, int content_x, int content_y, int content_w, int content_h)
{
    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 10, theme->dock_tint, 120);

    fb_draw_string(content_x + 10, content_y + 8,
                   edit->file_path[0] ? edit->file_path : "Untitled", theme->text, theme->dock_tint);

    int line_y = content_y + 30;
    for (int i = 0; i < edit->line_count && i < TEXTEDIT_MAX_LINES; i++) {
        if (edit->sel_active && edit->sel_line == i) {
            textedit_normalize_selection(edit);
            int start = edit->sel_start;
            int end = edit->sel_end;
            if (start < end) {
                int x1 = content_x + 10 + start * FONT_WIDTH;
                int width = (end - start) * FONT_WIDTH;
                draw_rounded_rect_blend(x1 - 2, line_y - 2, width + 4, FONT_HEIGHT + 4,
                                        6, theme->accent, 40);
            }
        }
        fb_draw_string(content_x + 10, line_y, edit->lines[i], theme->text, theme->dock_tint);
        line_y += 16;
    }

    char status[96];
    status[0] = '\0';
    if (edit->dirty) {
        str_append(status, sizeof(status), "Edited • ");
    }
    str_append(status, sizeof(status), edit->status);
    str_append(status, sizeof(status), "  Ln ");
    char num[8];
    utoa(edit->cursor_line + 1, num, 10);
    str_append(status, sizeof(status), num);
    str_append(status, sizeof(status), ", Col ");
    utoa(edit->cursor_col + 1, num, 10);
    str_append(status, sizeof(status), num);

    fb_draw_string(content_x + 10, content_y + content_h - 18, status,
                   theme->text_muted, theme->dock_tint);
}

static void calendar_format_time(const CalendarEvent *ev, char *out, size_t size)
{
    if (!ev || !out || size == 0) return;
    if (ev->all_day) {
        strncpy(out, "All-day", size - 1);
        out[size - 1] = '\0';
        return;
    }

    bool is_24 = settings_get()->time_24h;
    int hour = ev->hour;
    const char *suffix = "";

    if (!is_24) {
        suffix = (hour >= 12) ? " PM" : " AM";
        hour = hour % 12;
        if (hour == 0) hour = 12;
    }

    char tmp[16];
    tmp[0] = '\0';
    if (hour < 10) str_append(tmp, sizeof(tmp), "0");
    char num[8];
    utoa(hour, num, 10);
    str_append(tmp, sizeof(tmp), num);
    str_append(tmp, sizeof(tmp), ":");
    if (ev->minute < 10) str_append(tmp, sizeof(tmp), "0");
    utoa(ev->minute, num, 10);
    str_append(tmp, sizeof(tmp), num);
    str_append(tmp, sizeof(tmp), suffix);
    strncpy(out, tmp, size - 1);
    out[size - 1] = '\0';
}

static int calendar_events_for_day(CalendarState *cal, int year, int month, int day, int *indices, int max)
{
    int count = 0;
    for (int i = 0; i < cal->event_count; i++) {
        CalendarEvent *ev = &cal->events[i];
        if (ev->year == year && ev->month == month && ev->day == day) {
            if (indices && count < max) {
                indices[count] = i;
            }
            count++;
        }
    }
    return count;
}

static void draw_calendar_window(CalendarState *cal, int content_x, int content_y, int content_w, int content_h)
{
    if (!cal->loaded) {
        calendar_load(cal);
    }

    RtcTime now;
    rtc_read_time(&now);
    if (now.year == cal->year && now.month == cal->month) {
        cal->day = now.day;
    }

    int header_h = 40;
    int sidebar_w = 160;
    int agenda_w = 200;

    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 12, theme->dock_tint, 120);

    int header_x = content_x + sidebar_w + 10;
    int header_w = content_w - sidebar_w - agenda_w - 20;
    draw_rounded_rect_blend(header_x, content_y, header_w, header_h, 12, theme->dock_tint, 140);

    static const char *months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    char title[64];
    title[0] = '\0';
    str_append(title, sizeof(title), months[cal->month - 1]);
    str_append(title, sizeof(title), " ");
    char num[8];
    utoa(cal->year, num, 10);
    str_append(title, sizeof(title), num);
    fb_draw_string(header_x + 12, content_y + 12, title, theme->text, theme->dock_tint);

    int nav_y = content_y + 10;
    fb_fill_rect(header_x + header_w - 90, nav_y, 18, 18, theme->accent_soft);
    fb_fill_rect(header_x + header_w - 66, nav_y, 18, 18, theme->accent_soft);
    fb_fill_rect(header_x + header_w - 42, nav_y, 36, 18, theme->accent);
    fb_draw_string(header_x + header_w - 36, nav_y + 4, "Today", theme->text, theme->dock_tint);

    int view_x = header_x + header_w - 200;
    const char *views[] = { "Month", "Week", "Day", "Agenda" };
    for (int i = 0; i < 4; i++) {
        int vx = view_x + i * 45;
        if (cal->view == (CalendarView)i) {
            draw_rounded_rect_blend(vx, content_y + header_h - 18, 42, 16, 8, theme->accent, 50);
        }
        fb_draw_string(vx + 4, content_y + header_h - 16, views[i], theme->text_muted, theme->dock_tint);
    }

    draw_rounded_rect_blend(content_x, content_y, sidebar_w, content_h, 12, theme->dock_tint, 130);
    fb_draw_string(content_x + 10, content_y + 10, "Calendars", theme->text_muted, theme->dock_tint);
    fb_draw_string(content_x + 10, content_y + 30, "Local", theme->text, theme->dock_tint);
    fb_draw_string(content_x + 10, content_y + 48, "Personal", theme->text, theme->dock_tint);
    fb_draw_string(content_x + 10, content_y + 66, "Work", theme->text, theme->dock_tint);
    draw_rounded_rect_blend(content_x + 10, content_y + content_h - 36, sidebar_w - 20, 22, 10, theme->accent, 60);
    fb_draw_string(content_x + 20, content_y + content_h - 32, "New Event", theme->text, theme->dock_tint);

    int grid_x = header_x;
    int grid_y = content_y + header_h + 6;
    int grid_w = header_w;
    int grid_h = content_h - header_h - 12;

    if (cal->view == CAL_VIEW_MONTH) {
        static const char *weekdays[] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
        int cell_w = grid_w / 7;
        int cell_h = (grid_h - 20) / 6;
        for (int i = 0; i < 7; i++) {
            fb_draw_string(grid_x + i * cell_w + 6, grid_y, weekdays[i], theme->text_muted, theme->dock_tint);
        }

        int first_wd = weekday_of_date(cal->year, cal->month, 1);
        int days = days_in_month(cal->year, cal->month);
        int day = 1;
        int row_y = grid_y + 14;
        for (int row = 0; row < 6; row++) {
            for (int col = 0; col < 7; col++) {
                int idx = row * 7 + col;
                int cx = grid_x + col * cell_w;
                int cy = row_y + row * cell_h;
                if (idx >= first_wd && day <= days) {
                    if (day == cal->selected_day) {
                        draw_rounded_rect_blend(cx + 2, cy + 2, cell_w - 4, cell_h - 4, 10, theme->accent, 35);
                    }
                    if (day == cal->day) {
                        draw_rounded_rect_blend(cx + 6, cy + 6, 24, 18, 8, theme->accent_soft, 90);
                    }
                    char numstr[4];
                    utoa(day, numstr, 10);
                    fb_draw_string(cx + 8, cy + 6, numstr, theme->text, theme->dock_tint);

                    int indices[4];
                    int count = calendar_events_for_day(cal, cal->year, cal->month, day, indices, 4);
                    int chip_y = cy + 24;
                    int shown = count > 2 ? 2 : count;
                    for (int e = 0; e < shown; e++) {
                        CalendarEvent *ev = &cal->events[indices[e]];
                        draw_rounded_rect_blend(cx + 6, chip_y, cell_w - 12, 12, 6, theme->accent, 70);
                        fb_draw_string(cx + 10, chip_y + 2, ev->title, theme->text, theme->dock_tint);
                        chip_y += 14;
                    }
                    if (count > 2) {
                        fb_draw_string(cx + 10, chip_y + 2, "+", theme->text_muted, theme->dock_tint);
                    }
                    day++;
                }
            }
        }
    } else if (cal->view == CAL_VIEW_WEEK) {
        int cell_w = grid_w / 7;
        int week_start = cal->selected_day - weekday_of_date(cal->year, cal->month, cal->selected_day);
        for (int col = 0; col < 7; col++) {
            int day = week_start + col;
            if (day < 1 || day > days_in_month(cal->year, cal->month)) continue;
            int cx = grid_x + col * cell_w;
            draw_rounded_rect_blend(cx + 2, grid_y, cell_w - 4, grid_h, 8, theme->dock_tint, 100);
            char numstr[4];
            utoa(day, numstr, 10);
            fb_draw_string(cx + 8, grid_y + 6, numstr, theme->text, theme->dock_tint);

            int indices[6];
            int count = calendar_events_for_day(cal, cal->year, cal->month, day, indices, 6);
            int ey = grid_y + 24;
            for (int e = 0; e < count && e < 4; e++) {
                CalendarEvent *ev = &cal->events[indices[e]];
                draw_rounded_rect_blend(cx + 6, ey, cell_w - 12, 12, 6, theme->accent, 70);
                fb_draw_string(cx + 10, ey + 2, ev->title, theme->text, theme->dock_tint);
                ey += 14;
            }
        }
    } else if (cal->view == CAL_VIEW_DAY) {
        int indices[10];
        int count = calendar_events_for_day(cal, cal->year, cal->month, cal->selected_day, indices, 10);
        fb_draw_string(grid_x + 10, grid_y + 6, "Day", theme->text_muted, theme->dock_tint);
        int ey = grid_y + 24;
        for (int i = 0; i < count; i++) {
            CalendarEvent *ev = &cal->events[indices[i]];
            char timebuf[16];
            calendar_format_time(ev, timebuf, sizeof(timebuf));
            fb_draw_string(grid_x + 10, ey, timebuf, theme->text_muted, theme->dock_tint);
            fb_draw_string(grid_x + 80, ey, ev->title, theme->text, theme->dock_tint);
            ey += 18;
        }
    } else {
        int ey = grid_y + 10;
        for (int i = 0; i < cal->event_count && i < 10; i++) {
            CalendarEvent *ev = &cal->events[i];
            char timebuf[16];
            calendar_format_time(ev, timebuf, sizeof(timebuf));
            fb_draw_string(grid_x + 8, ey, timebuf, theme->text_muted, theme->dock_tint);
            fb_draw_string(grid_x + 72, ey, ev->title, theme->text, theme->dock_tint);
            ey += 18;
        }
    }

    int agenda_x = content_x + content_w - agenda_w;
    draw_rounded_rect_blend(agenda_x, content_y, agenda_w, content_h, 12, theme->dock_tint, 110);
    fb_draw_string(agenda_x + 10, content_y + 10, "Agenda", theme->text_muted, theme->dock_tint);
    int indices[8];
    int count = calendar_events_for_day(cal, cal->year, cal->month, cal->selected_day, indices, 8);
    int ay = content_y + 30;
    for (int i = 0; i < count && i < 6; i++) {
        CalendarEvent *ev = &cal->events[indices[i]];
        char timebuf[16];
        calendar_format_time(ev, timebuf, sizeof(timebuf));
        if (indices[i] == cal->selected_event) {
            draw_rounded_rect_blend(agenda_x + 6, ay - 2, agenda_w - 12, 16, 8, theme->accent, 40);
        }
        fb_draw_string(agenda_x + 10, ay, timebuf, theme->text_muted, theme->dock_tint);
        fb_draw_string(agenda_x + 70, ay, ev->title, theme->text, theme->dock_tint);
        ay += 18;
    }

    if (cal->edit_mode && cal->selected_event >= 0) {
        draw_rounded_rect_blend(agenda_x + 8, content_y + content_h - 40, agenda_w - 16, 26, 8,
                                theme->dock_tint, 140);
        const char *label = "Title";
        if (cal->edit_field == 1) label = "Time";
        if (cal->edit_field == 2) label = "Location";
        if (cal->edit_field == 3) label = "Notes";
        fb_draw_string(agenda_x + 12, content_y + content_h - 36, label,
                       theme->text_muted, theme->dock_tint);
        fb_draw_string(agenda_x + 70, content_y + content_h - 36,
                       cal->edit_buffer[0] ? cal->edit_buffer : "",
                       theme->text, theme->dock_tint);
        if (cal->edit_error) {
            fb_draw_string(agenda_x + 12, content_y + content_h - 20,
                           "Invalid time", theme->text_muted, theme->dock_tint);
        }
    }
}

static void draw_preview_window(PreviewState *preview, int content_x, int content_y, int content_w, int content_h)
{
    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 10, theme->dock_tint, 120);
    fb_draw_string(content_x + 12, content_y + 12, "Preview", theme->text, theme->dock_tint);

    int thumb_y = content_y + 40;
    int thumb_x = content_x + 16;
    int gap = 16;

    if (preview->loaded) {
        draw_image_scaled(preview->light_thumb, PREVIEW_THUMB_W, PREVIEW_THUMB_H, thumb_x, thumb_y, 120, 80);
        draw_image_scaled(preview->dark_thumb, PREVIEW_THUMB_W, PREVIEW_THUMB_H, thumb_x + 120 + gap, thumb_y, 120, 80);
    } else {
        fb_fill_rect(thumb_x, thumb_y, 120, 80, theme->accent_soft);
        fb_fill_rect(thumb_x + 120 + gap, thumb_y, 120, 80, theme->accent);
    }

    fb_draw_string(thumb_x + 10, thumb_y + 90, "Tahoe Light", theme->text_muted, theme->dock_tint);
    fb_draw_string(thumb_x + 140 + gap, thumb_y + 90, "Tahoe Dark", theme->text_muted, theme->dock_tint);

    fb_draw_string(content_x + 12, content_y + content_h - 18,
                   preview->current, theme->text_muted, theme->dock_tint);
}

static void draw_window(int idx)
{
    CompositorWindow *win = &windows[idx];
    AppWindowState *state = &app_states[idx];
    int r = theme->glass.corner_radius[win->corner_level];
    int blur_px = theme->glass.blur_px[win->blur_level];
    uint8_t opacity = theme->glass.opacity[win->glass_level];
    uint8_t highlight = theme->glass.highlight[win->glass_level];

    int anim = win->anim_open;
    if (anim < 0) anim = 0;
    if (anim > 1000) anim = 1000;

    int scale = 900 + (anim / 10);
    int draw_w = (win->w * scale) / 1000;
    int draw_h = (win->h * scale) / 1000;
    int draw_x = win->x + (win->w - draw_w) / 2;
    int draw_y = win->y + (win->h - draw_h) / 2;

    draw_shadow(draw_x, draw_y, draw_w, draw_h, r);

    for (int py = draw_y; py < draw_y + draw_h; py++) {
        for (int px = draw_x; px < draw_x + draw_w; px++) {
            if (!point_in_rounded_rect(px, py, draw_x, draw_y, draw_w, draw_h, r)) {
                continue;
            }
            Color blurred = blur_sample(px, py, blur_px);
            Color glass = blend(blurred, theme->glass_aqua, opacity);
            fb_put_pixel(px, py, glass);
        }
    }

    draw_rounded_rect_blend(draw_x, draw_y, draw_w, 32, r, theme->accent_soft, highlight);
    fb_draw_string(draw_x + 16, draw_y + 10, win->title,
                   theme->text, blend(theme->accent_soft, theme->glass_aqua, 24));

    fb_fill_rect(draw_x + 10, draw_y + 10, 8, 8, RGB(235, 92, 86));
    fb_fill_rect(draw_x + 22, draw_y + 10, 8, 8, RGB(245, 197, 72));
    fb_fill_rect(draw_x + 34, draw_y + 10, 8, 8, RGB(86, 200, 105));

    int content_x = draw_x + 12;
    int content_y = draw_y + 40;
    int content_w = draw_w - 24;
    int content_h = draw_h - 52;

    switch (state->type) {
        case APP_FINDER:
            draw_finder_window(win, &state->finder, content_x, content_y, content_w, content_h);
            break;
        case APP_SETTINGS:
            draw_settings_window(&state->settings, content_x, content_y, content_w, content_h);
            break;
        case APP_TERMINAL:
            draw_terminal_window(&state->terminal, content_x, content_y, content_w, content_h);
            break;
        case APP_TEXTEDIT:
            draw_textedit_window(&state->textedit, content_x, content_y, content_w, content_h);
            break;
        case APP_NOTES:
            draw_textedit_window(&state->notes, content_x, content_y, content_w, content_h);
            break;
        case APP_PREVIEW:
            draw_preview_window(&state->preview, content_x, content_y, content_w, content_h);
            break;
        case APP_CALENDAR:
            draw_calendar_window(&state->calendar, content_x, content_y, content_w, content_h);
            break;
        default:
            break;
    }

    if (win->demo) {
        int panel_x = draw_x + 30;
        int panel_y = draw_y + 50;
        int panel_w = draw_w - 60;
        int panel_h = draw_h - 80;
        int pr = r > 10 ? r - 6 : r;

        draw_rounded_rect_blend(panel_x, panel_y, panel_w, panel_h, pr, theme->accent, 40);
        fb_draw_string(panel_x + 20, panel_y + 16, "Glass Panel",
                       theme->text, blend(theme->accent, theme->glass_aqua, 40));
        fb_draw_string(panel_x + 20, panel_y + 36, "Tahoe material demo",
                       theme->text_muted, blend(theme->accent, theme->glass_aqua, 30));

        int dot_y = panel_y + 62;
        for (int i = 0; i < 3; i++) {
            int dot_x = panel_x + 20 + i * 18;
            fb_fill_rect(dot_x, dot_y, 10, 10, theme->accent);
        }
    }
}

static void draw_wallpaper(void)
{
    for (uint32_t y = 0; y < comp_height; y++) {
        for (uint32_t x = 0; x < comp_width; x++) {
            fb_put_pixel((int)x, (int)y, wallpaper_sample((int)x, (int)y));
        }
    }
}

static void draw_menu_bar(void)
{
    draw_rounded_rect_blend(0, 0, (int)comp_width, MENU_BAR_HEIGHT, 0,
                            theme->dock_tint, 140);

    fb_draw_string(14, 8, active_app_name, theme->text, theme->dock_tint);
    fb_draw_string(120, 8, "File  Edit  View  Window  Help", theme->text_muted, theme->dock_tint);

    RtcTime time;
    rtc_read_time(&time);
    char time_buf[16];
    bool is_24 = settings_get()->time_24h;
    int hour = time.hour;
    const char *suffix = "";

    if (!is_24) {
        suffix = (hour >= 12) ? " PM" : " AM";
        hour = hour % 12;
        if (hour == 0) hour = 12;
    }

    char num[8];
    time_buf[0] = '\0';
    if (hour < 10) str_append(time_buf, sizeof(time_buf), "0");
    utoa(hour, num, 10);
    str_append(time_buf, sizeof(time_buf), num);
    str_append(time_buf, sizeof(time_buf), ":");
    if (time.minute < 10) str_append(time_buf, sizeof(time_buf), "0");
    utoa(time.minute, num, 10);
    str_append(time_buf, sizeof(time_buf), num);
    str_append(time_buf, sizeof(time_buf), suffix);

    const char *status = "WiFi  Vol";
    int status_x = (int)comp_width - 120;
    fb_draw_string(status_x, 8, status, theme->text_muted, theme->dock_tint);
    fb_draw_string((int)comp_width - 70, 8, time_buf, theme->text, theme->dock_tint);
}

static void draw_icon_scaled(const uint8_t *pixels, int src_size, int x, int y, int size)
{
    for (int py = 0; py < size; py++) {
        int sy = (py * src_size) / size;
        for (int px = 0; px < size; px++) {
            int sx = (px * src_size) / size;
            int idx = (sy * src_size + sx) * 4;
            uint8_t r = pixels[idx + 0];
            uint8_t g = pixels[idx + 1];
            uint8_t b = pixels[idx + 2];
            uint8_t a = pixels[idx + 3];
            if (a > 16) {
                fb_put_pixel(x + px, y + py, RGB(r, g, b));
            }
        }
    }
}

static void draw_image_scaled(const uint8_t *pixels, int src_w, int src_h, int x, int y, int w, int h)
{
    for (int py = 0; py < h; py++) {
        int sy = (py * src_h) / h;
        for (int px = 0; px < w; px++) {
            int sx = (px * src_w) / w;
            int idx = (sy * src_w + sx) * 4;
            uint8_t r = pixels[idx + 0];
            uint8_t g = pixels[idx + 1];
            uint8_t b = pixels[idx + 2];
            uint8_t a = pixels[idx + 3];
            if (a > 16) {
                fb_put_pixel(x + px, y + py, RGB(r, g, b));
            }
        }
    }
}

static void draw_dock(uint64_t now_ms)
{
    int count = app_registry_count();
    if (count == 0) return;

    SettingsState *settings = settings_get();
    int icon_base = settings->dock_size;
    int icon_max = settings->dock_magnify;

    int spacing = 12;
    int total_width = count * icon_base + (count - 1) * spacing + 40;
    if (total_width < 240) total_width = 240;

    int dock_w = total_width;
    int dock_h = DOCK_HEIGHT;
    int dock_x = ((int)comp_width - dock_w) / 2;
    int dock_y = (int)comp_height - dock_h - 20;
    int r = 20;

    draw_rounded_rect_blend(dock_x, dock_y, dock_w, dock_h, r, theme->dock_tint, 120);

    int base_x = dock_x + 20;
    for (int i = 0; i < count; i++) {
        AppInfo *app = app_registry_get(i);
        int icon_center_x = base_x + i * (icon_base + spacing) + icon_base / 2;
        int icon_center_y = dock_y + 36;
        int dx = cursor_x - icon_center_x;
        int dy = cursor_y - icon_center_y;
        int dist_sq = dx * dx + dy * dy;
        int radius_sq = DOCK_HOVER_RADIUS * DOCK_HOVER_RADIUS;

        int size = icon_base;
        if (dist_sq < radius_sq) {
            int t = radius_sq - dist_sq;
            int scale = (t * 256) / radius_sq;
            size = icon_base + (((icon_max - icon_base) * scale) / 256);
        }

        int bounce = 0;
        if (app->bounce_until > now_ms) {
            int phase = (int)(((app->bounce_until - now_ms) / 80) % 5);
            int bounce_steps[] = { 0, -6, -10, -6, 0 };
            bounce = bounce_steps[phase];
        }

        int icon_x = icon_center_x - size / 2;
        int lift = (size - icon_base) / 3;
        int icon_y = icon_center_y - size / 2 + bounce - lift;

        if (app->icon.valid) {
            draw_icon_scaled(app->icon.pixels, BUNDLE_ICON_SIZE, icon_x, icon_y, size);
        } else {
            fb_fill_rect(icon_x, icon_y, size, size, theme->accent);
        }

        if (app->running) {
            fb_fill_rect(icon_center_x - 4, dock_y + dock_h - 8, 8, 3, theme->accent);
        }
    }
}

static void draw_spotlight(int anim)
{
    int x = ((int)comp_width - SPOTLIGHT_WIDTH) / 2;
    int y = 120 - overlay_offset(anim, 24);
    int r = 16;

    uint8_t panel_alpha = overlay_alpha(80, anim);
    uint8_t text_alpha = overlay_alpha(220, anim);
    draw_shadow(x, y, SPOTLIGHT_WIDTH, SPOTLIGHT_HEIGHT, r);
    draw_rounded_rect_blend(x, y, SPOTLIGHT_WIDTH, SPOTLIGHT_HEIGHT, r, theme->glass_aqua, panel_alpha);
    fb_draw_string(x + 18, y + 20, spotlight_query[0] ? spotlight_query : "Search", theme->text,
                   blend(theme->glass_aqua, theme->dock_tint, 60));

    int list_y = y + SPOTLIGHT_HEIGHT + 8;
    int list_h = spotlight_count * 28 + 12;
    if (list_h > 0) {
        draw_rounded_rect_blend(x, list_y, SPOTLIGHT_WIDTH, list_h, 14, theme->dock_tint, panel_alpha + 20);
        for (int i = 0; i < spotlight_count; i++) {
            int row_y = list_y + 8 + i * 28;
            if (i == spotlight_selected) {
                draw_rounded_rect_blend(x + 6, row_y - 2, SPOTLIGHT_WIDTH - 12, 24, 10,
                                        theme->accent, panel_alpha);
            }
            fb_draw_string(x + 18, row_y + 6, spotlight_results[i].title,
                           theme->text, theme->dock_tint);
            fb_draw_string(x + 240, row_y + 6, spotlight_results[i].subtitle,
                           theme->text_muted, theme->dock_tint);
        }
    } else if (spotlight_query[0]) {
        draw_rounded_rect_blend(x, list_y, SPOTLIGHT_WIDTH, 36, 14, theme->dock_tint, panel_alpha + 20);
        fb_draw_string(x + 18, list_y + 10, "No results", theme->text_muted, theme->dock_tint);
    }
}

static void draw_launchpad(int anim)
{
    int blur_radius = theme->glass.blur_px[2];
    for (uint32_t y = 0; y < comp_height; y++) {
        for (uint32_t x = 0; x < comp_width; x++) {
            Color blurred = blur_sample((int)x, (int)y, blur_radius);
            Color blended = blend(blurred, theme->dock_tint, 80);
            Color base = fb_get_pixel((int)x, (int)y);
            fb_put_pixel((int)x, (int)y, blend(base, blended, overlay_alpha(220, anim)));
        }
    }

    int count = app_registry_count();
    int grid_w = LAUNCHPAD_COLS * LAUNCHPAD_ICON_SIZE + (LAUNCHPAD_COLS - 1) * 40;
    int grid_h = LAUNCHPAD_ROWS * (LAUNCHPAD_ICON_SIZE + 28) + (LAUNCHPAD_ROWS - 1) * 20;
    int start_x = ((int)comp_width - grid_w) / 2;
    int start_y = ((int)comp_height - grid_h) / 2;

    int idx = 0;
    for (int row = 0; row < LAUNCHPAD_ROWS; row++) {
        for (int col = 0; col < LAUNCHPAD_COLS; col++) {
            if (idx >= count) return;
            AppInfo *app = app_registry_get(idx);
            int ix = start_x + col * (LAUNCHPAD_ICON_SIZE + 40);
            int iy = start_y + row * (LAUNCHPAD_ICON_SIZE + 48);

            if (app->icon.valid) {
                draw_icon_scaled(app->icon.pixels, BUNDLE_ICON_SIZE, ix, iy, LAUNCHPAD_ICON_SIZE);
            } else {
                fb_fill_rect(ix, iy, LAUNCHPAD_ICON_SIZE, LAUNCHPAD_ICON_SIZE, theme->accent);
            }
            fb_draw_string(ix - 4, iy + LAUNCHPAD_ICON_SIZE + 10, app->name,
                           theme->text, theme->dock_tint);
            idx++;
        }
    }
}

static void draw_control_center(int anim)
{
    int x = (int)comp_width - CONTROL_CENTER_WIDTH - 20;
    int y = MENU_BAR_HEIGHT + 10 - overlay_offset(anim, 24);
    int r = 16;

    draw_shadow(x, y, CONTROL_CENTER_WIDTH, CONTROL_CENTER_HEIGHT, r);
    draw_rounded_rect_blend(x, y, CONTROL_CENTER_WIDTH, CONTROL_CENTER_HEIGHT, r,
                            theme->dock_tint, overlay_alpha(140, anim));

    SettingsState *settings = settings_get();

    fb_draw_string(x + 16, y + 12, "Control Center", theme->text, theme->dock_tint);

    int toggle_y = y + 44;
    fb_draw_string(x + 16, toggle_y, settings->wifi_enabled ? "Wi-Fi: On" : "Wi-Fi: Off",
                   theme->text, theme->dock_tint);
    fb_draw_string(x + 16, toggle_y + 22,
                   settings->bluetooth_enabled ? "Bluetooth: On" : "Bluetooth: Off",
                   theme->text, theme->dock_tint);
    fb_draw_string(x + 16, toggle_y + 44,
                   settings->dark_mode ? "Appearance: Dark" : "Appearance: Light",
                   theme->text, theme->dock_tint);

    fb_draw_string(x + 16, toggle_y + 76, "Volume", theme->text_muted, theme->dock_tint);
    fb_fill_rect(x + 16, toggle_y + 92, settings->volume, 6, theme->accent);

    fb_draw_string(x + 16, toggle_y + 112, "Brightness", theme->text_muted, theme->dock_tint);
    fb_fill_rect(x + 16, toggle_y + 128, settings->brightness, 6, theme->accent_soft);
}

static void draw_mission_control(int anim)
{
    draw_rounded_rect_blend(0, 0, (int)comp_width, (int)comp_height, 0,
                            theme->dock_tint, overlay_alpha(140, anim));
    fb_draw_string(((int)comp_width - 220) / 2, 120,
                   "Mission Control (Phase 2)", theme->text, theme->dock_tint);
}

static void draw_app_switcher(int anim)
{
    int count = app_registry_count();
    if (count == 0) return;

    int running[APP_REGISTRY_MAX];
    int running_count = 0;
    for (int i = 0; i < count; i++) {
        AppInfo *app = app_registry_get(i);
        if (app && app->running) {
            running[running_count++] = i;
        }
    }
    if (running_count == 0) return;

    int width = 360;
    int height = 120;
    int x = ((int)comp_width - width) / 2;
    int y = 160 - overlay_offset(anim, 18);

    draw_shadow(x, y, width, height, 16);
    draw_rounded_rect_blend(x, y, width, height, 16, theme->dock_tint, overlay_alpha(140, anim));

    int base_x = x + 20;
    int icon_y = y + 34;
    int shown = MIN(running_count, 5);
    for (int i = 0; i < shown; i++) {
        AppInfo *app = app_registry_get(running[i]);
        int icon_x = base_x + i * 64;
        int size = 40;
        if (i == app_switcher_index) {
            draw_rounded_rect_blend(icon_x - 6, icon_y - 6, size + 12, size + 12, 12, theme->accent, 70);
        }
        if (app->icon.valid) {
            draw_icon_scaled(app->icon.pixels, BUNDLE_ICON_SIZE, icon_x, icon_y, size);
        } else {
            fb_fill_rect(icon_x, icon_y, size, size, theme->accent);
        }
    }
}

static void draw_cursor(int x, int y)
{
    static const uint8_t cursor[12] = {
        0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC,
        0xFE, 0xF0, 0xD8, 0x8C, 0x0C, 0x06
    };

    for (int row = 0; row < 12; row++) {
        for (int col = 0; col < 8; col++) {
            if (cursor[row] & (0x80 >> col)) {
                fb_put_pixel(x + col, y + row, COLOR_BLACK);
            }
        }
    }
    for (int row = 1; row < 11; row++) {
        for (int col = 1; col < 7; col++) {
            uint8_t inner = cursor[row] & (0x80 >> col);
            uint8_t left = cursor[row] & (0x80 >> (col - 1));
            if (inner && left) {
                fb_put_pixel(x + col, y + row, COLOR_WHITE);
            }
        }
    }
}

static void spotlight_refresh(void)
{
    spotlight_count = search_index_query(spotlight_query, spotlight_results, SEARCH_RESULTS_MAX);
    if (spotlight_selected >= spotlight_count) {
        spotlight_selected = spotlight_count > 0 ? 0 : 0;
    }
}

static void overlay_set(OverlayMode mode)
{
    overlay = mode;
    spotlight_active = (mode == OVERLAY_SPOTLIGHT);
    launchpad_active = (mode == OVERLAY_LAUNCHPAD);
    control_center_active = (mode == OVERLAY_CONTROL_CENTER);
    mission_control_active = (mode == OVERLAY_MISSION_CONTROL);
    app_switcher_active = (mode == OVERLAY_APP_SWITCHER);
}

static void launch_app_index(int index, uint64_t now_ms)
{
    if (app_registry_launch(index) == 0) {
        AppInfo *app = app_registry_get(index);
        if (app) {
            strncpy(active_app_name, app->name, sizeof(active_app_name) - 1);
            app->bounce_until = now_ms + 600;
            AppType type = app_type_from_bundle(app->bundle.manifest.bundle_id);
            if (type != APP_DEMO) {
                app_open_window(type, app->name);
            }
        }
    }
}

static uint8_t overlay_alpha(uint8_t base, int anim)
{
    if (anim <= 0) return 0;
    if (anim >= 1000) return base;
    return (uint8_t)((base * anim) / 1000);
}

static int overlay_offset(int anim, int max_offset)
{
    if (anim <= 0) return max_offset;
    if (anim >= 1000) return 0;
    return (max_offset * (1000 - anim)) / 1000;
}

static size_t str_append(char *dest, size_t size, const char *src)
{
    size_t len = strlen(dest);
    size_t i = 0;
    if (len >= size) return len;
    while (src[i] && (len + i + 1) < size) {
        dest[len + i] = src[i];
        i++;
    }
    dest[len + i] = '\0';
    return len + i;
}

static const char *str_find(const char *haystack, const char *needle)
{
    if (!haystack || !needle || needle[0] == '\0') return NULL;
    size_t nlen = strlen(needle);
    for (size_t i = 0; haystack[i]; i++) {
        if (strncmp(haystack + i, needle, nlen) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

static char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

static bool str_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || needle[0] == '\0') return false;

    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return false;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (ascii_lower(haystack[i + j]) != ascii_lower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static bool preview_load_thumbnail(const char *path, uint8_t *out)
{
    if (!path || !out) return false;

    VfsFile *file = vfs_open(path, VFS_O_READ);
    if (!file) return false;

    uint32_t header[2];
    ssize_t header_read = vfs_read(file, header, sizeof(header));
    if (header_read != (ssize_t)sizeof(header)) {
        vfs_close(file);
        return false;
    }

    uint32_t w = header[0];
    uint32_t h = header[1];
    uint64_t size = (uint64_t)w * (uint64_t)h * 4;
    if (size > PREVIEW_RAW_MAX) {
        vfs_close(file);
        return false;
    }

    static uint8_t raw[PREVIEW_RAW_MAX];
    ssize_t read_size = vfs_read(file, raw, size);
    vfs_close(file);
    if (read_size != (ssize_t)size) return false;

    for (int y = 0; y < PREVIEW_THUMB_H; y++) {
        int sy = (int)((y * h) / PREVIEW_THUMB_H);
        for (int x = 0; x < PREVIEW_THUMB_W; x++) {
            int sx = (int)((x * w) / PREVIEW_THUMB_W);
            uint32_t src = (sy * w + sx) * 4;
            uint32_t dst = (y * PREVIEW_THUMB_W + x) * 4;
            out[dst + 0] = raw[src + 0];
            out[dst + 1] = raw[src + 1];
            out[dst + 2] = raw[src + 2];
            out[dst + 3] = raw[src + 3];
        }
    }

    return true;
}

static void normalize_path(char *path, size_t size)
{
    if (!path || size == 0) return;

    char temp[256];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    const char *parts[32];
    int count = 0;
    char *p = temp;
    if (*p == '/') p++;

    while (*p && count < 32) {
        char *start = p;
        while (*p && *p != '/') p++;
        if (*p) {
            *p = '\0';
            p++;
        }

        if (strcmp(start, "") == 0 || strcmp(start, ".") == 0) {
            continue;
        }
        if (strcmp(start, "..") == 0) {
            if (count > 0) count--;
            continue;
        }
        parts[count++] = start;
    }

    path[0] = '\0';
    str_append(path, size, "/");
    for (int i = 0; i < count; i++) {
        str_append(path, size, parts[i]);
        if (i + 1 < count) {
            str_append(path, size, "/");
        }
    }
}

static int parse_int(const char *s, int *consumed)
{
    int value = 0;
    int i = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        value = value * 10 + (s[i] - '0');
        i++;
    }
    if (consumed) *consumed = i;
    return value;
}

static void parse_field(const char **cursor, char *out, size_t size)
{
    if (!cursor || !*cursor || !out || size == 0) return;
    size_t i = 0;
    const char *p = *cursor;
    while (*p && *p != '|' && i + 1 < size) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    if (*p == '|') p++;
    *cursor = p;
}

static bool is_leap_year(int year)
{
    if (year % 400 == 0) return true;
    if (year % 100 == 0) return false;
    return (year % 4 == 0);
}

static int days_in_month(int year, int month)
{
    static const int days[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (month == 2 && is_leap_year(year)) return 29;
    if (month < 1 || month > 12) return 30;
    return days[month - 1];
}

static int weekday_of_date(int year, int month, int day)
{
    int y = year;
    int m = month;
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int k = y % 100;
    int j = y / 100;
    int h = (day + (13 * (m + 1)) / 5 + k + (k / 4) + (j / 4) + (5 * j)) % 7;
    int d = ((h + 6) % 7); /* 0=Sunday */
    return d;
}

static void calendar_add_event(CalendarState *cal, CalendarEvent *event)
{
    if (!cal || !event) return;
    if (cal->event_count >= CALENDAR_MAX_EVENTS) return;
    cal->events[cal->event_count++] = *event;
}

static bool calendar_load(CalendarState *cal)
{
    if (!cal) return false;
    cal->event_count = 0;

    VfsFile *file = vfs_open("/Users/guest/Documents/Calendar.txt", VFS_O_READ);
    if (!file) {
        cal->loaded = true;
        return false;
    }

    char buffer[256];
    ssize_t bytes;
    char line[128];
    int line_len = 0;

    while ((bytes = vfs_read(file, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < bytes; i++) {
            char c = buffer[i];
            if (c == '\n' || line_len >= (int)sizeof(line) - 1) {
                line[line_len] = '\0';
                line_len = 0;

                if (line[0] == '\0') continue;

                CalendarEvent ev = {0};
                const char *p = line;
                int n;
                ev.year = parse_int(p, &n); p += n;
                if (*p == '-') p++;
                ev.month = parse_int(p, &n); p += n;
                if (*p == '-') p++;
                ev.day = parse_int(p, &n); p += n;
                if (*p == '|') p++;

                if (strncmp(p, "all-day", 7) == 0) {
                    ev.all_day = true;
                    ev.hour = 0;
                    ev.minute = 0;
                    p += 7;
                } else {
                    ev.hour = parse_int(p, &n); p += n;
                    if (*p == ':') p++;
                    ev.minute = parse_int(p, &n); p += n;
                }

                if (*p == '|') p++;
                parse_field(&p, ev.title, sizeof(ev.title));
                parse_field(&p, ev.location, sizeof(ev.location));
                parse_field(&p, ev.notes, sizeof(ev.notes));

                calendar_add_event(cal, &ev);
            } else {
                line[line_len++] = c;
            }
        }
    }
    vfs_close(file);
    cal->loaded = true;
    return true;
}

static void calendar_save(CalendarState *cal)
{
    if (!cal) return;
    VfsFile *file = vfs_open("/Users/guest/Documents/Calendar.txt",
                             VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    if (!file) return;

    for (int i = 0; i < cal->event_count; i++) {
        CalendarEvent *ev = &cal->events[i];
        char line[160];
        line[0] = '\0';
        char num[16];

        utoa(ev->year, num, 10);
        str_append(line, sizeof(line), num);
        str_append(line, sizeof(line), "-");
        if (ev->month < 10) str_append(line, sizeof(line), "0");
        utoa(ev->month, num, 10);
        str_append(line, sizeof(line), num);
        str_append(line, sizeof(line), "-");
        if (ev->day < 10) str_append(line, sizeof(line), "0");
        utoa(ev->day, num, 10);
        str_append(line, sizeof(line), num);
        str_append(line, sizeof(line), "|");

        if (ev->all_day) {
            str_append(line, sizeof(line), "all-day");
        } else {
            if (ev->hour < 10) str_append(line, sizeof(line), "0");
            utoa(ev->hour, num, 10);
            str_append(line, sizeof(line), num);
            str_append(line, sizeof(line), ":");
            if (ev->minute < 10) str_append(line, sizeof(line), "0");
            utoa(ev->minute, num, 10);
            str_append(line, sizeof(line), num);
        }

        str_append(line, sizeof(line), "|");
        str_append(line, sizeof(line), ev->title);
        str_append(line, sizeof(line), "|");
        str_append(line, sizeof(line), ev->location);
        str_append(line, sizeof(line), "|");
        str_append(line, sizeof(line), ev->notes);
        str_append(line, sizeof(line), "\n");
        vfs_write(file, line, strlen(line));
    }
    vfs_close(file);
}

static void calendar_shift_month(CalendarState *cal, int delta)
{
    if (!cal) return;
    int m = cal->month + delta;
    int y = cal->year;
    while (m < 1) { m += 12; y--; }
    while (m > 12) { m -= 12; y++; }
    cal->month = m;
    cal->year = y;
    int dim = days_in_month(y, m);
    if (cal->selected_day > dim) cal->selected_day = dim;
}

static void calendar_add_quick_event(CalendarState *cal)
{
    if (!cal || cal->event_count >= CALENDAR_MAX_EVENTS) return;
    CalendarEvent ev = {0};
    ev.year = cal->year;
    ev.month = cal->month;
    ev.day = cal->selected_day;
    ev.hour = 9;
    ev.minute = 0;
    ev.all_day = false;
    strncpy(ev.title, "New Event", sizeof(ev.title) - 1);
    calendar_add_event(cal, &ev);
    cal->selected_event = cal->event_count - 1;
    calendar_save(cal);
}

static void calendar_delete_selected(CalendarState *cal)
{
    if (!cal) return;
    if (cal->selected_event < 0 || cal->selected_event >= cal->event_count) return;

    for (int i = cal->selected_event + 1; i < cal->event_count; i++) {
        cal->events[i - 1] = cal->events[i];
    }
    cal->event_count--;
    if (cal->event_count <= 0) {
        cal->selected_event = -1;
    } else if (cal->selected_event >= cal->event_count) {
        cal->selected_event = cal->event_count - 1;
    }
    calendar_save(cal);
}

static void calendar_start_edit(CalendarState *cal, int field)
{
    if (!cal) return;
    if (cal->selected_event < 0 || cal->selected_event >= cal->event_count) return;
    CalendarEvent *ev = &cal->events[cal->selected_event];
    cal->edit_field = field;
    cal->edit_mode = true;

    if (field == 0) {
        strncpy(cal->edit_buffer, ev->title, sizeof(cal->edit_buffer) - 1);
    } else if (field == 1) {
        if (ev->all_day) {
            strncpy(cal->edit_buffer, "all-day", sizeof(cal->edit_buffer) - 1);
        } else {
            char buf[16];
            buf[0] = '\0';
            if (ev->hour < 10) str_append(buf, sizeof(buf), "0");
            char num[8];
            utoa(ev->hour, num, 10);
            str_append(buf, sizeof(buf), num);
            str_append(buf, sizeof(buf), ":");
            if (ev->minute < 10) str_append(buf, sizeof(buf), "0");
            utoa(ev->minute, num, 10);
            str_append(buf, sizeof(buf), num);
            strncpy(cal->edit_buffer, buf, sizeof(cal->edit_buffer) - 1);
        }
    } else if (field == 2) {
        strncpy(cal->edit_buffer, ev->location, sizeof(cal->edit_buffer) - 1);
    } else if (field == 3) {
        strncpy(cal->edit_buffer, ev->notes, sizeof(cal->edit_buffer) - 1);
    }
}

static bool calendar_apply_edit(CalendarState *cal)
{
    if (!cal) return false;
    if (cal->selected_event < 0 || cal->selected_event >= cal->event_count) return false;
    CalendarEvent *ev = &cal->events[cal->selected_event];
    cal->edit_error = false;

    if (cal->edit_field == 0) {
        strncpy(ev->title, cal->edit_buffer, sizeof(ev->title) - 1);
    } else if (cal->edit_field == 1) {
        if (strcmp(cal->edit_buffer, "all-day") == 0) {
            ev->all_day = true;
            ev->hour = 0;
            ev->minute = 0;
        } else {
            int h = 0;
            int m = 0;
            int consumed = 0;
            h = parse_int(cal->edit_buffer, &consumed);
            const char *p = cal->edit_buffer + consumed;
            if (*p == ':') {
                p++;
                m = parse_int(p, &consumed);
                if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
                    ev->hour = h;
                    ev->minute = m;
                    ev->all_day = false;
                } else {
                    cal->edit_error = true;
                    return false;
                }
            } else {
                cal->edit_error = true;
                return false;
            }
        }
    } else if (cal->edit_field == 2) {
        strncpy(ev->location, cal->edit_buffer, sizeof(ev->location) - 1);
    } else if (cal->edit_field == 3) {
        strncpy(ev->notes, cal->edit_buffer, sizeof(ev->notes) - 1);
    }

    calendar_save(cal);
    return true;
}

static bool load_system_icon(const char *path, uint8_t *out)
{
    VfsFile *file = vfs_open(path, VFS_O_READ);
    if (!file) return false;
    ssize_t read_size = vfs_read(file, out, BUNDLE_ICON_BYTES);
    vfs_close(file);
    return read_size == BUNDLE_ICON_BYTES;
}

static void rebuild_app_window_index(void)
{
    for (int i = 0; i < APP_COUNT; i++) {
        app_window_index[i] = -1;
    }
    for (int i = 0; i < window_count; i++) {
        AppType type = app_states[i].type;
        if (type >= 0 && type < APP_COUNT) {
            app_window_index[type] = i;
        }
    }
}

static void finder_refresh(FinderState *state)
{
    if (!state) return;

    state->entry_count = 0;
    VfsDir *dir = vfs_opendir(state->path[0] ? state->path : "/");
    if (!dir) return;

    VfsDirEntry entry;
    while (vfs_readdir(dir, &entry) == 0 && state->entry_count < FINDER_MAX_ENTRIES) {
        if (state->search[0]) {
            if (!str_contains_ci(entry.name, state->search)) {
                continue;
            }
        }
        FinderEntry *dst = &state->entries[state->entry_count++];
        strncpy(dst->name, entry.name, sizeof(dst->name) - 1);
        dst->type = entry.type;
        dst->size = entry.size;
    }
    vfs_closedir(dir);
    state->needs_refresh = false;
}

static void finder_set_path(FinderState *state, const char *path)
{
    if (!state || !path) return;
    if (strcmp(state->path, path) == 0) return;
    strncpy(state->path, path, sizeof(state->path) - 1);
    state->needs_refresh = true;
    state->selected = -1;
    state->search[0] = '\0';
    state->rename_mode = false;
    state->rename_buffer[0] = '\0';
    state->preview_ready = false;
    state->preview_path[0] = '\0';
    state->drag_active = false;
    state->drag_path[0] = '\0';
    state->drag_hover_index = -1;

    if (state->history_count < 8) {
        strncpy(state->history[state->history_count++], path, sizeof(state->history[0]) - 1);
        state->history_pos = state->history_count - 1;
    } else {
        for (int i = 1; i < 8; i++) {
            strncpy(state->history[i - 1], state->history[i], sizeof(state->history[0]) - 1);
        }
        strncpy(state->history[7], path, sizeof(state->history[0]) - 1);
        state->history_pos = 7;
    }
}

static void finder_back(FinderState *state)
{
    if (!state) return;
    if (state->history_pos > 0) {
        state->history_pos--;
        strncpy(state->path, state->history[state->history_pos], sizeof(state->path) - 1);
        state->needs_refresh = true;
    }
}

static void finder_forward(FinderState *state)
{
    if (!state) return;
    if (state->history_pos + 1 < state->history_count) {
        state->history_pos++;
        strncpy(state->path, state->history[state->history_pos], sizeof(state->path) - 1);
        state->needs_refresh = true;
    }
}

static void finder_update_preview(FinderState *state)
{
    if (!state || state->selected < 0 || state->selected >= state->entry_count) {
        state->preview_ready = false;
        return;
    }

    FinderEntry *entry = &state->entries[state->selected];
    char full[256];
    vfs_join_path(full, sizeof(full), state->path, entry->name);

    if (strcmp(state->preview_path, full) == 0 && state->preview_ready) {
        return;
    }

    strncpy(state->preview_path, full, sizeof(state->preview_path) - 1);
    strncpy(state->preview_name, entry->name, sizeof(state->preview_name) - 1);
    state->preview_size = entry->size;

    if (entry->type == VFS_TYPE_DIR) {
        strcpy(state->preview_type, "Folder");
    } else if (entry->type == VFS_TYPE_BUNDLE) {
        strcpy(state->preview_type, "App");
    } else {
        strcpy(state->preview_type, "File");
    }

    for (int i = 0; i < 3; i++) {
        state->preview_lines[i][0] = '\0';
    }

    if (entry->type == VFS_TYPE_FILE) {
        VfsFile *file = vfs_open(full, VFS_O_READ);
        if (file) {
            char buf[192];
            ssize_t bytes = vfs_read(file, buf, sizeof(buf) - 1);
            vfs_close(file);
            if (bytes > 0) {
                buf[bytes] = '\0';
                int line = 0;
                int col = 0;
                bool printable = true;
                for (int i = 0; i < bytes; i++) {
                    char c = buf[i];
                    if ((c < 9 || (c > 13 && c < 32)) && c != '\0') {
                        printable = false;
                        break;
                    }
                }
                if (printable) {
                    for (int i = 0; i < bytes && line < 3; i++) {
                        char c = buf[i];
                        if (c == '\n') {
                            state->preview_lines[line][col] = '\0';
                            line++;
                            col = 0;
                            continue;
                        }
                        if (col < (int)sizeof(state->preview_lines[line]) - 1) {
                            state->preview_lines[line][col++] = c;
                        }
                    }
                    if (line < 3) {
                        state->preview_lines[line][col] = '\0';
                    }
                }
            }
        }
    }

    state->preview_ready = true;
}

static int copy_file_path(const char *src, const char *dst)
{
    VfsFile *in = vfs_open(src, VFS_O_READ);
    if (!in) return -1;
    VfsFile *out = vfs_open(dst, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    if (!out) {
        vfs_close(in);
        return -1;
    }

    char buffer[128];
    ssize_t bytes;
    while ((bytes = vfs_read(in, buffer, sizeof(buffer))) > 0) {
        vfs_write(out, buffer, bytes);
    }
    vfs_close(in);
    vfs_close(out);
    return 0;
}

static void terminal_append_line(TerminalState *term, const char *text)
{
    if (!term || !text) return;
    if (term->line_count >= TERMINAL_MAX_LINES) {
        for (int i = 1; i < TERMINAL_MAX_LINES; i++) {
            strncpy(term->lines[i - 1], term->lines[i], TERMINAL_LINE_MAX - 1);
        }
        term->line_count = TERMINAL_MAX_LINES - 1;
    }
    strncpy(term->lines[term->line_count++], text, TERMINAL_LINE_MAX - 1);
}

static void terminal_history_push(TerminalState *term, const char *cmd)
{
    if (!term || !cmd || cmd[0] == '\0') return;
    if (term->history_count < 16) {
        strncpy(term->history[term->history_count++], cmd, TERMINAL_LINE_MAX - 1);
    } else {
        for (int i = 1; i < 16; i++) {
            strncpy(term->history[i - 1], term->history[i], TERMINAL_LINE_MAX - 1);
        }
        strncpy(term->history[15], cmd, TERMINAL_LINE_MAX - 1);
    }
    term->history_pos = term->history_count;
}

static void terminal_history_apply(TerminalState *term, int direction)
{
    if (!term || term->history_count == 0) return;
    int pos = term->history_pos + direction;
    if (pos < 0) pos = 0;
    if (pos > term->history_count) pos = term->history_count;
    term->history_pos = pos;

    if (term->history_pos >= term->history_count) {
        term->input_len = 0;
        term->input[0] = '\0';
        return;
    }

    strncpy(term->input, term->history[term->history_pos], TERMINAL_LINE_MAX - 1);
    term->input_len = (int)strlen(term->input);
}

static void terminal_make_prompt(const TerminalState *term, char *out, size_t size)
{
    const char *home = "/Users/guest";
    char path[128];
    if (strncmp(term->cwd, home, strlen(home)) == 0) {
        strcpy(path, "~");
        strncpy(path + 1, term->cwd + strlen(home), sizeof(path) - 2);
    } else {
        strncpy(path, term->cwd, sizeof(path) - 1);
    }

    out[0] = '\0';
    str_append(out, size, "guest@ojjyos:");
    str_append(out, size, path[0] ? path : "/");
    str_append(out, size, " %");
}

static void terminal_resolve_path(const TerminalState *term, const char *input, char *out, size_t size)
{
    if (!input || !out || size == 0) return;

    if (input[0] == '/') {
        strncpy(out, input, size - 1);
        out[size - 1] = '\0';
        normalize_path(out, size);
        return;
    }

    if (strcmp(input, "~") == 0) {
        strncpy(out, "/Users/guest", size - 1);
        out[size - 1] = '\0';
        normalize_path(out, size);
        return;
    }

    if (strncmp(input, "~/", 2) == 0) {
        out[0] = '\0';
        str_append(out, size, "/Users/guest/");
        str_append(out, size, input + 2);
        normalize_path(out, size);
        return;
    }

    if (term->cwd[0]) {
        vfs_join_path(out, size, term->cwd, input);
    } else {
        out[0] = '\0';
        str_append(out, size, "/");
        str_append(out, size, input);
    }

    normalize_path(out, size);
}

static void terminal_ls(TerminalState *term, const char *path)
{
    VfsDir *dir = vfs_opendir(path);
    if (!dir) {
        terminal_append_line(term, "ls: cannot open");
        return;
    }

    VfsDirEntry entry;
    while (vfs_readdir(dir, &entry) == 0) {
        terminal_append_line(term, entry.name);
    }
    vfs_closedir(dir);
}

static void terminal_cat(TerminalState *term, const char *path)
{
    VfsFile *file = vfs_open(path, VFS_O_READ);
    if (!file) {
        terminal_append_line(term, "cat: cannot open");
        return;
    }

    char buffer[64];
    ssize_t bytes = vfs_read(file, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        terminal_append_line(term, buffer);
    }
    vfs_close(file);
}

static void terminal_exec(TerminalState *term, const char *cmd)
{
    if (!term || !cmd) return;

    if (strcmp(cmd, "help") == 0) {
        terminal_append_line(term, "Commands: ls, cd, pwd, cat, echo, touch, mkdir, open, clear");
        return;
    }

    if (strcmp(cmd, "clear") == 0) {
        term->line_count = 0;
        return;
    }

    if (strcmp(cmd, "pwd") == 0) {
        terminal_append_line(term, term->cwd[0] ? term->cwd : "/");
        return;
    }

    if (strcmp(cmd, "cd") == 0) {
        strncpy(term->cwd, "/Users/guest", sizeof(term->cwd) - 1);
        return;
    }

    if (strncmp(cmd, "cd ", 3) == 0) {
        char path[256];
        terminal_resolve_path(term, cmd + 3, path, sizeof(path));
        if (vfs_isdir(path)) {
            strncpy(term->cwd, path, sizeof(term->cwd) - 1);
        } else {
            terminal_append_line(term, "cd: no such directory");
        }
        return;
    }

    if (strncmp(cmd, "ls", 2) == 0) {
        const char *arg = cmd + 2;
        while (*arg == ' ') arg++;
        char path[256];
        if (*arg) {
            terminal_resolve_path(term, arg, path, sizeof(path));
        } else {
            strncpy(path, term->cwd[0] ? term->cwd : "/", sizeof(path) - 1);
        }
        terminal_ls(term, path);
        return;
    }

    if (strncmp(cmd, "cat ", 4) == 0) {
        char path[256];
        terminal_resolve_path(term, cmd + 4, path, sizeof(path));
        terminal_cat(term, path);
        return;
    }

    if (strncmp(cmd, "touch ", 6) == 0) {
        char path[256];
        terminal_resolve_path(term, cmd + 6, path, sizeof(path));
        VfsFile *file = vfs_open(path, VFS_O_CREATE | VFS_O_WRITE);
        if (file) vfs_close(file);
        return;
    }

    if (strncmp(cmd, "rm ", 3) == 0) {
        char path[256];
        terminal_resolve_path(term, cmd + 3, path, sizeof(path));
        if (vfs_unlink(path) != 0) {
            terminal_append_line(term, "rm: failed");
        }
        return;
    }

    if (strncmp(cmd, "mv ", 3) == 0) {
        const char *args = cmd + 3;
        const char *space = str_find(args, " ");
        if (!space) {
            terminal_append_line(term, "mv: missing destination");
            return;
        }
        char src[256];
        char dst[256];
        size_t len = (size_t)(space - args);
        if (len >= sizeof(src)) len = sizeof(src) - 1;
        memcpy(src, args, len);
        src[len] = '\0';
        const char *dst_arg = space + 1;
        terminal_resolve_path(term, src, src, sizeof(src));
        terminal_resolve_path(term, dst_arg, dst, sizeof(dst));
        if (vfs_rename(src, dst) != 0) {
            terminal_append_line(term, "mv: failed");
        }
        return;
    }

    if (strncmp(cmd, "cp ", 3) == 0) {
        const char *args = cmd + 3;
        const char *space = str_find(args, " ");
        if (!space) {
            terminal_append_line(term, "cp: missing destination");
            return;
        }
        char src[256];
        char dst[256];
        size_t len = (size_t)(space - args);
        if (len >= sizeof(src)) len = sizeof(src) - 1;
        memcpy(src, args, len);
        src[len] = '\0';
        const char *dst_arg = space + 1;
        terminal_resolve_path(term, src, src, sizeof(src));
        terminal_resolve_path(term, dst_arg, dst, sizeof(dst));

        VfsFile *in = vfs_open(src, VFS_O_READ);
        if (!in) {
            terminal_append_line(term, "cp: cannot open source");
            return;
        }
        VfsFile *out = vfs_open(dst, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
        if (!out) {
            vfs_close(in);
            terminal_append_line(term, "cp: cannot open destination");
            return;
        }
        char buffer[128];
        ssize_t bytes;
        while ((bytes = vfs_read(in, buffer, sizeof(buffer))) > 0) {
            vfs_write(out, buffer, bytes);
        }
        vfs_close(in);
        vfs_close(out);
        return;
    }

    if (strncmp(cmd, "mkdir ", 6) == 0) {
        char path[256];
        terminal_resolve_path(term, cmd + 6, path, sizeof(path));
        if (vfs_mkdir(path) != 0) {
            terminal_append_line(term, "mkdir: failed");
        }
        return;
    }

    if (strncmp(cmd, "echo ", 5) == 0) {
        const char *redir = str_find(cmd, "> ");
        if (redir) {
            char text[128];
            size_t len = (size_t)(redir - (cmd + 5));
            if (len > sizeof(text) - 1) len = sizeof(text) - 1;
            memcpy(text, cmd + 5, len);
            text[len] = '\0';

            const char *path_arg = redir + 2;
            char path[256];
            terminal_resolve_path(term, path_arg, path, sizeof(path));
            VfsFile *file = vfs_open(path, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
            if (file) {
                vfs_write(file, text, strlen(text));
                vfs_write(file, "\n", 1);
                vfs_close(file);
            }
        } else {
            terminal_append_line(term, cmd + 5);
        }
        return;
    }

    if (strncmp(cmd, "open ", 5) == 0) {
        char path[256];
        terminal_resolve_path(term, cmd + 5, path, sizeof(path));
        if (!vfs_exists(path)) {
            char alt[256];
            vfs_join_path(alt, sizeof(alt), "/Applications", cmd + 5);
            if (vfs_exists(alt)) {
                strncpy(path, alt, sizeof(path) - 1);
            }
        }

        if (vfs_isdir(path) && vfs_is_bundle(path)) {
            Bundle bundle;
            if (bundle_load(path, &bundle) == 0) {
                int app_idx = app_registry_find_by_bundle_id(bundle.manifest.bundle_id);
                if (app_idx >= 0) {
                    launch_app_index(app_idx, timer_get_ticks());
                }
            }
        } else if (vfs_isdir(path)) {
            app_open_window(APP_FINDER, "Finder");
            if (app_window_index[APP_FINDER] >= 0) {
                FinderState *finder = &app_states[app_window_index[APP_FINDER]].finder;
                finder_set_path(finder, path);
            }
        } else if (vfs_isfile(path)) {
            strncpy(last_opened_path, path, sizeof(last_opened_path) - 1);
            int textedit_idx = app_registry_find_by_bundle_id("com.ojjyos.textedit");
            if (textedit_idx >= 0) {
                launch_app_index(textedit_idx, timer_get_ticks());
                if (app_window_index[APP_TEXTEDIT] >= 0) {
                    TextEditState *edit = &app_states[app_window_index[APP_TEXTEDIT]].textedit;
                    textedit_load_file(edit, path);
                }
            }
        }
        return;
    }

    terminal_append_line(term, "Unknown command");
}

static void textedit_clear(TextEditState *edit)
{
    edit->line_count = 1;
    edit->cursor_line = 0;
    edit->cursor_col = 0;
    memset(edit->lines, 0, sizeof(edit->lines));
    edit->dirty = false;
    edit->sel_active = false;
    edit->sel_line = 0;
    edit->sel_start = 0;
    edit->sel_end = 0;
    edit->sel_dragging = false;
}

static void textedit_clear_selection(TextEditState *edit)
{
    if (!edit) return;
    edit->sel_active = false;
}

static void textedit_normalize_selection(TextEditState *edit)
{
    if (!edit || !edit->sel_active) return;
    if (edit->sel_start > edit->sel_end) {
        int tmp = edit->sel_start;
        edit->sel_start = edit->sel_end;
        edit->sel_end = tmp;
    }
}

static void textedit_delete_selection(TextEditState *edit)
{
    if (!edit || !edit->sel_active) return;
    if (edit->sel_line < 0 || edit->sel_line >= edit->line_count) return;

    textedit_normalize_selection(edit);
    int line = edit->sel_line;
    int start = edit->sel_start;
    int end = edit->sel_end;
    int len = (int)strlen(edit->lines[line]);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) {
        textedit_clear_selection(edit);
        return;
    }

    for (int i = end; i <= len; i++) {
        edit->lines[line][start + (i - end)] = edit->lines[line][i];
    }
    edit->cursor_line = line;
    edit->cursor_col = start;
    edit->dirty = true;
    strcpy(edit->status, "Edited");
    textedit_clear_selection(edit);
}

static void textedit_copy_selection(TextEditState *edit)
{
    if (!edit || !edit->sel_active) return;
    textedit_normalize_selection(edit);
    int line = edit->sel_line;
    int start = edit->sel_start;
    int end = edit->sel_end;
    int len = (int)strlen(edit->lines[line]);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return;

    int copy_len = end - start;
    if (copy_len >= (int)sizeof(text_clipboard)) copy_len = (int)sizeof(text_clipboard) - 1;
    memcpy(text_clipboard, &edit->lines[line][start], copy_len);
    text_clipboard[copy_len] = '\0';
}

static void textedit_delete_line(TextEditState *edit, int line)
{
    if (!edit) return;
    if (line < 0 || line >= edit->line_count) return;
    for (int i = line + 1; i < edit->line_count; i++) {
        strncpy(edit->lines[i - 1], edit->lines[i], TEXTEDIT_LINE_MAX - 1);
    }
    if (edit->line_count > 1) {
        edit->line_count--;
    }
    if (edit->cursor_line >= edit->line_count) {
        edit->cursor_line = edit->line_count - 1;
    }
    if (edit->cursor_line < 0) edit->cursor_line = 0;
    edit->cursor_col = 0;
    edit->dirty = true;
    strcpy(edit->status, "Edited");
}

static void textedit_insert_line(TextEditState *edit, int line, const char *text)
{
    if (!edit || !text) return;
    if (edit->line_count >= TEXTEDIT_MAX_LINES) return;
    if (line < 0) line = 0;
    if (line > edit->line_count) line = edit->line_count;

    for (int i = edit->line_count; i > line; i--) {
        strncpy(edit->lines[i], edit->lines[i - 1], TEXTEDIT_LINE_MAX - 1);
    }
    strncpy(edit->lines[line], text, TEXTEDIT_LINE_MAX - 1);
    edit->line_count++;
    edit->dirty = true;
    strcpy(edit->status, "Edited");
}

static void textedit_load_file(TextEditState *edit, const char *path)
{
    if (!edit || !path) return;
    textedit_clear(edit);
    strncpy(edit->file_path, path, sizeof(edit->file_path) - 1);
    strcpy(edit->status, "Opened");

    VfsFile *file = vfs_open(path, VFS_O_READ);
    if (!file) {
        strcpy(edit->status, "Open failed");
        return;
    }

    char buffer[128];
    int line = 0;
    int col = 0;
    bool any = false;
    ssize_t bytes;

    while ((bytes = vfs_read(file, buffer, sizeof(buffer))) > 0) {
        any = true;
        for (int i = 0; i < bytes && line < TEXTEDIT_MAX_LINES; i++) {
            char c = buffer[i];
            if (c == '\n') {
                line++;
                col = 0;
                if (line >= TEXTEDIT_MAX_LINES) break;
                continue;
            }
            if (col < TEXTEDIT_LINE_MAX - 1) {
                edit->lines[line][col++] = c;
                edit->lines[line][col] = '\0';
            }
        }
        if (line >= TEXTEDIT_MAX_LINES) break;
    }
    vfs_close(file);

    if (!any) {
        strcpy(edit->status, "Empty file");
        return;
    }
    edit->line_count = line + 1;
}

static void textedit_save(TextEditState *edit)
{
    if (!edit) return;
    if (edit->file_path[0] == '\0') {
        strncpy(edit->file_path, "/Users/guest/Documents/Untitled.txt",
                sizeof(edit->file_path) - 1);
    }

    VfsFile *file = vfs_open(edit->file_path, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    if (!file) {
        strcpy(edit->status, "Save failed");
        return;
    }

    for (int i = 0; i < edit->line_count; i++) {
        size_t len = strlen(edit->lines[i]);
        if (len > 0) {
            vfs_write(file, edit->lines[i], len);
        }
        vfs_write(file, "\n", 1);
    }
    vfs_close(file);
    strcpy(edit->status, "Saved");
    edit->dirty = false;
    search_index_init();
}

static void terminal_handle_key(TerminalState *term, char ascii, KeyCode key)
{
    if (!term) return;

    if (key == KEY_BACKSPACE) {
        if (term->input_len > 0) {
            term->input_len--;
            term->input[term->input_len] = '\0';
        }
        return;
    }

    if (key == KEY_UP) {
        terminal_history_apply(term, -1);
        return;
    }

    if (key == KEY_DOWN) {
        terminal_history_apply(term, 1);
        return;
    }

    if (key == KEY_ENTER) {
        term->input[term->input_len] = '\0';
        char prompt[TERMINAL_LINE_MAX + 32];
        char prefix[64];
        terminal_make_prompt(term, prefix, sizeof(prefix));
        prompt[0] = '\0';
        str_append(prompt, sizeof(prompt), prefix);
        str_append(prompt, sizeof(prompt), " ");
        str_append(prompt, sizeof(prompt), term->input);
        terminal_append_line(term, prompt);
        terminal_history_push(term, term->input);
        terminal_exec(term, term->input);
        term->input_len = 0;
        term->input[0] = '\0';
        return;
    }

    if (ascii >= 32 && ascii <= 126) {
        if (term->input_len < TERMINAL_LINE_MAX - 1) {
            term->input[term->input_len++] = ascii;
            term->input[term->input_len] = '\0';
        }
    }
}

static void textedit_handle_key(TextEditState *edit, char ascii, KeyCode key, uint8_t modifiers)
{
    if (!edit) return;

    bool shift = (modifiers & INPUT_MOD_SHIFT) != 0;

    if ((modifiers & INPUT_MOD_CTRL) && key == KEY_LEFT) {
        int col = edit->cursor_col;
        if (col > 0) {
            while (col > 0 && edit->lines[edit->cursor_line][col - 1] == ' ') {
                col--;
            }
            while (col > 0 && edit->lines[edit->cursor_line][col - 1] != ' ') {
                col--;
            }
            edit->cursor_col = col;
        }
        return;
    }

    if ((modifiers & INPUT_MOD_CTRL) && key == KEY_RIGHT) {
        int len = (int)strlen(edit->lines[edit->cursor_line]);
        int col = edit->cursor_col;
        while (col < len && edit->lines[edit->cursor_line][col] != ' ') {
            col++;
        }
        while (col < len && edit->lines[edit->cursor_line][col] == ' ') {
            col++;
        }
        edit->cursor_col = col;
        return;
    }

    if (key == KEY_LEFT) {
        if (edit->cursor_col > 0) {
            edit->cursor_col--;
        }
        if (shift) {
            if (!edit->sel_active) {
                edit->sel_active = true;
                edit->sel_line = edit->cursor_line;
                edit->sel_start = edit->cursor_col;
                edit->sel_end = edit->cursor_col + 1;
            } else {
                edit->sel_end = edit->cursor_col;
            }
        } else {
            textedit_clear_selection(edit);
        }
        return;
    }

    if (key == KEY_RIGHT) {
        if (edit->cursor_col < (int)strlen(edit->lines[edit->cursor_line])) {
            edit->cursor_col++;
        }
        if (shift) {
            if (!edit->sel_active) {
                edit->sel_active = true;
                edit->sel_line = edit->cursor_line;
                edit->sel_start = edit->cursor_col - 1;
                edit->sel_end = edit->cursor_col;
            } else {
                edit->sel_end = edit->cursor_col;
            }
        } else {
            textedit_clear_selection(edit);
        }
        return;
    }

    if (key == KEY_UP) {
        if (edit->cursor_line > 0) {
            edit->cursor_line--;
            int max_col = (int)strlen(edit->lines[edit->cursor_line]);
            if (edit->cursor_col > max_col) edit->cursor_col = max_col;
        }
        if (!shift) {
            textedit_clear_selection(edit);
        }
        return;
    }

    if (key == KEY_DOWN) {
        if (edit->cursor_line + 1 < edit->line_count) {
            edit->cursor_line++;
            int max_col = (int)strlen(edit->lines[edit->cursor_line]);
            if (edit->cursor_col > max_col) edit->cursor_col = max_col;
        }
        if (!shift) {
            textedit_clear_selection(edit);
        }
        return;
    }

    if (key == KEY_BACKSPACE) {
        if (edit->sel_active) {
            textedit_delete_selection(edit);
            return;
        }
        if (edit->cursor_col > 0) {
            edit->cursor_col--;
            edit->lines[edit->cursor_line][edit->cursor_col] = '\0';
            edit->dirty = true;
            strcpy(edit->status, "Edited");
        } else if (edit->cursor_line > 0) {
            int prev = edit->cursor_line - 1;
            int prev_len = (int)strlen(edit->lines[prev]);
            if (prev_len < TEXTEDIT_LINE_MAX - 1) {
                str_append(edit->lines[prev], TEXTEDIT_LINE_MAX, edit->lines[edit->cursor_line]);
                textedit_delete_line(edit, edit->cursor_line);
                edit->cursor_line = prev;
                edit->cursor_col = prev_len;
            }
        }
        return;
    }

    if (key == KEY_ENTER) {
        if (edit->line_count < TEXTEDIT_MAX_LINES - 1) {
            char tail[TEXTEDIT_LINE_MAX];
            tail[0] = '\0';
            if (edit->cursor_col < (int)strlen(edit->lines[edit->cursor_line])) {
                strncpy(tail, &edit->lines[edit->cursor_line][edit->cursor_col], sizeof(tail) - 1);
                edit->lines[edit->cursor_line][edit->cursor_col] = '\0';
            }
            edit->cursor_line++;
            edit->cursor_col = 0;
            edit->line_count = MAX(edit->line_count, edit->cursor_line + 1);
            if (tail[0]) {
                textedit_insert_line(edit, edit->cursor_line, tail);
            }
            edit->dirty = true;
            strcpy(edit->status, "Edited");
        }
        textedit_clear_selection(edit);
        return;
    }

    if (ascii >= 32 && ascii <= 126) {
        if (edit->sel_active) {
            textedit_delete_selection(edit);
        }
        if (edit->cursor_col < TEXTEDIT_LINE_MAX - 1) {
            int len = (int)strlen(edit->lines[edit->cursor_line]);
            for (int i = len; i >= edit->cursor_col && i < TEXTEDIT_LINE_MAX - 1; i--) {
                edit->lines[edit->cursor_line][i + 1] = edit->lines[edit->cursor_line][i];
            }
            edit->lines[edit->cursor_line][edit->cursor_col++] = ascii;
            edit->dirty = true;
            strcpy(edit->status, "Edited");
        }
        textedit_clear_selection(edit);
    }
}

static AppType app_type_from_bundle(const char *bundle_id)
{
    if (!bundle_id) return APP_DEMO;
    if (strcmp(bundle_id, "com.ojjyos.finder") == 0) return APP_FINDER;
    if (strcmp(bundle_id, "com.ojjyos.settings") == 0) return APP_SETTINGS;
    if (strcmp(bundle_id, "com.ojjyos.terminal") == 0) return APP_TERMINAL;
    if (strcmp(bundle_id, "com.ojjyos.textedit") == 0) return APP_TEXTEDIT;
    if (strcmp(bundle_id, "com.ojjyos.notes") == 0) return APP_NOTES;
    if (strcmp(bundle_id, "com.ojjyos.preview") == 0) return APP_PREVIEW;
    if (strcmp(bundle_id, "com.ojjyos.calendar") == 0) return APP_CALENDAR;
    if (strcmp(bundle_id, "com.ojjyos.about") == 0) return APP_ABOUT;
    return APP_DEMO;
}

static const char *app_name_from_type(AppType type)
{
    switch (type) {
        case APP_FINDER: return "Finder";
        case APP_SETTINGS: return "Settings";
        case APP_TERMINAL: return "Terminal";
        case APP_TEXTEDIT: return "TextEdit";
        case APP_NOTES: return "Notes";
        case APP_PREVIEW: return "Preview";
        case APP_CALENDAR: return "Calendar";
        case APP_ABOUT: return "About";
        default: return "App";
    }
}

static int create_window_internal(const char *title, int x, int y, int w, int h, AppType type)
{
    if (window_count >= COMPOSITOR_MAX_WINDOWS) return -1;

    int id = window_count + 1;
    CompositorWindow *win = &windows[window_count];
    AppWindowState *state = &app_states[window_count];
    memset(win, 0, sizeof(*win));
    memset(state, 0, sizeof(*state));

    win->id = id;
    win->app_type = type;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->base_w = w;
    win->base_h = h;
    win->glass_level = 1;
    win->blur_level = 1;
    win->corner_level = 1;
    win->active = true;
    win->demo = false;
    win->anim_open = 0;
    win->animating = true;
    strncpy(win->title, title ? title : "Window", sizeof(win->title) - 1);

    state->type = type;
    if (type == APP_FINDER) {
        finder_set_path(&state->finder, "/Users");
        state->finder.view_mode = FINDER_VIEW_LIST;
    } else if (type == APP_SETTINGS) {
        state->settings.page = SETTINGS_PAGE_APPEARANCE;
    } else if (type == APP_TERMINAL) {
        terminal_append_line(&state->terminal, "ojjyOS Terminal");
        terminal_append_line(&state->terminal, "Type 'help' for commands");
        strncpy(state->terminal.cwd, "/Users/guest", sizeof(state->terminal.cwd) - 1);
        state->terminal.history_count = 0;
        state->terminal.history_pos = 0;
    } else if (type == APP_TEXTEDIT) {
        textedit_clear(&state->textedit);
        strcpy(state->textedit.status, "Ready");
    } else if (type == APP_NOTES) {
        textedit_clear(&state->notes);
        strncpy(state->notes.file_path, "/Users/guest/Documents/Notes.txt",
                sizeof(state->notes.file_path) - 1);
        textedit_load_file(&state->notes, state->notes.file_path);
        strcpy(state->notes.status, "Notes");
    } else if (type == APP_PREVIEW) {
        strncpy(state->preview.options[0], "Tahoe Light", sizeof(state->preview.options[0]) - 1);
        strncpy(state->preview.options[1], "Tahoe Dark", sizeof(state->preview.options[1]) - 1);
        strncpy(state->preview.current, state->preview.options[0], sizeof(state->preview.current) - 1);
        state->preview.loaded = false;
        if (preview_load_thumbnail("/System/Wallpapers/Tahoe Light.raw", state->preview.light_thumb)) {
            if (preview_load_thumbnail("/System/Wallpapers/Tahoe Dark.raw", state->preview.dark_thumb)) {
                state->preview.loaded = true;
            }
        }
    } else if (type == APP_CALENDAR) {
        RtcTime now;
        rtc_read_time(&now);
        state->calendar.view = CAL_VIEW_MONTH;
        state->calendar.year = now.year;
        state->calendar.month = now.month;
        state->calendar.day = now.day;
        state->calendar.selected_day = now.day;
        state->calendar.selected_event = -1;
        state->calendar.edit_mode = false;
        state->calendar.edit_buffer[0] = '\0';
        calendar_load(&state->calendar);
    }

    window_count++;
    if (wm_hooks.on_create) {
        wm_hooks.on_create(id, type, x, y, w, h);
    }
    return id;
}

static int app_open_window(AppType type, const char *title)
{
    if (type < 0 || type >= APP_COUNT) return -1;

    if (app_window_index[type] >= 0 && app_window_index[type] < window_count) {
        int idx = app_window_index[type];
        if (idx != window_count - 1) {
            CompositorWindow temp = windows[idx];
            AppWindowState temp_state = app_states[idx];
            for (int j = idx; j < window_count - 1; j++) {
                windows[j] = windows[j + 1];
                app_states[j] = app_states[j + 1];
            }
            windows[window_count - 1] = temp;
            app_states[window_count - 1] = temp_state;
        }
        active_window_index = window_count - 1;
        rebuild_app_window_index();
        return windows[window_count - 1].id;
    }

    int w = 560;
    int h = 380;
    int x = 140;
    int y = 120;
    if (type == APP_FINDER) { w = 640; h = 420; }
    if (type == APP_SETTINGS) { w = 560; h = 400; }
    if (type == APP_TERMINAL) { w = 600; h = 360; }
    if (type == APP_TEXTEDIT) { w = 560; h = 360; }
    if (type == APP_NOTES) { w = 460; h = 320; }
    if (type == APP_PREVIEW) { w = 480; h = 320; }
    if (type == APP_CALENDAR) { w = 720; h = 440; }

    int id = create_window_internal(title, x, y, w, h, type);
    if (id >= 0) {
        rebuild_app_window_index();
        active_window_index = window_count - 1;
    }
    return id;
}

void compositor_init(uint32_t width, uint32_t height)
{
    comp_width = width;
    comp_height = height;
    theme = theme_light();
    window_count = 0;
    active_window_index = -1;
    for (int i = 0; i < APP_COUNT; i++) {
        app_window_index[i] = -1;
    }
    memset(app_states, 0, sizeof(app_states));
    last_frame_ms = 0;
    wallpaper_loaded = false;
    dragging = false;
    drag_index = -1;
    cursor_x = (int)width / 2;
    cursor_y = (int)height / 2;
    active_app_name[0] = 'F';
    active_app_name[1] = 'i';
    active_app_name[2] = 'n';
    active_app_name[3] = 'd';
    active_app_name[4] = 'e';
    active_app_name[5] = 'r';
    active_app_name[6] = '\0';

    spotlight_query[0] = '\0';
    spotlight_count = 0;
    spotlight_selected = 0;
    overlay_set(OVERLAY_NONE);

    app_registry_init();
    search_index_init();
    notifications_init();
    settings_load();
    compositor_set_dark_mode(settings_get()->dark_mode);
    compositor_set_wallpaper(settings_get()->dark_mode
        ? "/System/Wallpapers/Tahoe Dark.raw"
        : "/System/Wallpapers/Tahoe Light.raw");

    icon_folder_loaded = load_system_icon("/System/Library/Icons/Folder.raw", icon_folder);
    icon_file_loaded = load_system_icon("/System/Library/Icons/File.raw", icon_file);
}

void compositor_set_dark_mode(bool enabled)
{
    dark_mode = enabled;
    theme = dark_mode ? theme_dark() : theme_light();
    settings_get()->dark_mode = enabled;
}

void compositor_set_wallpaper(const char *path)
{
    if (!path) {
        wallpaper_loaded = false;
        return;
    }

    VfsFile *file = vfs_open(path, VFS_O_READ);
    if (!file) {
        serial_printf("[COMPOSITOR] Wallpaper not found: %s\n", path);
        wallpaper_loaded = false;
        return;
    }

    uint32_t header[2];
    ssize_t header_read = vfs_read(file, header, sizeof(header));
    if (header_read != (ssize_t)sizeof(header)) {
        vfs_close(file);
        wallpaper_loaded = false;
        return;
    }

    wallpaper_w = header[0];
    wallpaper_h = header[1];

    if (wallpaper_w == 0 || wallpaper_h == 0 ||
        wallpaper_w > WALLPAPER_MAX_W || wallpaper_h > WALLPAPER_MAX_H) {
        vfs_close(file);
        wallpaper_loaded = false;
        return;
    }

    size_t data_size = wallpaper_w * wallpaper_h * 4;
    ssize_t data_read = vfs_read(file, wallpaper_data, data_size);
    vfs_close(file);

    if (data_read != (ssize_t)data_size) {
        wallpaper_loaded = false;
        return;
    }

    wallpaper_loaded = true;
    serial_printf("[COMPOSITOR] Wallpaper loaded: %s (%dx%d)\n",
                  path, wallpaper_w, wallpaper_h);
}

int compositor_create_window(const char *title, int x, int y, int w, int h)
{
    return create_window_internal(title, x, y, w, h, APP_DEMO);
}

void compositor_move_window(int id, int x, int y)
{
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            windows[i].x = x;
            windows[i].y = y;
            if (wm_hooks.on_move) {
                wm_hooks.on_move(id, x, y);
            }
            return;
        }
    }
}

void compositor_resize_window(int id, int w, int h)
{
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            windows[i].w = w;
            windows[i].h = h;
            if (wm_hooks.on_resize) {
                wm_hooks.on_resize(id, w, h);
            }
            return;
        }
    }
}

void compositor_set_demo(int id, bool demo)
{
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            windows[i].demo = demo;
            return;
        }
    }
}

void compositor_set_active_app(const char *name)
{
    if (!name || name[0] == '\0') return;
    strncpy(active_app_name, name, sizeof(active_app_name) - 1);
}

void compositor_set_wm_hooks(const CompositorWmHooks *hooks)
{
    if (!hooks) {
        memset(&wm_hooks, 0, sizeof(wm_hooks));
        return;
    }
    wm_hooks = *hooks;
}

int compositor_destroy_window(int id)
{
    if (wm_hooks.on_destroy) {
        wm_hooks.on_destroy(id);
    }
    return -1;
}

void compositor_open_default_apps(void)
{
    int idx = app_registry_find_by_bundle_id("com.ojjyos.finder");
    if (idx >= 0) {
        launch_app_index(idx, timer_get_ticks());
    } else {
        app_open_window(APP_FINDER, "Finder");
    }
}

void compositor_handle_key(KeyCode keycode, char ascii, uint8_t modifiers)
{
    if (keycode == KEY_ESCAPE && overlay != OVERLAY_NONE) {
        overlay_set(OVERLAY_NONE);
        return;
    }

    bool shortcuts = settings_get()->shortcuts_enabled;
    if (shortcuts && (modifiers & INPUT_MOD_SUPER) && keycode == KEY_SPACE) {
        if (overlay == OVERLAY_SPOTLIGHT) {
            overlay_set(OVERLAY_NONE);
        } else {
            overlay_set(OVERLAY_SPOTLIGHT);
            spotlight_query[0] = '\0';
            spotlight_selected = 0;
            spotlight_refresh();
        }
        return;
    }

    if (shortcuts && (modifiers & INPUT_MOD_SUPER) && keycode == KEY_L) {
        overlay_set(overlay == OVERLAY_LAUNCHPAD ? OVERLAY_NONE : OVERLAY_LAUNCHPAD);
        return;
    }

    if (shortcuts && (modifiers & INPUT_MOD_SUPER) && keycode == KEY_C) {
        overlay_set(overlay == OVERLAY_CONTROL_CENTER ? OVERLAY_NONE : OVERLAY_CONTROL_CENTER);
        return;
    }

    if (shortcuts && (modifiers & INPUT_MOD_SUPER) && keycode == KEY_M) {
        overlay_set(overlay == OVERLAY_MISSION_CONTROL ? OVERLAY_NONE : OVERLAY_MISSION_CONTROL);
        return;
    }

    if (shortcuts && (modifiers & INPUT_MOD_SUPER) && keycode == KEY_TAB) {
        int count = app_registry_count();
        if (count == 0) return;

        int running_count = 0;
        for (int i = 0; i < count; i++) {
            AppInfo *app = app_registry_get(i);
            if (app && app->running) running_count++;
        }
        if (running_count == 0) return;

        if (overlay != OVERLAY_APP_SWITCHER) {
            overlay_set(OVERLAY_APP_SWITCHER);
            app_switcher_index = 0;
        } else {
            app_switcher_index = (app_switcher_index + 1) % running_count;
        }
        return;
    }

    if (shortcuts && (modifiers & INPUT_MOD_SUPER) && keycode == KEY_Q) {
        int idx = app_registry_find_by_name(active_app_name);
        AppInfo *app = app_registry_get(idx);
        if (app) {
            app->running = false;
            strncpy(active_app_name, "Finder", sizeof(active_app_name) - 1);
        }
        return;
    }

    if (overlay == OVERLAY_APP_SWITCHER) {
        if (keycode == KEY_ENTER) {
            int count = app_registry_count();
            int running[APP_REGISTRY_MAX];
            int running_count = 0;
            for (int i = 0; i < count; i++) {
                AppInfo *app = app_registry_get(i);
                if (app && app->running) running[running_count++] = i;
            }
            if (running_count > 0) {
                int idx = running[app_switcher_index % running_count];
                launch_app_index(idx, timer_get_ticks());
            }
            overlay_set(OVERLAY_NONE);
        }
        return;
    }

    if (overlay == OVERLAY_SPOTLIGHT) {
        if (keycode == KEY_UP) {
            if (spotlight_selected > 0) spotlight_selected--;
            return;
        }
        if (keycode == KEY_DOWN) {
            if (spotlight_selected + 1 < spotlight_count) spotlight_selected++;
            return;
        }
        if (keycode == KEY_BACKSPACE) {
            size_t len = strlen(spotlight_query);
            if (len > 0) {
                spotlight_query[len - 1] = '\0';
                spotlight_refresh();
            }
            return;
        }
        if (keycode == KEY_ENTER) {
            if (spotlight_count > 0) {
                SearchResult *res = &spotlight_results[spotlight_selected];
                if (res->type == SEARCH_RESULT_APP) {
                    launch_app_index(res->app_index, timer_get_ticks());
                }
                overlay_set(OVERLAY_NONE);
            }
            return;
        }
        if (ascii >= 32 && ascii <= 126) {
            size_t len = strlen(spotlight_query);
            if (len < SEARCH_QUERY_MAX - 1) {
                spotlight_query[len] = ascii;
                spotlight_query[len + 1] = '\0';
                spotlight_refresh();
            }
        }
    }

    if (overlay == OVERLAY_NONE && active_window_index >= 0 && active_window_index < window_count) {
        AppWindowState *state = &app_states[active_window_index];
        if (state->type == APP_TERMINAL) {
            terminal_handle_key(&state->terminal, ascii, keycode);
        } else if (state->type == APP_TEXTEDIT) {
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_S) {
                textedit_save(&state->textedit);
                return;
            }
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_O) {
                if (last_opened_path[0]) {
                    textedit_load_file(&state->textedit, last_opened_path);
                }
                return;
            }
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_C) {
                strncpy(text_clipboard, state->textedit.lines[state->textedit.cursor_line],
                        sizeof(text_clipboard) - 1);
                strcpy(state->textedit.status, "Copied line");
                return;
            }
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_X) {
                strncpy(text_clipboard, state->textedit.lines[state->textedit.cursor_line],
                        sizeof(text_clipboard) - 1);
                textedit_delete_line(&state->textedit, state->textedit.cursor_line);
                strcpy(state->textedit.status, "Cut line");
                return;
            }
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_V) {
                if (text_clipboard[0]) {
                    textedit_insert_line(&state->textedit, state->textedit.cursor_line + 1, text_clipboard);
                    strcpy(state->textedit.status, "Pasted line");
                }
                return;
            }
            textedit_handle_key(&state->textedit, ascii, keycode, modifiers);
        } else if (state->type == APP_NOTES) {
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_S) {
                textedit_save(&state->notes);
                return;
            }
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_C) {
                strncpy(text_clipboard, state->notes.lines[state->notes.cursor_line],
                        sizeof(text_clipboard) - 1);
                strcpy(state->notes.status, "Copied line");
                return;
            }
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_X) {
                strncpy(text_clipboard, state->notes.lines[state->notes.cursor_line],
                        sizeof(text_clipboard) - 1);
                textedit_delete_line(&state->notes, state->notes.cursor_line);
                strcpy(state->notes.status, "Cut line");
                return;
            }
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_V) {
                if (text_clipboard[0]) {
                    textedit_insert_line(&state->notes, state->notes.cursor_line + 1, text_clipboard);
                    strcpy(state->notes.status, "Pasted line");
                }
                return;
            }
            textedit_handle_key(&state->notes, ascii, keycode, modifiers);
            if (state->notes.dirty) {
                textedit_save(&state->notes);
            }
        } else if (state->type == APP_FINDER) {
            FinderState *finder = &state->finder;
            if (finder->rename_mode) {
                if (keycode == KEY_ESCAPE) {
                    finder->rename_mode = false;
                    return;
                }
                if (keycode == KEY_ENTER) {
                    if (finder->selected >= 0) {
                        FinderEntry *entry = &finder->entries[finder->selected];
                        char src[256];
                        char dst[256];
                        vfs_join_path(src, sizeof(src), finder->path, entry->name);
                        vfs_join_path(dst, sizeof(dst), finder->path, finder->rename_buffer);
                        if (vfs_rename(src, dst) == 0) {
                            finder->needs_refresh = true;
                        }
                    }
                    finder->rename_mode = false;
                    return;
                }
                if (keycode == KEY_BACKSPACE) {
                    size_t len = strlen(finder->rename_buffer);
                    if (len > 0) {
                        finder->rename_buffer[len - 1] = '\0';
                    }
                    return;
                }
                if (ascii >= 32 && ascii <= 126) {
                    size_t len = strlen(finder->rename_buffer);
                    if (len < sizeof(finder->rename_buffer) - 1) {
                        finder->rename_buffer[len] = ascii;
                        finder->rename_buffer[len + 1] = '\0';
                    }
                }
                return;
            }
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_C && finder->selected >= 0) {
                FinderEntry *entry = &finder->entries[finder->selected];
                vfs_join_path(finder->clip_path, sizeof(finder->clip_path), finder->path, entry->name);
                finder->clip_cut = false;
                return;
            }
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_X && finder->selected >= 0) {
                FinderEntry *entry = &finder->entries[finder->selected];
                vfs_join_path(finder->clip_path, sizeof(finder->clip_path), finder->path, entry->name);
                finder->clip_cut = true;
                return;
            }
            if ((modifiers & INPUT_MOD_SUPER) && keycode == KEY_V && finder->clip_path[0]) {
                const char *base = vfs_basename(finder->clip_path);
                char dst[256];
                vfs_join_path(dst, sizeof(dst), finder->path, base);
                if (finder->clip_cut) {
                    if (vfs_rename(finder->clip_path, dst) == 0) {
                        finder->clip_path[0] = '\0';
                        finder->clip_cut = false;
                        finder->needs_refresh = true;
                    }
                } else {
                    if (copy_file_path(finder->clip_path, dst) == 0) {
                        finder->needs_refresh = true;
                    }
                }
                return;
            }
            if (keycode == KEY_BACKSPACE) {
                size_t len = strlen(finder->search);
                if (len > 0) {
                    finder->search[len - 1] = '\0';
                    finder->needs_refresh = true;
                }
                return;
            }
            if (keycode == KEY_DELETE && finder->selected >= 0) {
                FinderEntry *entry = &finder->entries[finder->selected];
                char path[256];
                vfs_join_path(path, sizeof(path), finder->path, entry->name);
                if (vfs_unlink(path) == 0) {
                    finder->needs_refresh = true;
                }
                return;
            }
            if (keycode == KEY_R && finder->selected >= 0) {
                FinderEntry *entry = &finder->entries[finder->selected];
                strncpy(finder->rename_buffer, entry->name, sizeof(finder->rename_buffer) - 1);
                finder->rename_mode = true;
                return;
            }
            if (keycode == KEY_UP) {
                if (finder->selected > 0) finder->selected--;
                finder_update_preview(finder);
                return;
            }
            if (keycode == KEY_DOWN) {
                if (finder->selected + 1 < finder->entry_count) finder->selected++;
                finder_update_preview(finder);
                return;
            }
            if (keycode == KEY_ENTER && finder->selected >= 0) {
                FinderEntry *entry = &finder->entries[finder->selected];
                char path[256];
                vfs_join_path(path, sizeof(path), finder->path, entry->name);
                if (entry->type == VFS_TYPE_DIR) {
                    finder_set_path(finder, path);
                } else if (entry->type == VFS_TYPE_BUNDLE) {
                    Bundle bundle;
                    if (bundle_load(path, &bundle) == 0) {
                        int app_idx = app_registry_find_by_bundle_id(bundle.manifest.bundle_id);
                        if (app_idx >= 0) {
                            launch_app_index(app_idx, timer_get_ticks());
                        }
                    }
                } else if (entry->type == VFS_TYPE_FILE) {
                    strncpy(last_opened_path, path, sizeof(last_opened_path) - 1);
                    int textedit_idx = app_registry_find_by_bundle_id("com.ojjyos.textedit");
                    if (textedit_idx >= 0) {
                        launch_app_index(textedit_idx, timer_get_ticks());
                    }
                    if (app_window_index[APP_TEXTEDIT] >= 0) {
                        TextEditState *edit = &app_states[app_window_index[APP_TEXTEDIT]].textedit;
                        textedit_load_file(edit, path);
                    }
                }
                return;
            }
            if (ascii >= 32 && ascii <= 126) {
                size_t len = strlen(finder->search);
                if (len < sizeof(finder->search) - 1) {
                    finder->search[len] = ascii;
                    finder->search[len + 1] = '\0';
                    finder->needs_refresh = true;
                }
            }
        } else if (state->type == APP_CALENDAR) {
            CalendarState *cal = &state->calendar;
            if (cal->edit_mode) {
                if (keycode == KEY_ESCAPE) {
                    cal->edit_mode = false;
                    cal->edit_buffer[0] = '\0';
                    return;
                }
                if (keycode == KEY_TAB) {
                    cal->edit_field = (cal->edit_field + 1) % 4;
                    calendar_start_edit(cal, cal->edit_field);
                    return;
                }
                if (keycode == KEY_BACKSPACE) {
                    size_t len = strlen(cal->edit_buffer);
                    if (len > 0) cal->edit_buffer[len - 1] = '\0';
                    return;
                }
                if (keycode == KEY_ENTER) {
                    if (calendar_apply_edit(cal)) {
                        cal->edit_mode = false;
                    }
                    return;
                }
                if (ascii >= 32 && ascii <= 126) {
                    size_t len = strlen(cal->edit_buffer);
                    if (len < sizeof(cal->edit_buffer) - 1) {
                        cal->edit_buffer[len] = ascii;
                        cal->edit_buffer[len + 1] = '\0';
                    }
                }
                return;
            }

            if (keycode == KEY_DELETE) {
                calendar_delete_selected(cal);
                return;
            }
            if (keycode == KEY_N) {
                calendar_add_quick_event(cal);
                return;
            }
            if (keycode == KEY_E) {
                calendar_start_edit(cal, 0);
                return;
            }
            if (keycode == KEY_T) {
                calendar_start_edit(cal, 1);
                return;
            }
            if (keycode == KEY_L) {
                calendar_start_edit(cal, 2);
                return;
            }
            if (keycode == KEY_O) {
                calendar_start_edit(cal, 3);
                return;
            }
        }
    }
}

bool compositor_overlay_active(void)
{
    return overlay != OVERLAY_NONE;
}

void compositor_handle_mouse(int x, int y, bool down, bool up)
{
    cursor_x = x;
    cursor_y = y;

    if (active_window_index >= 0 && active_window_index < window_count) {
        AppWindowState *state = &app_states[active_window_index];
        CompositorWindow *win = &windows[active_window_index];
        if (state->type == APP_FINDER && state->finder.drag_active) {
            FinderState *finder = &state->finder;
            finder->drag_hover_index = -1;

            int content_x = win->x + 12;
            int content_y = win->y + 40;
            int sidebar_w = 150;
            int preview_w = 180;
            int main_x = content_x + sidebar_w + 10;
            int main_w = win->w - 24 - sidebar_w - preview_w - 20;

            if (x >= main_x && x <= main_x + main_w) {
                if (finder->view_mode == FINDER_VIEW_LIST) {
                    int row = (y - (content_y + 44)) / 18;
                    if (row >= 0 && row < finder->entry_count) {
                        FinderEntry *entry = &finder->entries[row];
                        if (entry->type == VFS_TYPE_DIR) {
                            finder->drag_hover_index = row;
                        }
                    }
                } else {
                    int icon = 40;
                    int gap = 18;
                    int cols = 4;
                    int start_x = main_x + 16;
                    int start_y = content_y + 40;
                    int col = (x - start_x) / (icon + gap);
                    int row = (y - start_y) / (icon + 28);
                    int idx = row * cols + col;
                    if (col >= 0 && row >= 0 && idx >= 0 && idx < finder->entry_count) {
                        FinderEntry *entry = &finder->entries[idx];
                        if (entry->type == VFS_TYPE_DIR) {
                            finder->drag_hover_index = idx;
                        }
                    }
                }
            }
        }
    }

    if (up && active_window_index >= 0 && active_window_index < window_count) {
        AppWindowState *state = &app_states[active_window_index];
        CompositorWindow *win = &windows[active_window_index];
        if (state->type == APP_FINDER && state->finder.drag_active) {
            FinderState *finder = &state->finder;
            finder->drag_active = false;
            finder->drag_hover_index = -1;

            int content_x = win->x + 12;
            int content_y = win->y + 40;
            int sidebar_w = 150;
            int preview_w = 180;
            int main_x = content_x + sidebar_w + 10;
            int main_w = win->w - 24 - sidebar_w - preview_w - 20;

            if (x >= main_x && x <= main_x + main_w) {
                if (finder->view_mode == FINDER_VIEW_LIST) {
                    int row = (y - (content_y + 44)) / 18;
                    if (row >= 0 && row < finder->entry_count) {
                        FinderEntry *entry = &finder->entries[row];
                        if (entry->type == VFS_TYPE_DIR) {
                            char dst_dir[256];
                            vfs_join_path(dst_dir, sizeof(dst_dir), finder->path, entry->name);
                            const char *base = vfs_basename(finder->drag_path);
                            char dst[256];
                            vfs_join_path(dst, sizeof(dst), dst_dir, base);
                            if (vfs_rename(finder->drag_path, dst) == 0) {
                                finder->needs_refresh = true;
                            }
                        }
                    }
                } else {
                    int icon = 40;
                    int gap = 18;
                    int cols = 4;
                    int start_x = main_x + 16;
                    int start_y = content_y + 40;
                    int col = (x - start_x) / (icon + gap);
                    int row = (y - start_y) / (icon + 28);
                    int idx = row * cols + col;
                    if (col >= 0 && row >= 0 && idx >= 0 && idx < finder->entry_count) {
                        FinderEntry *entry = &finder->entries[idx];
                        if (entry->type == VFS_TYPE_DIR) {
                            char dst_dir[256];
                            vfs_join_path(dst_dir, sizeof(dst_dir), finder->path, entry->name);
                            const char *base = vfs_basename(finder->drag_path);
                            char dst[256];
                            vfs_join_path(dst, sizeof(dst), dst_dir, base);
                            if (vfs_rename(finder->drag_path, dst) == 0) {
                                finder->needs_refresh = true;
                            }
                        }
                    }
                }
            }
        }
    }

    if (down) {
        if (y <= MENU_BAR_HEIGHT && x >= (int)comp_width - 140) {
            overlay_set(overlay == OVERLAY_CONTROL_CENTER ? OVERLAY_NONE : OVERLAY_CONTROL_CENTER);
            return;
        }

        if (overlay == OVERLAY_SPOTLIGHT) {
            int x0 = ((int)comp_width - SPOTLIGHT_WIDTH) / 2;
            int y0 = 120;
            int list_y = y0 + SPOTLIGHT_HEIGHT + 8;
            int list_h = spotlight_count * 28 + 12;

            if (y >= list_y && y <= list_y + list_h) {
                int idx = (y - list_y - 8) / 28;
                if (idx >= 0 && idx < spotlight_count) {
                    spotlight_selected = idx;
                    if (spotlight_results[idx].type == SEARCH_RESULT_APP) {
                        launch_app_index(spotlight_results[idx].app_index, timer_get_ticks());
                    }
                    overlay_set(OVERLAY_NONE);
                }
            } else if (!(x >= x0 && x <= x0 + SPOTLIGHT_WIDTH && y >= y0 && y <= y0 + SPOTLIGHT_HEIGHT)) {
                overlay_set(OVERLAY_NONE);
            }
            return;
        }

        if (overlay == OVERLAY_LAUNCHPAD) {
            int count = app_registry_count();
            int grid_w = LAUNCHPAD_COLS * LAUNCHPAD_ICON_SIZE + (LAUNCHPAD_COLS - 1) * 40;
            int grid_h = LAUNCHPAD_ROWS * (LAUNCHPAD_ICON_SIZE + 28) + (LAUNCHPAD_ROWS - 1) * 20;
            int start_x = ((int)comp_width - grid_w) / 2;
            int start_y = ((int)comp_height - grid_h) / 2;

            int idx = 0;
            for (int row = 0; row < LAUNCHPAD_ROWS; row++) {
                for (int col = 0; col < LAUNCHPAD_COLS; col++) {
                    if (idx >= count) break;
                    int ix = start_x + col * (LAUNCHPAD_ICON_SIZE + 40);
                    int iy = start_y + row * (LAUNCHPAD_ICON_SIZE + 48);
                    if (x >= ix && x <= ix + LAUNCHPAD_ICON_SIZE &&
                        y >= iy && y <= iy + LAUNCHPAD_ICON_SIZE) {
                        launch_app_index(idx, timer_get_ticks());
                        overlay_set(OVERLAY_NONE);
                        return;
                    }
                    idx++;
                }
            }
            overlay_set(OVERLAY_NONE);
            return;
        }

        if (overlay == OVERLAY_CONTROL_CENTER) {
            int panel_x = (int)comp_width - CONTROL_CENTER_WIDTH - 20;
            int panel_y = MENU_BAR_HEIGHT + 10;
            if (x >= panel_x && x <= panel_x + CONTROL_CENTER_WIDTH &&
                y >= panel_y && y <= panel_y + CONTROL_CENTER_HEIGHT) {
                if (y >= panel_y + 44 && y < panel_y + 66) {
                    settings_toggle_wifi();
                } else if (y >= panel_y + 66 && y < panel_y + 88) {
                    settings_toggle_bluetooth();
                } else if (y >= panel_y + 88 && y < panel_y + 110) {
                    settings_toggle_dark_mode();
                    compositor_set_dark_mode(settings_get()->dark_mode);
                    compositor_set_wallpaper(settings_get()->dark_mode
                        ? "/System/Wallpapers/Tahoe Dark.raw"
                        : "/System/Wallpapers/Tahoe Light.raw");
                } else if (y >= panel_y + 136 && y < panel_y + 146) {
                    int value = x - (panel_x + 16);
                    if (value < 0) value = 0;
                    if (value > 100) value = 100;
                    settings_set_volume((uint8_t)value);
                } else if (y >= panel_y + 172 && y < panel_y + 182) {
                    int value = x - (panel_x + 16);
                    if (value < 0) value = 0;
                    if (value > 100) value = 100;
                    settings_set_brightness((uint8_t)value);
                }
                return;
            }
            overlay_set(OVERLAY_NONE);
            return;
        }

        if (overlay == OVERLAY_MISSION_CONTROL) {
            overlay_set(OVERLAY_NONE);
            return;
        }

        int count = app_registry_count();
        if (count > 0) {
            SettingsState *settings = settings_get();
            int icon_base = settings->dock_size;
            int spacing = 12;
            int total_width = count * icon_base + (count - 1) * spacing + 40;
            if (total_width < 240) total_width = 240;
            int dock_w = total_width;
            int dock_h = DOCK_HEIGHT;
            int dock_x = ((int)comp_width - dock_w) / 2;
            int dock_y = (int)comp_height - dock_h - 20;

            if (x >= dock_x && x <= dock_x + dock_w && y >= dock_y && y <= dock_y + dock_h) {
                int base_x = dock_x + 20;
                for (int i = 0; i < count; i++) {
                    int icon_center_x = base_x + i * (icon_base + spacing) + icon_base / 2;
                    int icon_center_y = dock_y + 36;
                    int size = icon_base;
                    if (x >= icon_center_x - size && x <= icon_center_x + size &&
                        y >= icon_center_y - size && y <= icon_center_y + size) {
                        launch_app_index(i, timer_get_ticks());
                        return;
                    }
                }
            }
        }

        for (int i = window_count - 1; i >= 0; i--) {
            CompositorWindow *win = &windows[i];
            AppWindowState *state = &app_states[i];
                if (x >= win->x && x < win->x + win->w && y >= win->y && y < win->y + win->h) {
                    active_window_index = i;
                    strncpy(active_app_name, app_name_from_type(state->type),
                            sizeof(active_app_name) - 1);
                    if (wm_hooks.on_focus) {
                        wm_hooks.on_focus(win->id);
                    }

                if (y < win->y + 32) {
                    dragging = true;
                    drag_index = i;
                    drag_dx = x - win->x;
                    drag_dy = y - win->y;
                } else if (state->type == APP_FINDER) {
                    int content_x = win->x + 12;
                    int content_y = win->y + 40;
                    int sidebar_w = 150;
                    int toolbar_y = win->y + 36;

                    if (y >= win->y + 10 && y <= win->y + 32 &&
                        x >= content_x + 60 && x <= content_x + 110) {
                        state->finder.view_mode = (state->finder.view_mode == FINDER_VIEW_LIST)
                            ? FINDER_VIEW_ICON : FINDER_VIEW_LIST;
                    }

                    if (y >= toolbar_y - 26 && y <= toolbar_y - 8) {
                        if (x >= content_x + 8 && x <= content_x + 26) {
                            finder_back(&state->finder);
                        } else if (x >= content_x + 30 && x <= content_x + 48) {
                            finder_forward(&state->finder);
                        }
                    }

                    if (x >= content_x && x <= content_x + sidebar_w) {
                        int rel_y = y - content_y;
                        if (rel_y >= 32 && rel_y < 52) {
                            finder_set_path(&state->finder, "/Applications");
                        } else if (rel_y >= 52 && rel_y < 72) {
                            finder_set_path(&state->finder, "/System");
                        } else if (rel_y >= 72 && rel_y < 92) {
                            finder_set_path(&state->finder, "/Users");
                        }
                    } else {
                        int main_x = content_x + sidebar_w + 10;
                        if (x >= main_x) {
                            if (state->finder.view_mode == FINDER_VIEW_LIST) {
                                int row = (y - (content_y + 36)) / 18;
                                if (row >= 0 && row < state->finder.entry_count) {
                                    state->finder.selected = row;
                                    finder_update_preview(&state->finder);
                                    FinderEntry *entry = &state->finder.entries[row];
                                    char path[256];
                                    vfs_join_path(path, sizeof(path), state->finder.path, entry->name);
                                    if (entry->type == VFS_TYPE_FILE || entry->type == VFS_TYPE_DIR) {
                                        strncpy(state->finder.drag_path, path, sizeof(state->finder.drag_path) - 1);
                                        state->finder.drag_active = true;
                                    }
                                    if (entry->type == VFS_TYPE_DIR || entry->type == VFS_TYPE_BUNDLE) {
                                        if (entry->type == VFS_TYPE_BUNDLE) {
                                            Bundle bundle;
                                            if (bundle_load(path, &bundle) == 0) {
                                                int app_idx = app_registry_find_by_bundle_id(bundle.manifest.bundle_id);
                                                if (app_idx >= 0) {
                                                    launch_app_index(app_idx, timer_get_ticks());
                                                }
                                            }
                                        } else {
                                            finder_set_path(&state->finder, path);
                                        }
                                    } else {
                                        strncpy(last_opened_path, path, sizeof(last_opened_path) - 1);
                                        int textedit_idx = app_registry_find_by_bundle_id("com.ojjyos.textedit");
                                        if (textedit_idx >= 0) {
                                            launch_app_index(textedit_idx, timer_get_ticks());
                                        } else {
                                            app_open_window(APP_TEXTEDIT, "TextEdit");
                                        }
                                        if (app_window_index[APP_TEXTEDIT] >= 0) {
                                            TextEditState *edit = &app_states[app_window_index[APP_TEXTEDIT]].textedit;
                                            textedit_load_file(edit, path);
                                        }
                                    }
                                }
                            } else {
                                int icon = 40;
                                int gap = 18;
                                int cols = 4;
                                int start_x = main_x + 16;
                                int start_y = content_y + 40;
                                int col = (x - start_x) / (icon + gap);
                                int row = (y - start_y) / (icon + 28);
                                int idx = row * cols + col;
                                if (col >= 0 && row >= 0 && idx >= 0 && idx < state->finder.entry_count) {
                                    FinderEntry *entry = &state->finder.entries[idx];
                                    state->finder.selected = idx;
                                    finder_update_preview(&state->finder);
                                    char path[256];
                                    vfs_join_path(path, sizeof(path), state->finder.path, entry->name);
                                    if (entry->type == VFS_TYPE_FILE || entry->type == VFS_TYPE_DIR) {
                                        strncpy(state->finder.drag_path, path, sizeof(state->finder.drag_path) - 1);
                                        state->finder.drag_active = true;
                                    }
                                    if (entry->type == VFS_TYPE_DIR || entry->type == VFS_TYPE_BUNDLE) {
                                        if (entry->type == VFS_TYPE_BUNDLE) {
                                            Bundle bundle;
                                            if (bundle_load(path, &bundle) == 0) {
                                                int app_idx = app_registry_find_by_bundle_id(bundle.manifest.bundle_id);
                                                if (app_idx >= 0) {
                                                    launch_app_index(app_idx, timer_get_ticks());
                                                }
                                            }
                                        } else {
                                            finder_set_path(&state->finder, path);
                                        }
                                    } else {
                                        strncpy(last_opened_path, path, sizeof(last_opened_path) - 1);
                                        int textedit_idx = app_registry_find_by_bundle_id("com.ojjyos.textedit");
                                        if (textedit_idx >= 0) {
                                            launch_app_index(textedit_idx, timer_get_ticks());
                                        }
                                        if (app_window_index[APP_TEXTEDIT] >= 0) {
                                            TextEditState *edit = &app_states[app_window_index[APP_TEXTEDIT]].textedit;
                                            textedit_load_file(edit, path);
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else if (state->type == APP_SETTINGS) {
                    int content_x = win->x + 12;
                    int content_y = win->y + 40;
                    int sidebar_w = 160;
                    if (x >= content_x && x <= content_x + sidebar_w) {
                        int rel_y = y - (content_y + 16);
                        int idx = rel_y / 20;
                        if (idx >= 0 && idx < SETTINGS_PAGE_COUNT) {
                            state->settings.page = (SettingsPage)idx;
                        }
                    } else {
                        int main_x = content_x + sidebar_w + 10;
                        if (state->settings.page == SETTINGS_PAGE_APPEARANCE &&
                            x >= main_x + 100 && x <= main_x + 140 && y >= content_y + 38 && y <= content_y + 52) {
                            settings_toggle_dark_mode();
                            compositor_set_dark_mode(settings_get()->dark_mode);
                            compositor_set_wallpaper(settings_get()->dark_mode
                                ? "/System/Wallpapers/Tahoe Dark.raw"
                                : "/System/Wallpapers/Tahoe Light.raw");
                        }
                        if (state->settings.page == SETTINGS_PAGE_APPEARANCE &&
                            x >= main_x + 140 && x <= main_x + 180 && y >= content_y + 60 && y <= content_y + 74) {
                            settings_toggle_time_format();
                        }
                        if (state->settings.page == SETTINGS_PAGE_WALLPAPER) {
                            if (x >= main_x + 16 && x <= main_x + 96 && y >= content_y + 48 && y <= content_y + 98) {
                                compositor_set_wallpaper("/System/Wallpapers/Tahoe Light.raw");
                                settings_get()->dark_mode = false;
                                compositor_set_dark_mode(false);
                                settings_save();
                            }
                            if (x >= main_x + 120 && x <= main_x + 200 && y >= content_y + 48 && y <= content_y + 98) {
                                compositor_set_wallpaper("/System/Wallpapers/Tahoe Dark.raw");
                                settings_get()->dark_mode = true;
                                compositor_set_dark_mode(true);
                                settings_save();
                            }
                        }
                        if (state->settings.page == SETTINGS_PAGE_DOCK) {
                            if (x >= main_x + 120 && x <= main_x + 220 && y >= content_y + 34 && y <= content_y + 52) {
                                settings_get()->dock_size += 4;
                                if (settings_get()->dock_size > 44) settings_get()->dock_size = 32;
                                settings_save();
                            }
                            if (x >= main_x + 120 && x <= main_x + 240 && y >= content_y + 58 && y <= content_y + 76) {
                                settings_get()->dock_magnify += 4;
                                if (settings_get()->dock_magnify > 72) settings_get()->dock_magnify = 48;
                                settings_save();
                            }
                        }
                        if (state->settings.page == SETTINGS_PAGE_KEYBOARD) {
                            if (y >= content_y + 38 && y <= content_y + 56) {
                                settings_get()->shortcuts_enabled = !settings_get()->shortcuts_enabled;
                                settings_save();
                            }
                        }
                        if (state->settings.page == SETTINGS_PAGE_MOUSE) {
                            if (x >= main_x + 140 && x <= main_x + 240 && y >= content_y + 34 && y <= content_y + 52) {
                                settings_get()->mouse_speed++;
                                if (settings_get()->mouse_speed > 4) settings_get()->mouse_speed = 1;
                                settings_save();
                            }
                        }
                    }
                } else if (state->type == APP_CALENDAR) {
                    CalendarState *cal = &state->calendar;
                    int content_x = win->x + 12;
                    int content_y = win->y + 40;
                    int content_w = win->w - 24;
                    int content_h = win->h - 52;
                    int sidebar_w = 160;
                    int agenda_w = 200;
                    int header_h = 40;

                    int header_x = content_x + sidebar_w + 10;
                    int header_w = content_w - sidebar_w - agenda_w - 20;

                    if (y >= content_y + 10 && y <= content_y + 32) {
                        int nav_left = header_x + header_w - 90;
                        int nav_right = header_x + header_w - 66;
                        int today_x = header_x + header_w - 42;
                        if (x >= nav_left && x <= nav_left + 18) {
                            calendar_shift_month(cal, -1);
                            return;
                        }
                        if (x >= nav_right && x <= nav_right + 18) {
                            calendar_shift_month(cal, 1);
                            return;
                        }
                        if (x >= today_x && x <= today_x + 36) {
                            RtcTime now;
                            rtc_read_time(&now);
                            cal->year = now.year;
                            cal->month = now.month;
                            cal->day = now.day;
                            cal->selected_day = now.day;
                            return;
                        }
                    }

                    int view_x = header_x + header_w - 200;
                    if (y >= content_y + header_h - 18 && y <= content_y + header_h - 2) {
                        for (int i = 0; i < 4; i++) {
                            int vx = view_x + i * 45;
                            if (x >= vx && x <= vx + 42) {
                                cal->view = (CalendarView)i;
                                return;
                            }
                        }
                    }

                    if (x >= content_x + 10 && x <= content_x + sidebar_w - 10 &&
                        y >= content_y + content_h - 36 && y <= content_y + content_h - 14) {
                        calendar_add_quick_event(cal);
                        return;
                    }

                    if (cal->view == CAL_VIEW_MONTH) {
                        int grid_x = header_x;
                        int grid_y = content_y + header_h + 6;
                        int grid_w = header_w;
                        int grid_h = content_h - header_h - 12;
                        int cell_w = grid_w / 7;
                        int cell_h = (grid_h - 20) / 6;
                        int first_wd = weekday_of_date(cal->year, cal->month, 1);
                        int days = days_in_month(cal->year, cal->month);

                        int rel_x = x - grid_x;
                        int rel_y = y - (grid_y + 14);
                        if (rel_x >= 0 && rel_y >= 0) {
                            int col = rel_x / cell_w;
                            int row = rel_y / cell_h;
                            int idx = row * 7 + col;
                            int day = idx - first_wd + 1;
                            if (day >= 1 && day <= days) {
                                cal->selected_day = day;
                                int indices[8];
                                int count = calendar_events_for_day(cal, cal->year, cal->month, cal->selected_day, indices, 8);
                                cal->selected_event = (count > 0) ? indices[0] : -1;
                            }
                        }
                    }

                    int agenda_x = content_x + content_w - agenda_w;
                    if (x >= agenda_x && x <= agenda_x + agenda_w && y >= content_y + 30 && y <= content_y + content_h - 40) {
                        int indices[8];
                        int count = calendar_events_for_day(cal, cal->year, cal->month, cal->selected_day, indices, 8);
                        int row = (y - (content_y + 30)) / 18;
                        if (row >= 0 && row < count) {
                            cal->selected_event = indices[row];
                        }
                    }
                } else if (state->type == APP_PREVIEW) {
                    int content_x = win->x + 12;
                    int content_y = win->y + 40;
                    int thumb_x = content_x + 16;
                    int thumb_y = content_y + 40;
                    int gap = 16;

                    if (x >= thumb_x && x <= thumb_x + 120 && y >= thumb_y && y <= thumb_y + 80) {
                        compositor_set_wallpaper("/System/Wallpapers/Tahoe Light.raw");
                        strncpy(state->preview.current, "Tahoe Light", sizeof(state->preview.current) - 1);
                        settings_get()->dark_mode = false;
                        compositor_set_dark_mode(false);
                        settings_save();
                    } else if (x >= thumb_x + 120 + gap && x <= thumb_x + 240 + gap &&
                               y >= thumb_y && y <= thumb_y + 80) {
                        compositor_set_wallpaper("/System/Wallpapers/Tahoe Dark.raw");
                        strncpy(state->preview.current, "Tahoe Dark", sizeof(state->preview.current) - 1);
                        settings_get()->dark_mode = true;
                        compositor_set_dark_mode(true);
                        settings_save();
                    }
                }

                if (i != window_count - 1) {
                    CompositorWindow temp = windows[i];
                    AppWindowState temp_state = app_states[i];
                    for (int j = i; j < window_count - 1; j++) {
                        windows[j] = windows[j + 1];
                        app_states[j] = app_states[j + 1];
                    }
                    windows[window_count - 1] = temp;
                    app_states[window_count - 1] = temp_state;
                    drag_index = window_count - 1;
                    active_window_index = window_count - 1;
                    rebuild_app_window_index();
                    if (wm_hooks.on_focus) {
                        wm_hooks.on_focus(windows[window_count - 1].id);
                    }
                }
                break;
            }
        }
    }

    if (up) {
        dragging = false;
        drag_index = -1;
    }

    if (dragging && drag_index >= 0 && drag_index < window_count) {
        windows[drag_index].x = x - drag_dx;
        windows[drag_index].y = y - drag_dy;
    }
}

void compositor_handle_mouse_move(int32_t dx, int32_t dy)
{
    int speed = settings_get()->mouse_speed;
    if (speed < 1) speed = 1;
    if (speed > 4) speed = 4;

    cursor_x += dx * speed;
    cursor_y += dy * speed;

    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x >= (int)comp_width) cursor_x = (int)comp_width - 1;
    if (cursor_y >= (int)comp_height) cursor_y = (int)comp_height - 1;

    if (dragging && drag_index >= 0 && drag_index < window_count) {
        windows[drag_index].x = cursor_x - drag_dx;
        windows[drag_index].y = cursor_y - drag_dy;
    }
}

static void update_animations(void)
{
    for (int i = 0; i < window_count; i++) {
        CompositorWindow *win = &windows[i];
        if (win->animating) {
            win->anim_open += 40;
            if (win->anim_open >= 1000) {
                win->anim_open = 1000;
                win->animating = false;
            }
        }
    }

    int step = 120;
    int target_spotlight = (overlay == OVERLAY_SPOTLIGHT) ? 1000 : 0;
    int target_launchpad = (overlay == OVERLAY_LAUNCHPAD) ? 1000 : 0;
    int target_control = (overlay == OVERLAY_CONTROL_CENTER) ? 1000 : 0;
    int target_mission = (overlay == OVERLAY_MISSION_CONTROL) ? 1000 : 0;
    int target_switcher = (overlay == OVERLAY_APP_SWITCHER) ? 1000 : 0;

    if (anim_spotlight < target_spotlight) anim_spotlight = MIN(1000, anim_spotlight + step);
    if (anim_spotlight > target_spotlight) anim_spotlight = MAX(0, anim_spotlight - step);

    if (anim_launchpad < target_launchpad) anim_launchpad = MIN(1000, anim_launchpad + step);
    if (anim_launchpad > target_launchpad) anim_launchpad = MAX(0, anim_launchpad - step);

    if (anim_control_center < target_control) anim_control_center = MIN(1000, anim_control_center + step);
    if (anim_control_center > target_control) anim_control_center = MAX(0, anim_control_center - step);

    if (anim_mission_control < target_mission) anim_mission_control = MIN(1000, anim_mission_control + step);
    if (anim_mission_control > target_mission) anim_mission_control = MAX(0, anim_mission_control - step);

    if (anim_app_switcher < target_switcher) anim_app_switcher = MIN(1000, anim_app_switcher + step);
    if (anim_app_switcher > target_switcher) anim_app_switcher = MAX(0, anim_app_switcher - step);
}

void compositor_tick(uint64_t now_ms)
{
    if (now_ms - last_frame_ms < 33) {
        return;
    }

    last_frame_ms = now_ms;
    update_animations();

    draw_wallpaper();

    if (anim_launchpad > 0) {
        draw_launchpad(anim_launchpad);
    } else if (mission_control_active || anim_mission_control > 0) {
        for (int i = 0; i < window_count; i++) {
            draw_window(i);
        }
        draw_mission_control(anim_mission_control);
    } else {
        for (int i = 0; i < window_count; i++) {
            draw_window(i);
        }
    }

    draw_menu_bar();

    if (anim_launchpad == 0) {
        draw_dock(now_ms);
    }

    if (anim_spotlight > 0) {
        draw_spotlight(anim_spotlight);
    }
    if (anim_control_center > 0) {
        draw_control_center(anim_control_center);
    }
    if (anim_app_switcher > 0) {
        draw_app_switcher(anim_app_switcher);
    }

    draw_cursor(cursor_x, cursor_y);
}
