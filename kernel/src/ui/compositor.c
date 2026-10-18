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
#include "../drivers/driver.h"
#include "../serial.h"
#include "../memory.h"
#include "../system.h"
#include "../users.h"
#include "../proc.h"
#include "../user.h"

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

/* Screen-sized wallpaper caches, rebuilt only when the wallpaper or theme
 * changes. Per-frame sampling becomes a single array read instead of a
 * divide per pixel (wallpaper) or ~100 reads per pixel (glass blur). */
#define WP_CACHE_MAX_PIXELS     (1920 * 1200)
#define WP_CACHE_MAX_DIM        2048
#define WP_BLUR_RADIUS          10
static Color wp_cache[WP_CACHE_MAX_PIXELS];
static Color wp_blur[WP_CACHE_MAX_PIXELS];
static Color wp_line_tmp[WP_CACHE_MAX_DIM];
static bool wp_cache_valid = false;
static void wallpaper_rebuild_cache(void);

static bool dragging = false;
static int drag_index = -1;
static int drag_dx = 0;
static int drag_dy = 0;
static bool resizing = false;
static int resize_index = -1;
static int resize_start_x = 0;
static int resize_start_y = 0;
static int resize_orig_w = 0;
static int resize_orig_h = 0;
static int cursor_x = 0;
static int cursor_y = 0;

static char active_app_name[32] = "Finder";

/* Dirty tracking (see compositor_tick): redraw only when something changed */
static volatile bool need_redraw = true;
static int last_drawn_minute = -1;

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
    APP_WEB,
    APP_CALCULATOR,
    APP_MONITOR,
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
    int scroll;                 /* First visible row (list view) */
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
    SETTINGS_PAGE_USERS,
    SETTINGS_PAGE_WALLPAPER,
    SETTINGS_PAGE_DOCK,
    SETTINGS_PAGE_KEYBOARD,
    SETTINGS_PAGE_MOUSE,
    SETTINGS_PAGE_ABOUT,
    SETTINGS_PAGE_COUNT
} SettingsPage;

typedef struct {
    SettingsPage page;
    int user_field;             /* 0=none, 1=old_pw, 2=new_pw */
    char old_pw[32];
    char new_pw[32];
    char msg[48];
    bool msg_ok;
} SettingsStateUi;

#define TERMINAL_MAX_LINES 48
#define TERMINAL_LINE_MAX  80

typedef struct {
    char lines[TERMINAL_MAX_LINES][TERMINAL_LINE_MAX];
    int line_count;
    int scroll;                  /* First visible history line */
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
    int scroll;                  /* First visible line */
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

/*
 * Web app: an offline page browser.
 * Pages live in /System/Web/<id>.page with a tiny markup:
 *   "# Heading"      - heading line
 *   ">id Label"      - hyperlink to page <id>
 *   anything else    - plain text
 */
#define WEB_MAX_LINES   48
#define WEB_LINE_MAX    88
#define WEB_HISTORY_MAX  8

typedef struct {
    char page_id[64];                    /* Current page id */
    char address[64];                    /* Address bar contents */
    char lines[WEB_MAX_LINES][WEB_LINE_MAX];
    char links[WEB_MAX_LINES][64];       /* Link target per line ("" = none) */
    bool heading[WEB_MAX_LINES];         /* Line is a heading */
    int line_count;
    int scroll;
    char history[WEB_HISTORY_MAX][64];
    int history_count;
    int history_pos;
} WebState;

typedef struct {
    char display[24];
    int64_t acc;
    char op;                    /* 0, '+', '-', '*', '/' */
    bool clear_next;
    bool has_error;
} CalculatorState;

typedef struct {
    AppType type;
    FinderState finder;
    SettingsStateUi settings;
    TerminalState terminal;
    TextEditState textedit;
    TextEditState notes;
    PreviewState preview;
    CalendarState calendar;
    WebState web;
    CalculatorState calc;
} AppWindowState;

static AppWindowState app_states[COMPOSITOR_MAX_WINDOWS];
static int active_window_index = -1;
static int app_window_index[APP_COUNT];

/*
 * Scratch buffer for reordering windows. AppWindowState is ~110KB, so
 * it must NEVER live on the kernel stack (the old raise path copied it
 * to a local and blew past the stack).
 */
static AppWindowState state_scratch;
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

/* Account Setup Assistant & Lock/Login screen state */
static bool login_create_mode = false;
static int login_selected_user = 0;
static int login_active_field = 0;
static char login_full_name[USER_FULLNAME_MAX];
static char login_username[USER_NAME_MAX];
static char login_password[USER_PASS_MAX];
static char login_confirm[USER_PASS_MAX];
static char login_status[64];
static bool login_status_ok = false;

/* Menu bar dropdown state (0=none, 1=ojjy, 2=File, 3=Edit, 4=View, 5=Window) */
static int active_menu = 0;

/* Desktop right-click context menu state */
static bool ctx_menu_open = false;
static int ctx_menu_x = 0;
static int ctx_menu_y = 0;

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
static void raise_window_to_top(int idx);
static int destroy_window_internal(int id);
static void launch_app_index(int index, uint64_t now_ms);
static void finder_set_path(FinderState *state, const char *path);
static bool desktop_handle_click(int x, int y, uint64_t now_ms);
static void web_load_page(WebState *web, const char *page_id);
static void web_go_history(WebState *web, int delta);
static void mark_dirty(void);

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

    int wave_y = (((int)comp_height * 34) / 100) + tri_wave(x, 480, 50);
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

static Color wallpaper_sample_uncached(int x, int y)
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

static inline int clamp_coord(int v, int max)
{
    if (v < 0) return 0;
    if (v >= max) return max - 1;
    return v;
}

static Color wallpaper_sample(int x, int y)
{
    if (wp_cache_valid) {
        x = clamp_coord(x, (int)comp_width);
        y = clamp_coord(y, (int)comp_height);
        return wp_cache[y * comp_width + x];
    }
    return wallpaper_sample_uncached(x, y);
}

/* Blurred wallpaper behind glass. `radius` is kept for API compatibility;
 * the cache uses one fixed radius (WP_BLUR_RADIUS). */
static Color blur_sample(int x, int y, int radius)
{
    (void)radius;
    if (wp_cache_valid) {
        x = clamp_coord(x, (int)comp_width);
        y = clamp_coord(y, (int)comp_height);
        return wp_blur[y * comp_width + x];
    }
    return wallpaper_sample(x, y);
}

/*
 * One pass of a box blur over `count` pixels spaced `stride` apart,
 * reading from src and writing to dst (src and dst may be the same array
 * because the line is first copied into wp_line_tmp).
 */
static void box_blur_line(Color *buf, int count, int stride, int radius)
{
    for (int i = 0; i < count; i++) {
        wp_line_tmp[i] = buf[i * stride];
    }

    int window = radius * 2 + 1;
    int sr = 0, sg = 0, sb = 0;
    /* Prime the running sum with edge-clamped samples */
    for (int k = -radius; k <= radius; k++) {
        Color c = wp_line_tmp[clamp_coord(k, count)];
        sr += (c >> 16) & 0xFF;
        sg += (c >> 8) & 0xFF;
        sb += c & 0xFF;
    }

    for (int i = 0; i < count; i++) {
        buf[i * stride] = RGB(sr / window, sg / window, sb / window);

        Color out = wp_line_tmp[clamp_coord(i - radius, count)];
        Color in = wp_line_tmp[clamp_coord(i + radius + 1, count)];
        sr += (int)((in >> 16) & 0xFF) - (int)((out >> 16) & 0xFF);
        sg += (int)((in >> 8) & 0xFF) - (int)((out >> 8) & 0xFF);
        sb += (int)(in & 0xFF) - (int)(out & 0xFF);
    }
}

/*
 * Rebuild the screen-sized wallpaper + blur caches. Called whenever the
 * wallpaper image or the theme (procedural fallback colors) changes.
 */
static void wallpaper_rebuild_cache(void)
{
    wp_cache_valid = false;
    uint32_t w = comp_width, h = comp_height;
    if (w == 0 || h == 0 || (uint64_t)w * h > WP_CACHE_MAX_PIXELS ||
        w > WP_CACHE_MAX_DIM || h > WP_CACHE_MAX_DIM) {
        return;   /* Fall back to the slow uncached path */
    }

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            Color c = wallpaper_sample_uncached((int)x, (int)y);
            wp_cache[y * w + x] = c;
            wp_blur[y * w + x] = c;
        }
    }

    /* Separable box blur, two passes each way (approximates a Gaussian) */
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t y = 0; y < h; y++) {
            box_blur_line(&wp_blur[y * w], (int)w, 1, WP_BLUR_RADIUS);
        }
        for (uint32_t x = 0; x < w; x++) {
            box_blur_line(&wp_blur[x], (int)h, (int)w, WP_BLUR_RADIUS);
        }
    }

    wp_cache_valid = true;
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
    fb_draw_string(content_x + 12, content_y + 92, "Home", theme->accent, theme->dock_tint);
    fb_draw_string(content_x + 12, content_y + 112, "Desktop", theme->text, theme->dock_tint);

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

        int visible = (content_h - 48) / 18;
        if (visible < 1) visible = 1;
        int max_scroll = state->entry_count - visible;
        if (max_scroll < 0) max_scroll = 0;
        if (state->scroll > max_scroll) state->scroll = max_scroll;
        if (state->scroll < 0) state->scroll = 0;

        int row_y = content_y + 44;
        for (int i = state->scroll; i < state->entry_count && i < state->scroll + visible; i++) {
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
        int rows = (content_h - 48) / (icon + 28);
        if (rows < 1) rows = 1;
        int visible = cols * rows;
        int max_scroll = state->entry_count - visible;
        if (max_scroll < 0) max_scroll = 0;
        if (state->scroll > max_scroll) state->scroll = max_scroll;
        if (state->scroll < 0) state->scroll = 0;

        int start_x = main_x + 16;
        int start_y = content_y + 40;
        for (int i = state->scroll; i < state->entry_count && i < state->scroll + visible; i++) {
            int col = (i - state->scroll) % cols;
            int row = (i - state->scroll) / cols;
            int ix = start_x + col * (icon + gap);
            int iy = start_y + row * (icon + 28);
            FinderEntry *entry = &state->entries[i];
            bool drew_icon = false;
            if (state->drag_active && i == state->drag_hover_index && entry->type == VFS_TYPE_DIR) {
                draw_rounded_rect_blend(ix - 2, iy - 2, icon + 4, icon + 4, 8, theme->accent_soft, 40);
            }
            if (entry->type == VFS_TYPE_BUNDLE) {
                /* Use the registry's already-loaded icon: never re-parse
                 * the manifest on every frame (old code did a full
                 * bundle_load per icon per frame) */
                char path[256];
                if (vfs_join_path(path, sizeof(path), state->path, entry->name) == 0) {
                    for (int a = 0; a < app_registry_count(); a++) {
                        AppInfo *app = app_registry_get(a);
                        if (app && strcmp(app->path, path) == 0 && app->icon.valid) {
                            draw_icon_scaled(app->icon.pixels, BUNDLE_ICON_SIZE, ix, iy, icon);
                            drew_icon = true;
                            break;
                        }
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

static void mask_password_str(const char *src, char *dst, size_t dst_size)
{
    size_t n = 0;
    while (src[n] && n + 1 < dst_size) {
        dst[n++] = '*';
    }
    dst[n] = '\0';
}

static void draw_settings_window(SettingsStateUi *state, int content_x, int content_y, int content_w, int content_h)
{
    int sidebar_w = 160;
    draw_rounded_rect_blend(content_x, content_y, sidebar_w, content_h, 12, theme->dock_tint, 130);

    const char *items[] = {
        "Appearance",
        "Users & Accounts",
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
                       settings->dark_mode ? "Theme: Dark" : "Theme: Light", theme->text_muted, theme->dock_tint);
        fb_fill_rect(main_x + 210, content_y + 38, 40, 14, settings->dark_mode ? theme->accent : theme->accent_soft);

        fb_draw_string(main_x + 16, content_y + 66,
                       settings->time_24h ? "Clock: 24-hour" : "Clock: 12-hour",
                       theme->text_muted, theme->dock_tint);
        fb_fill_rect(main_x + 210, content_y + 62, 40, 14, settings->time_24h ? theme->accent : theme->accent_soft);

        fb_draw_string(main_x + 16, content_y + 90,
                       settings->reduced_transparency ? "Reduced Transparency: On" : "Reduced Transparency: Off",
                       theme->text_muted, theme->dock_tint);
        fb_fill_rect(main_x + 230, content_y + 86, 40, 14,
                     settings->reduced_transparency ? theme->accent : theme->accent_soft);

        fb_draw_string(main_x + 16, content_y + 114,
                       settings->reduced_motion ? "Reduced Motion: On" : "Reduced Motion: Off",
                       theme->text_muted, theme->dock_tint);
        fb_fill_rect(main_x + 230, content_y + 110, 40, 14,
                     settings->reduced_motion ? theme->accent : theme->accent_soft);
    } else if (state->page == SETTINGS_PAGE_USERS) {
        fb_draw_string(main_x + 16, content_y + 14, "Users & Credentials", theme->text, theme->dock_tint);

        /* Active user card */
        draw_rounded_rect_blend(main_x + 14, content_y + 34, main_w - 28, 52, 10, theme->glass_aqua, 120);
        draw_rounded_rect_blend(main_x + 22, content_y + 42, 36, 36, 10, theme->accent, 230);
        char initials[3] = { 'U', '\0', '\0' };
        const char *fn = users_current_full_name();
        if (fn && fn[0]) initials[0] = (fn[0] >= 'a' && fn[0] <= 'z') ? (char)(fn[0] - 32) : fn[0];
        fb_draw_string(main_x + 36, content_y + 54, initials, COLOR_WHITE, theme->accent);

        char uline[96];
        uline[0] = '\0';
        str_append(uline, sizeof(uline), fn);
        str_append(uline, sizeof(uline), " (@");
        str_append(uline, sizeof(uline), users_current_username());
        str_append(uline, sizeof(uline), ")");
        fb_draw_string(main_x + 68, content_y + 44, uline, theme->text, theme->glass_aqua);
        fb_draw_string(main_x + 68, content_y + 62, users_current_home(), theme->text_muted, theme->glass_aqua);

        /* Password change form */
        fb_draw_string(main_x + 16, content_y + 98, "Change Password (SHA-256):", theme->text, theme->dock_tint);

        fb_draw_string(main_x + 16, content_y + 122, "Current:", theme->text_muted, theme->dock_tint);
        draw_rounded_rect_blend(main_x + 92, content_y + 118, 150, 20, 6,
                                state->user_field == 1 ? theme->accent_soft : theme->glass_aqua, 170);
        char masked[32];
        mask_password_str(state->old_pw, masked, sizeof(masked));
        fb_draw_string(main_x + 98, content_y + 122,
                       masked[0] ? masked : (state->user_field == 1 ? "|" : "click to type"),
                       masked[0] ? theme->text : theme->text_muted, theme->glass_aqua);

        fb_draw_string(main_x + 16, content_y + 148, "New PW:", theme->text_muted, theme->dock_tint);
        draw_rounded_rect_blend(main_x + 92, content_y + 144, 150, 20, 6,
                                state->user_field == 2 ? theme->accent_soft : theme->glass_aqua, 170);
        mask_password_str(state->new_pw, masked, sizeof(masked));
        fb_draw_string(main_x + 98, content_y + 148,
                       masked[0] ? masked : (state->user_field == 2 ? "|" : "click to type"),
                       masked[0] ? theme->text : theme->text_muted, theme->glass_aqua);

        draw_rounded_rect_blend(main_x + 250, content_y + 130, 86, 24, 8, theme->accent, 210);
        fb_draw_string(main_x + 260, content_y + 136, "Save PW", COLOR_WHITE, theme->accent);

        if (state->msg[0]) {
            fb_draw_string(main_x + 16, content_y + 172, state->msg,
                           state->msg_ok ? theme->accent : RGB(220, 72, 68), theme->dock_tint);
        }

        /* Accounts list */
        fb_draw_string(main_x + 16, content_y + 194, "Registered Accounts:", theme->text_muted, theme->dock_tint);
        int ucount = users_count();
        for (int ui = 0; ui < ucount && ui < 4; ui++) {
            const UserAccount *acc = users_get_by_index(ui);
            if (!acc) continue;
            char aline[96];
            aline[0] = '\0';
            str_append(aline, sizeof(aline), acc->username);
            str_append(aline, sizeof(aline), " - ");
            str_append(aline, sizeof(aline), acc->full_name);
            if (acc->is_admin) str_append(aline, sizeof(aline), " [Admin]");
            fb_draw_string(main_x + 24, content_y + 212 + ui * 16, aline, theme->text, theme->dock_tint);
        }

        /* Bottom action buttons: Add Account, Lock Screen, Switch User */
        int by = content_y + content_h - 36;
        draw_rounded_rect_blend(main_x + 16, by, 112, 24, 8, theme->accent, 210);
        fb_draw_string(main_x + 24, by + 6, "+ Add Account", COLOR_WHITE, theme->accent);

        draw_rounded_rect_blend(main_x + 136, by, 96, 24, 8, theme->accent_soft, 180);
        fb_draw_string(main_x + 144, by + 6, "Lock Screen", theme->text, theme->accent_soft);

        draw_rounded_rect_blend(main_x + 240, by, 98, 24, 8, theme->accent_soft, 180);
        fb_draw_string(main_x + 248, by + 6, "Switch User", theme->text, theme->accent_soft);
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

    int visible = (content_h - 40) / 16;
    if (visible < 1) visible = 1;
    int max_scroll = term->line_count - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (term->scroll > max_scroll) term->scroll = max_scroll;
    if (term->scroll < 0) term->scroll = 0;
    /* Follow new output unless the user scrolled up */
    if (term->scroll == max_scroll - 1) term->scroll = max_scroll;

    int line_y = content_y + 10;
    for (int i = term->scroll; i < term->line_count && i < term->scroll + visible; i++) {
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

    int visible = (content_h - 60) / 16;
    if (visible < 1) visible = 1;
    int max_scroll = edit->line_count - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (edit->scroll > max_scroll) edit->scroll = max_scroll;
    if (edit->scroll < 0) edit->scroll = 0;
    /* Keep the cursor line in view while typing */
    if (edit->cursor_line < edit->scroll) edit->scroll = edit->cursor_line;
    if (edit->cursor_line >= edit->scroll + visible) {
        edit->scroll = edit->cursor_line - visible + 1;
    }

    int line_y = content_y + 30;
    for (int i = edit->scroll; i < edit->line_count && i < edit->scroll + visible; i++) {
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
        /* calendar_events_for_day returns the TOTAL match count; only the
         * first 10 indices are filled. Never iterate past the array. */
        int shown = MIN(count, 10);
        fb_draw_string(grid_x + 10, grid_y + 6, "Day", theme->text_muted, theme->dock_tint);
        int ey = grid_y + 24;
        for (int i = 0; i < shown; i++) {
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

/* ================================================================== */
/* Web app (offline browser)                                           */
/* ================================================================== */

/*
 * Load a page from /System/Web/<id>.page into the render model.
 * Missing pages render as a "not found" page with a link home.
 */
static void web_load_page(WebState *web, const char *page_id)
{
    if (!web || !page_id || !page_id[0]) return;

    /* Validate the id: [A-Za-z0-9-_] only (no path traversal) */
    for (const char *p = page_id; *p; p++) {
        bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '-' || *p == '_';
        if (!ok) return;
    }

    web->line_count = 0;
    web->scroll = 0;
    for (int i = 0; i < WEB_MAX_LINES; i++) {
        web->lines[i][0] = '\0';
        web->links[i][0] = '\0';
        web->heading[i] = false;
    }

    char path[128];
    if (vfs_join_path(path, sizeof(path), "/System/Web", page_id) != 0) return;
    str_append(path, sizeof(path), ".page");

    VfsFile *file = vfs_open(path, VFS_O_READ);
    if (!file) {
        /* Not-found page */
        strncpy(web->page_id, page_id, sizeof(web->page_id) - 1);
        web->page_id[sizeof(web->page_id) - 1] = '\0';
        int n = web->line_count++;
        strncpy(web->lines[n], "Page not found", WEB_LINE_MAX - 1);
        web->heading[n] = true;
        n = web->line_count++;
        strncpy(web->lines[n], "The page you requested is not on this disk.", WEB_LINE_MAX - 1);
        n = web->line_count++;
        strncpy(web->lines[n], "", 1);
        n = web->line_count++;
        strncpy(web->lines[n], "Go back to the start page", WEB_LINE_MAX - 1);
        strncpy(web->links[n], "home", sizeof(web->links[n]) - 1);
        return;
    }

    char line[128];
    ssize_t bytes;
    char pending[WEB_LINE_MAX];
    int pending_len = 0;

    while ((bytes = vfs_read(file, line, sizeof(line))) > 0) {
        for (ssize_t i = 0; i < bytes; i++) {
            char c = line[i];
            if (c == '\n' || pending_len >= (int)sizeof(pending) - 1) {
                pending[pending_len] = '\0';
                pending_len = 0;
                if (web->line_count >= WEB_MAX_LINES) goto done;

                if (pending[0] == '\0') {
                    int n = web->line_count++;
                    web->lines[n][0] = '\0';  /* Blank spacer line */
                } else if (pending[0] == '#' && pending[1] == ' ') {
                    int n = web->line_count++;
                    strncpy(web->lines[n], pending + 2, WEB_LINE_MAX - 1);
                    web->lines[n][WEB_LINE_MAX - 1] = '\0';
                    web->heading[n] = true;
                } else if (pending[0] == '>') {
                    /* Link line: ">target Label" */
                    const char *target = pending + 1;
                    const char *space = str_find(target, " ");
                    int n = web->line_count++;
                    if (space && space > target) {
                        size_t tlen = (size_t)(space - target);
                        if (tlen >= sizeof(web->links[n])) tlen = sizeof(web->links[n]) - 1;
                        memcpy(web->links[n], target, tlen);
                        web->links[n][tlen] = '\0';
                        while (*space == ' ') space++;
                        strncpy(web->lines[n], space, WEB_LINE_MAX - 1);
                        web->lines[n][WEB_LINE_MAX - 1] = '\0';
                    } else {
                        strncpy(web->links[n], target, sizeof(web->links[n]) - 1);
                        strncpy(web->lines[n], target, WEB_LINE_MAX - 1);
                    }
                } else {
                    int n = web->line_count++;
                    strncpy(web->lines[n], pending, WEB_LINE_MAX - 1);
                    web->lines[n][WEB_LINE_MAX - 1] = '\0';
                }
            } else if (c != '\r') {
                pending[pending_len++] = c;
            }
        }
    }
done:
    /* Final line without trailing newline */
    if (pending_len > 0 && web->line_count < WEB_MAX_LINES) {
        pending[pending_len] = '\0';
        int n = web->line_count++;
        strncpy(web->lines[n], pending, WEB_LINE_MAX - 1);
        web->lines[n][WEB_LINE_MAX - 1] = '\0';
    }
    vfs_close(file);

    strncpy(web->page_id, page_id, sizeof(web->page_id) - 1);
    web->page_id[sizeof(web->page_id) - 1] = '\0';

    /* Push history (forward history is discarded, like a browser) */
    if (web->history_count < WEB_HISTORY_MAX) {
        strncpy(web->history[web->history_count++], page_id, sizeof(web->history[0]) - 1);
        web->history_pos = web->history_count - 1;
    } else {
        for (int i = 1; i < WEB_HISTORY_MAX; i++) {
            memcpy(web->history[i - 1], web->history[i], sizeof(web->history[0]));
        }
        strncpy(web->history[WEB_HISTORY_MAX - 1], page_id, sizeof(web->history[0]) - 1);
        web->history_pos = WEB_HISTORY_MAX - 1;
    }
}

/* Move back/forward in history */
static void web_go_history(WebState *web, int delta)
{
    if (!web) return;
    int pos = web->history_pos + delta;
    if (pos < 0 || pos >= web->history_count) return;
    web->history_pos = pos;

    /* Re-render without re-pushing history */
    char page[64];
    strncpy(page, web->history[pos], sizeof(page) - 1);
    page[sizeof(page) - 1] = '\0';

    int saved_pos = web->history_pos;
    int saved_count = web->history_count;
    web_load_page(web, page);
    web->history_pos = saved_pos;
    web->history_count = saved_count;
}

static void draw_web_window(WebState *web, int content_x, int content_y, int content_w, int content_h)
{
    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 10, theme->dock_tint, 110);

    /* Toolbar: back / forward / address bar */
    int bar_y = content_y + 8;
    int bar_h = 22;
    draw_rounded_rect_blend(content_x + 8, bar_y, 22, bar_h, 6, theme->accent_soft, 120);
    fb_draw_string(content_x + 14, bar_y + 4, "<", theme->text, theme->dock_tint);
    draw_rounded_rect_blend(content_x + 34, bar_y, 22, bar_h, 6, theme->accent_soft, 120);
    fb_draw_string(content_x + 40, bar_y + 4, ">", theme->text, theme->dock_tint);

    int addr_x = content_x + 62;
    int addr_w = content_w - 70;
    draw_rounded_rect_blend(addr_x, bar_y, addr_w, bar_h, 6, theme->glass_aqua, 160);
    char addr[96];
    addr[0] = '\0';
    str_append(addr, sizeof(addr), "ojjy://");
    str_append(addr, sizeof(addr), web->address[0] ? web->address : web->page_id);
    fb_draw_string(addr_x + 8, bar_y + 4, addr, theme->text, theme->dock_tint);

    /* Page content */
    int page_y = content_y + 40;
    int page_h = content_h - 58;
    draw_rounded_rect_blend(content_x + 8, page_y, content_w - 16, page_h, 10,
                            theme->glass_aqua, 120);

    int visible = (page_h - 12) / 18;
    if (visible < 1) visible = 1;
    int max_scroll = web->line_count - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (web->scroll > max_scroll) web->scroll = max_scroll;
    if (web->scroll < 0) web->scroll = 0;

    int line_y = page_y + 8;
    for (int i = web->scroll; i < web->line_count && i < web->scroll + visible; i++) {
        if (!web->lines[i][0]) {
            line_y += 18;
            continue;
        }
        if (web->links[i][0]) {
            /* Hyperlinks: accent blue with an underline */
            fb_draw_string(content_x + 20, line_y, web->lines[i],
                           theme->accent, theme->glass_aqua);
            int underline_w = (int)strlen(web->lines[i]) * FONT_WIDTH;
            fb_fill_rect(content_x + 20, line_y + FONT_HEIGHT - 1, underline_w, 1, theme->accent);
        } else if (web->heading[i]) {
            fb_draw_string(content_x + 16, line_y, web->lines[i],
                           theme->text, theme->glass_aqua);
            int heading_w = (int)strlen(web->lines[i]) * FONT_WIDTH;
            fb_fill_rect(content_x + 16, line_y + FONT_HEIGHT + 1, heading_w, 2, theme->accent_soft);
        } else {
            fb_draw_string(content_x + 16, line_y, web->lines[i],
                           theme->text, theme->glass_aqua);
        }
        line_y += 18;
    }

    /* Status bar */
    char status[96];
    status[0] = '\0';
    str_append(status, sizeof(status), "Offline page  -  ojjyOS Web");
    fb_draw_string(content_x + 12, content_y + content_h - 16, status,
                   theme->text_muted, theme->dock_tint);
}

/* ================================================================== */
/* About window                                                        */
/* ================================================================== */

static void draw_about_window(int content_x, int content_y, int content_w, int content_h)
{
    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 10, theme->dock_tint, 110);

    /* Wave banner */
    {
        static const char *about_waves[] = {
            "    ~  ~  ojjyOS  ~  ~    ",
            "  ~~~  ~  ~  ~  ~  ~  ~~~  ",
        };
        int max_w = (int)strlen(about_waves[1]) * FONT_WIDTH;
        int wx = content_x + (content_w - max_w) / 2;
        fb_draw_string(wx, content_y + 10, about_waves[0], COLOR_SKY_BLUE, theme->dock_tint);
        fb_draw_string(wx, content_y + 26, about_waves[1], theme->accent_soft, theme->dock_tint);
    }

    int cx = content_x + 24;
    fb_draw_string(cx, content_y + 16, "ojjyOS", theme->text, theme->dock_tint);
    fb_draw_string(cx, content_y + 36, "Version 3.1.0", theme->text_muted, theme->dock_tint);
    fb_draw_string(cx, content_y + 56, "Created by Jonas Lee", theme->accent, theme->dock_tint);

    int rule_y = content_y + 76;
    fb_fill_rect(cx, rule_y, content_w - 48, 1, theme->accent_soft);

    char buf[96];

    fb_draw_string(cx, content_y + 92, "A custom operating system with a",
                   theme->text, theme->dock_tint);
    fb_draw_string(cx, content_y + 110, "macOS Tahoe-inspired user interface.",
                   theme->text, theme->dock_tint);
    fb_draw_string(cx, content_y + 130, "Built from scratch - not based on",
                   theme->text_muted, theme->dock_tint);
    fb_draw_string(cx, content_y + 148, "Linux, BSD, Windows or macOS.",
                   theme->text_muted, theme->dock_tint);

    utoa(fb_get_width(), buf, 10);
    str_append(buf, sizeof(buf), " x ");
    char num[16];
    utoa(fb_get_height(), num, 10);
    str_append(buf, sizeof(buf), num);
    str_append(buf, sizeof(buf), " display");
    fb_draw_string(cx, content_y + 176, buf, theme->text_muted, theme->dock_tint);

    buf[0] = '\0';
    utoa(pmm_get_total_memory() / (1024 * 1024), num, 10);
    str_append(buf, sizeof(buf), num);
    str_append(buf, sizeof(buf), " MB memory");
    fb_draw_string(cx, content_y + 194, buf, theme->text_muted, theme->dock_tint);
}

/* ================================================================== */
/* Calculator app                                                      */
/* ================================================================== */

static void calc_reset(CalculatorState *calc)
{
    if (!calc) return;
    strcpy(calc->display, "0");
    calc->acc = 0;
    calc->op = 0;
    calc->clear_next = true;
    calc->has_error = false;
}

static int64_t calc_parse_display(const CalculatorState *calc)
{
    if (!calc || !calc->display[0]) return 0;
    const char *p = calc->display;
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    int64_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    return neg ? -v : v;
}

static void calc_format_int64(int64_t v, char *out, size_t size)
{
    if (!out || size < 4) return;
    out[0] = '\0';
    uint64_t u;
    if (v < 0) {
        str_append(out, size, "-");
        u = (uint64_t)(-v);
    } else {
        u = (uint64_t)v;
    }
    char num[24];
    utoa(u, num, 10);
    str_append(out, size, num);
}

static void calc_press_digit(CalculatorState *calc, char d)
{
    if (!calc || d < '0' || d > '9') return;
    if (calc->has_error) calc_reset(calc);
    if (calc->clear_next || strcmp(calc->display, "0") == 0) {
        calc->display[0] = d;
        calc->display[1] = '\0';
        calc->clear_next = false;
    } else {
        size_t len = strlen(calc->display);
        if (len < 16) {
            calc->display[len] = d;
            calc->display[len + 1] = '\0';
        }
    }
}

static void calc_eval_pending(CalculatorState *calc)
{
    if (!calc || calc->has_error) return;
    int64_t cur = calc_parse_display(calc);
    if (calc->op == '+') calc->acc = calc->acc + cur;
    else if (calc->op == '-') calc->acc = calc->acc - cur;
    else if (calc->op == '*') calc->acc = calc->acc * cur;
    else if (calc->op == '/') {
        if (cur == 0) {
            strcpy(calc->display, "Error: Div 0");
            calc->has_error = true;
            calc->op = 0;
            calc->clear_next = true;
            return;
        }
        calc->acc = calc->acc / cur;
    } else {
        calc->acc = cur;
    }
    calc_format_int64(calc->acc, calc->display, sizeof(calc->display));
}

static void calc_press_op(CalculatorState *calc, char op)
{
    if (!calc) return;
    if (calc->has_error) calc_reset(calc);
    if (!calc->clear_next || calc->op == 0) {
        calc_eval_pending(calc);
    }
    calc->op = op;
    calc->clear_next = true;
}

static void calc_press_eq(CalculatorState *calc)
{
    if (!calc) return;
    if (calc->op != 0) {
        calc_eval_pending(calc);
        calc->op = 0;
        calc->clear_next = true;
    }
}

static void calc_press_neg(CalculatorState *calc)
{
    if (!calc || calc->has_error) return;
    int64_t v = -calc_parse_display(calc);
    calc_format_int64(v, calc->display, sizeof(calc->display));
}

static void draw_calculator_window(CalculatorState *calc, int content_x, int content_y, int content_w, int content_h)
{
    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 10, theme->dock_tint, 120);

    /* LCD readout */
    int lcd_x = content_x + 12;
    int lcd_y = content_y + 10;
    int lcd_w = content_w - 24;
    int lcd_h = 44;
    draw_rounded_rect_blend(lcd_x, lcd_y, lcd_w, lcd_h, 10, theme->midnight, 210);

    if (calc->op) {
        char op_str[4] = { calc->op, '\0', '\0', '\0' };
        fb_draw_string(lcd_x + 10, lcd_y + 8, op_str, theme->accent_soft, theme->midnight);
    }
    int dlen = (int)strlen(calc->display);
    int tx = lcd_x + lcd_w - 12 - dlen * FONT_WIDTH;
    if (tx < lcd_x + 10) tx = lcd_x + 10;
    fb_draw_string(tx, lcd_y + 18, calc->display, COLOR_WHITE, theme->midnight);

    static const char *labels[5][4] = {
        { "C",  "+/-", "%",  "/" },
        { "7",  "8",   "9",  "*" },
        { "4",  "5",   "6",  "-" },
        { "1",  "2",   "3",  "+" },
        { "0",  "00",  "BS", "=" }
    };

    int grid_y = lcd_y + lcd_h + 10;
    int grid_h = content_h - (lcd_h + 28);
    int cell_w = (content_w - 24 - 3 * 6) / 4;
    int cell_h = (grid_h - 4 * 6) / 5;
    if (cell_h < 22) cell_h = 22;

    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            int bx = lcd_x + c * (cell_w + 6);
            int by = grid_y + r * (cell_h + 6);
            Color bg = (c == 3) ? theme->accent :
                       (r == 0 ? theme->accent_soft : theme->glass_aqua);
            uint8_t alpha = (c == 3) ? 220 : 150;
            draw_rounded_rect_blend(bx, by, cell_w, cell_h, 8, bg, alpha);
            const char *lbl = labels[r][c];
            int lx = bx + (cell_w - (int)strlen(lbl) * FONT_WIDTH) / 2;
            int ly = by + (cell_h - FONT_HEIGHT) / 2;
            fb_draw_string(lx, ly, lbl, (c == 3) ? COLOR_WHITE : theme->text, bg);
        }
    }
}

/* ================================================================== */
/* Activity Monitor app                                                */
/* ================================================================== */

static void draw_monitor_window(int content_x, int content_y, int content_w, int content_h)
{
    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 10, theme->dock_tint, 120);

    int cx = content_x + 14;
    fb_draw_string(cx, content_y + 10, "ojjyOS Activity Monitor", theme->text, theme->dock_tint);

#if defined(__aarch64__)
    const char *arch_str = "Arch: Apple Silicon ARM64 (EL1 / HVF)";
#else
    const char *arch_str = "Arch: x86_64 Long Mode";
#endif
    fb_draw_string(cx, content_y + 28, arch_str, theme->accent, theme->dock_tint);

    /* Uptime + Active User + Syscalls */
    uint64_t secs = timer_get_ticks() / 1000;
    char up_line[112];
    char num[24];
    up_line[0] = '\0';
    str_append(up_line, sizeof(up_line), "Uptime: ");
    utoa(secs / 60, num, 10);
    str_append(up_line, sizeof(up_line), num);
    str_append(up_line, sizeof(up_line), "m ");
    utoa(secs % 60, num, 10);
    str_append(up_line, sizeof(up_line), num);
    str_append(up_line, sizeof(up_line), "s  User: ");
    str_append(up_line, sizeof(up_line), users_current_username());
    str_append(up_line, sizeof(up_line), "  Syscalls: ");
    utoa(proc_get_total_syscalls(), num, 10);
    str_append(up_line, sizeof(up_line), num);
    fb_draw_string(cx, content_y + 46, up_line, theme->text_muted, theme->dock_tint);

    /* Physical Memory + Kernel Heap Usage */
    uint64_t total_mb = pmm_get_total_memory() / (1024 * 1024);
    uint64_t free_mb = pmm_get_free_memory() / (1024 * 1024);
    uint64_t used_mb = (total_mb >= free_mb) ? (total_mb - free_mb) : 0;
    if (total_mb == 0) total_mb = 1;

    char mem_line[112];
    mem_line[0] = '\0';
    str_append(mem_line, sizeof(mem_line), "RAM: ");
    utoa(used_mb, num, 10);
    str_append(mem_line, sizeof(mem_line), num);
    str_append(mem_line, sizeof(mem_line), "/");
    utoa(total_mb, num, 10);
    str_append(mem_line, sizeof(mem_line), num);
    str_append(mem_line, sizeof(mem_line), " MB  |  KHeap: ");
    utoa(kheap_get_used() / 1024, num, 10);
    str_append(mem_line, sizeof(mem_line), num);
    str_append(mem_line, sizeof(mem_line), "/");
    utoa(kheap_get_total() / 1024, num, 10);
    str_append(mem_line, sizeof(mem_line), num);
    str_append(mem_line, sizeof(mem_line), " KB (");
    utoa(kheap_get_alloc_count(), num, 10);
    str_append(mem_line, sizeof(mem_line), num);
    str_append(mem_line, sizeof(mem_line), " blks)");
    fb_draw_string(cx, content_y + 66, mem_line, theme->text, theme->dock_tint);

    int bar_w = content_w - 28;
    draw_rounded_rect_blend(cx, content_y + 84, bar_w, 12, 6, theme->glass_aqua, 140);
    int fill_w = (int)((used_mb * (uint64_t)bar_w) / total_mb);
    if (fill_w < 8) fill_w = 8;
    if (fill_w > bar_w) fill_w = bar_w;
    draw_rounded_rect_blend(cx, content_y + 84, fill_w, 12, 6, theme->accent, 220);

    /* Kernel & User Process Table */
    fb_draw_string(cx, content_y + 104, "Active Processes (PID / Task / User / State / Ticks / Mem)",
                   theme->text, theme->dock_tint);
    fb_draw_string(cx + 4,   content_y + 122, "PID",     theme->text_muted, theme->dock_tint);
    fb_draw_string(cx + 48,  content_y + 122, "Process", theme->text_muted, theme->dock_tint);
    fb_draw_string(cx + 188, content_y + 122, "User",    theme->text_muted, theme->dock_tint);
    fb_draw_string(cx + 268, content_y + 122, "State",   theme->text_muted, theme->dock_tint);
    fb_draw_string(cx + 348, content_y + 122, "Ticks",   theme->text_muted, theme->dock_tint);
    fb_draw_string(cx + 416, content_y + 122, "Mem KB",  theme->text_muted, theme->dock_tint);

    int pcount = proc_count();
    int max_rows = (content_h - 168) / 16;
    if (max_rows > 10) max_rows = 10;
    if (max_rows < 3) max_rows = 3;
    for (int i = 0; i < pcount && i < max_rows; i++) {
        const ProcEntry *pe = proc_get_by_index(i);
        if (!pe) continue;
        int ry = content_y + 140 + i * 16;
        utoa(pe->pid, num, 10);
        fb_draw_string(cx + 4,   ry, num, theme->text_muted, theme->dock_tint);
        fb_draw_string(cx + 48,  ry, pe->name, theme->text, theme->dock_tint);
        fb_draw_string(cx + 188, ry, pe->owner, theme->text_muted, theme->dock_tint);
        fb_draw_string(cx + 268, ry, proc_state_name(pe->state),
                       (pe->state == PROC_RUNNING) ? theme->accent : theme->text_muted,
                       theme->dock_tint);
        utoa(pe->cpu_ticks, num, 10);
        fb_draw_string(cx + 348, ry, num, theme->text_muted, theme->dock_tint);
        utoa(pe->memory_bytes / 1024, num, 10);
        fb_draw_string(cx + 416, ry, num, theme->text_muted, theme->dock_tint);
    }

    /* Open Windows / Tasks summary at bottom */
    char task_line[96];
    task_line[0] = '\0';
    str_append(task_line, sizeof(task_line), "Processes: ");
    utoa((uint64_t)pcount, num, 10);
    str_append(task_line, sizeof(task_line), num);
    str_append(task_line, sizeof(task_line), "   Windows: ");
    utoa((uint64_t)window_count, num, 10);
    str_append(task_line, sizeof(task_line), num);
    str_append(task_line, sizeof(task_line), "   Active: ");
    str_append(task_line, sizeof(task_line), active_app_name);
    fb_draw_string(cx, content_y + content_h - 20, task_line, theme->text_muted, theme->dock_tint);
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

    if (settings_get()->reduced_transparency) {
        Color solid = blend(theme->dock_tint, theme->glass_aqua, 215);
        draw_rounded_rect_blend(draw_x, draw_y, draw_w, draw_h, r, solid, 255);
    } else {
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
    }

    draw_rounded_rect_blend(draw_x, draw_y, draw_w, 32, r, theme->accent_soft, highlight);
    fb_draw_string(draw_x + 52, draw_y + 10, win->title,
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
        case APP_ABOUT:
            draw_about_window(content_x, content_y, content_w, content_h);
            break;
        case APP_WEB:
            draw_web_window(&state->web, content_x, content_y, content_w, content_h);
            break;
        case APP_CALCULATOR:
            draw_calculator_window(&state->calc, content_x, content_y, content_w, content_h);
            break;
        case APP_MONITOR:
            draw_monitor_window(content_x, content_y, content_w, content_h);
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

    /* Bottom-right resize grip indicator when not maximized */
    if (!win->maximized) {
        int gx = draw_x + draw_w - 14;
        int gy = draw_y + draw_h - 14;
        for (int k = 0; k < 8; k++) {
            fb_put_pixel(gx + 7 - k, gy + k, theme->text_muted);
            if (k < 5) {
                fb_put_pixel(gx + 7 - k, gy + 3 + k, theme->text_muted);
            }
        }
    }
}

static void draw_wallpaper(void)
{
    if (wp_cache_valid) {
        fb_blit_full(wp_cache);
        return;
    }
    for (uint32_t y = 0; y < comp_height; y++) {
        for (uint32_t x = 0; x < comp_width; x++) {
            fb_put_pixel((int)x, (int)y, wallpaper_sample((int)x, (int)y));
        }
    }
}

static void draw_menu_bar(void)
{
    draw_rounded_rect_blend(0, 0, (int)comp_width, MENU_BAR_HEIGHT, 0,
                            theme->dock_tint, 155);

    /* System 'ojjy' glyph pill */
    if (active_menu == 1) {
        draw_rounded_rect_blend(6, 3, 44, MENU_BAR_HEIGHT - 6, 6, theme->accent, 180);
    }
    fb_draw_string(12, 8, "ojjy", active_menu == 1 ? COLOR_WHITE : theme->accent, theme->dock_tint);

    fb_draw_string(60, 8, active_app_name, theme->text, theme->dock_tint);

    /* Interactive menu items: File (140), Edit (184), View (228), Window (272) */
    const char *menus[4] = { "File", "Edit", "View", "Window" };
    int mx[4] = { 140, 184, 228, 272 };
    for (int i = 0; i < 4; i++) {
        int mw = (int)strlen(menus[i]) * FONT_WIDTH + 10;
        if (active_menu == i + 2) {
            draw_rounded_rect_blend(mx[i] - 5, 3, mw, MENU_BAR_HEIGHT - 6, 6, theme->accent, 170);
        }
        fb_draw_string(mx[i], 8, menus[i],
                       active_menu == i + 2 ? COLOR_WHITE : theme->text_muted, theme->dock_tint);
    }

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

    /* Active user pill + Control + Clock on right */
    const char *uname = users_current_username();
    int user_w = ((int)strlen(uname) + 1) * FONT_WIDTH;
    int status_x = (int)comp_width - 136;
    fb_draw_string(status_x - user_w - 14, 8, uname, theme->accent, theme->dock_tint);
    fb_draw_string(status_x, 8, "Control", theme->text_muted, theme->dock_tint);
    fb_draw_string((int)comp_width - 70, 8, time_buf, theme->text, theme->dock_tint);
}

static void draw_menu_dropdown(void)
{
    if (active_menu <= 0) return;

    const char *items[7];
    int count = 0;
    int x = 8;
    int w = 210;

    if (active_menu == 1) {
        x = 6;
        items[count++] = "About ojjyOS";
        items[count++] = "System Settings...";
        items[count++] = "Activity Monitor";
        items[count++] = "Lock Screen";
        items[count++] = "Switch User / Log Out...";
        items[count++] = "Restart...";
        items[count++] = "Shut Down...";
    } else if (active_menu == 2) {
        x = 136;
        items[count++] = "New Finder Window";
        items[count++] = "New Text Document";
        items[count++] = "Open Home Folder";
        items[count++] = "Save Document (Cmd+S)";
        items[count++] = "Close Window (Cmd+Q)";
    } else if (active_menu == 3) {
        x = 180;
        items[count++] = "Copy (Cmd+C)";
        items[count++] = "Cut (Cmd+X)";
        items[count++] = "Paste (Cmd+V)";
        items[count++] = "Clear Selection";
    } else if (active_menu == 4) {
        x = 224;
        items[count++] = "Toggle Dark / Light";
        items[count++] = "Toggle Transparency";
        items[count++] = "Mission Control (Cmd+M)";
        items[count++] = "Launchpad (Cmd+L)";
    } else if (active_menu == 5) {
        x = 268;
        items[count++] = "Minimize Window";
        items[count++] = "Zoom / Maximize";
        items[count++] = "Calculator";
        items[count++] = "Activity Monitor";
    }

    int h = count * 22 + 12;
    int y = MENU_BAR_HEIGHT + 4;
    draw_shadow(x, y, w, h, 10);
    draw_rounded_rect_blend(x, y, w, h, 10, theme->dock_tint, 225);

    for (int i = 0; i < count; i++) {
        int iy = y + 6 + i * 22;
        if (cursor_x >= x + 4 && cursor_x <= x + w - 4 &&
            cursor_y >= iy && cursor_y < iy + 20) {
            draw_rounded_rect_blend(x + 4, iy, w - 8, 20, 6, theme->accent, 180);
            fb_draw_string(x + 12, iy + 3, items[i], COLOR_WHITE, theme->accent);
        } else {
            fb_draw_string(x + 12, iy + 3, items[i], theme->text, theme->dock_tint);
        }
    }
}

static void draw_context_menu(void)
{
    if (!ctx_menu_open) return;

    static const char *items[7] = {
        "New Folder on Desktop",
        "New Text Note",
        "Open Home in Finder",
        "Open Terminal Here",
        "Open Calculator",
        "Toggle Dark / Light Mode",
        "Lock Screen"
    };
    int count = 7;
    int w = 216;
    int h = count * 22 + 12;
    int x = ctx_menu_x;
    int y = ctx_menu_y;
    if (x + w > (int)comp_width - 8) x = (int)comp_width - w - 8;
    if (y + h > (int)comp_height - 8) y = (int)comp_height - h - 8;

    draw_shadow(x, y, w, h, 10);
    draw_rounded_rect_blend(x, y, w, h, 10, theme->dock_tint, 230);

    for (int i = 0; i < count; i++) {
        int iy = y + 6 + i * 22;
        if (cursor_x >= x + 4 && cursor_x <= x + w - 4 &&
            cursor_y >= iy && cursor_y < iy + 20) {
            draw_rounded_rect_blend(x + 4, iy, w - 8, 20, 6, theme->accent, 180);
            fb_draw_string(x + 12, iy + 3, items[i], COLOR_WHITE, theme->accent);
        } else {
            fb_draw_string(x + 12, iy + 3, items[i], theme->text, theme->dock_tint);
        }
    }
}

static bool login_screen_visible(void)
{
    return (!users_is_authenticated()) || login_create_mode || (users_count() == 0);
}

static void draw_login_screen(void)
{
    /* Dim/tint the Tahoe wallpaper beneath the lock/setup card */
    draw_rounded_rect_blend(0, 0, (int)comp_width, (int)comp_height, 0,
                            theme->midnight, 115);

    /* Top clock display (macOS Tahoe Lock Screen style) */
    RtcTime now;
    rtc_read_time(&now);
    char clock_str[16];
    char num[8];
    clock_str[0] = '\0';
    if (now.hour < 10) str_append(clock_str, sizeof(clock_str), "0");
    utoa(now.hour, num, 10);
    str_append(clock_str, sizeof(clock_str), num);
    str_append(clock_str, sizeof(clock_str), ":");
    if (now.minute < 10) str_append(clock_str, sizeof(clock_str), "0");
    utoa(now.minute, num, 10);
    str_append(clock_str, sizeof(clock_str), num);
    fb_draw_string(((int)comp_width - (int)strlen(clock_str) * FONT_WIDTH) / 2, 42,
                   clock_str, COLOR_WHITE, theme->midnight);

    /* ojjyOS wave banner under the clock */
    {
        static const char *login_waves[] = {
            "    ~  ~  ojjyOS  ~  ~    ",
            "  ~~~  ~  ~  ~  ~  ~  ~~~  ",
        };
        int max_w = 0;
        for (int i = 0; i < 2; i++) {
            int lw = (int)strlen(login_waves[i]) * FONT_WIDTH;
            if (lw > max_w) max_w = lw;
        }
        int wx = ((int)comp_width - max_w) / 2;
        fb_draw_string(wx, 66, login_waves[0], COLOR_SKY_BLUE, theme->midnight);
        fb_draw_string(wx, 82, login_waves[1], COLOR_OCEAN, theme->midnight);
    }

    const char *brand = "ojjyOS Tahoe  •  Created by Jonas Lee";
    fb_draw_string(((int)comp_width - (int)strlen(brand) * FONT_WIDTH) / 2,
                   (int)comp_height - 28, brand, COLOR_TEXT_LIGHT, theme->midnight);

    bool create_mode = login_create_mode || (users_count() == 0);

    if (create_mode) {
        int cw = 440;
        int ch = 340;
        int cx = ((int)comp_width - cw) / 2;
        int cy = ((int)comp_height - ch) / 2;

        draw_shadow(cx, cy, cw, ch, 18);
        draw_rounded_rect_blend(cx, cy, cw, ch, 18, theme->dock_tint, 230);

        const char *title = (users_count() == 0)
            ? "Welcome to ojjyOS — Create Your Account"
            : "Create a New ojjyOS Account";
        fb_draw_string(cx + (cw - (int)strlen(title) * FONT_WIDTH) / 2, cy + 20,
                       title, theme->text, theme->dock_tint);

        const char *sub = "Credentials are salted & SHA-256 hashed on disk";
        fb_draw_string(cx + (cw - (int)strlen(sub) * FONT_WIDTH) / 2, cy + 40,
                       sub, theme->text_muted, theme->dock_tint);

        const char *labels[4] = {
            "Full Name:",
            "Username:",
            "Password:",
            "Confirm:"
        };
        for (int f = 0; f < 4; f++) {
            int fy = cy + 74 + f * 40;
            fb_draw_string(cx + 28, fy + 6, labels[f], theme->text, theme->dock_tint);

            int bx = cx + 132;
            int bw = cw - 160;
            bool focused = (login_active_field == f);
            draw_rounded_rect_blend(bx, fy, bw, 26, 8,
                                    focused ? theme->accent_soft : theme->glass_aqua, 190);
            if (focused) {
                fb_draw_rect(bx, fy, bw, 26, theme->accent);
            }

            char val[40];
            if (f == 0) {
                strncpy(val, login_full_name, sizeof(val) - 1);
                val[sizeof(val) - 1] = '\0';
            } else if (f == 1) {
                strncpy(val, login_username, sizeof(val) - 1);
                val[sizeof(val) - 1] = '\0';
            } else if (f == 2) {
                mask_password_str(login_password, val, sizeof(val));
            } else {
                mask_password_str(login_confirm, val, sizeof(val));
            }

            if (focused && strlen(val) + 1 < sizeof(val)) {
                str_append(val, sizeof(val), "|");
            }
            fb_draw_string(bx + 8, fy + 6, val, theme->text, theme->glass_aqua);
        }

        if (login_status[0]) {
            fb_draw_string(cx + 28, cy + 240, login_status,
                           login_status_ok ? theme->accent : RGB(225, 76, 70), theme->dock_tint);
        } else {
            fb_draw_string(cx + 28, cy + 240, "Press Tab to switch fields, Enter to create",
                           theme->text_muted, theme->dock_tint);
        }

        int btn_y = cy + ch - 46;
        int btn_w = (users_count() > 0) ? 230 : (cw - 56);
        draw_rounded_rect_blend(cx + 28, btn_y, btn_w, 30, 10, theme->accent, 230);
        const char *btxt = "Create Account & Sign In";
        fb_draw_string(cx + 28 + (btn_w - (int)strlen(btxt) * FONT_WIDTH) / 2,
                       btn_y + 8, btxt, COLOR_WHITE, theme->accent);

        if (users_count() > 0) {
            int cancel_x = cx + 28 + btn_w + 12;
            int cancel_w = cw - 56 - btn_w - 12;
            draw_rounded_rect_blend(cancel_x, btn_y, cancel_w, 30, 10, theme->accent_soft, 180);
            fb_draw_string(cancel_x + (cancel_w - 6 * FONT_WIDTH) / 2,
                           btn_y + 8, "Cancel", theme->text, theme->accent_soft);
        }
    } else {
        int cw = 360;
        int ch = 280;
        int cx = ((int)comp_width - cw) / 2;
        int cy = ((int)comp_height - ch) / 2;

        draw_shadow(cx, cy, cw, ch, 18);
        draw_rounded_rect_blend(cx, cy, cw, ch, 18, theme->dock_tint, 230);

        if (login_selected_user < 0 || login_selected_user >= users_count()) {
            login_selected_user = 0;
        }
        const UserAccount *acc = users_get_by_index(login_selected_user);
        const char *fname = acc ? acc->full_name : "User";
        const char *uname = acc ? acc->username : "user";

        /* Avatar circle/rounded square */
        int av_x = cx + (cw - 56) / 2;
        int av_y = cy + 20;
        draw_rounded_rect_blend(av_x, av_y, 56, 56, 18, theme->accent, 235);
        char init[2] = { 'U', '\0' };
        if (fname[0]) init[0] = (fname[0] >= 'a' && fname[0] <= 'z') ? (char)(fname[0] - 32) : fname[0];
        fb_draw_string(av_x + 24, av_y + 20, init, COLOR_WHITE, theme->accent);

        fb_draw_string(cx + (cw - (int)strlen(fname) * FONT_WIDTH) / 2, cy + 86,
                       fname, theme->text, theme->dock_tint);

        char handle[48];
        handle[0] = '@';
        handle[1] = '\0';
        str_append(handle, sizeof(handle), uname);
        if (acc && acc->is_admin) str_append(handle, sizeof(handle), " • Admin");
        fb_draw_string(cx + (cw - (int)strlen(handle) * FONT_WIDTH) / 2, cy + 104,
                       handle, theme->text_muted, theme->dock_tint);

        /* Password box */
        int pw_x = cx + 36;
        int pw_y = cy + 132;
        int pw_w = cw - 72;
        draw_rounded_rect_blend(pw_x, pw_y, pw_w, 28, 8, theme->glass_aqua, 200);
        fb_draw_rect(pw_x, pw_y, pw_w, 28, theme->accent);

        char masked[40];
        mask_password_str(login_password, masked, sizeof(masked));
        if (masked[0]) {
            str_append(masked, sizeof(masked), "|");
            fb_draw_string(pw_x + 10, pw_y + 7, masked, theme->text, theme->glass_aqua);
        } else {
            fb_draw_string(pw_x + 10, pw_y + 7, "Enter Password...", theme->text_muted, theme->glass_aqua);
        }

        if (login_status[0]) {
            fb_draw_string(cx + (cw - (int)strlen(login_status) * FONT_WIDTH) / 2, cy + 168,
                           login_status, login_status_ok ? theme->accent : RGB(225, 76, 70), theme->dock_tint);
        }

        /* Sign In button */
        int sign_y = cy + 190;
        draw_rounded_rect_blend(pw_x, sign_y, pw_w, 28, 8, theme->accent, 230);
        fb_draw_string(pw_x + (pw_w - 7 * FONT_WIDTH) / 2, sign_y + 7,
                       "Sign In", COLOR_WHITE, theme->accent);

        /* Bottom row: Switch User | + New Account */
        int bot_y = cy + ch - 42;
        int half_w = (pw_w - 10) / 2;
        draw_rounded_rect_blend(pw_x, bot_y, half_w, 24, 8, theme->accent_soft, 175);
        fb_draw_string(pw_x + (half_w - 11 * FONT_WIDTH) / 2, bot_y + 5,
                       "Switch User", theme->text, theme->accent_soft);

        draw_rounded_rect_blend(pw_x + half_w + 10, bot_y, half_w, 24, 8, theme->accent_soft, 175);
        fb_draw_string(pw_x + half_w + 10 + (half_w - 13 * FONT_WIDTH) / 2, bot_y + 5,
                       "+ New Account", theme->text, theme->accent_soft);
    }
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

/*
 * Desktop icons: files from /Users/guest/Desktop plus the "ojjyHD"
 * disk icon, macOS-style (anchored top-right, double-click to open).
 */
#define DESKTOP_MAX_ICONS   16
#define DESKTOP_ICON_SIZE  48
#define DESKTOP_CELL_W     84
#define DESKTOP_CELL_H     92
#define DESKTOP_DBLCLICK_MS 400

typedef struct {
    char name[64];
    char path[192];
    VfsFileType type;
    bool is_disk;
} DesktopIcon;

static DesktopIcon desktop_icons[DESKTOP_MAX_ICONS];
static int desktop_icon_count = 0;
static uint64_t desktop_last_reload = 0;
static int desktop_selected = -1;
static uint64_t desktop_last_click_ms = 0;
static int desktop_last_click_idx = -1;

/* Icon cell geometry for row-major layout from the top-right corner */
static void desktop_icon_rect(int idx, int *x, int *y)
{
    int per_col = ((int)comp_height - 90) / DESKTOP_CELL_H;
    if (per_col < 1) per_col = 1;
    int row = idx % per_col;
    int col = idx / per_col;
    *x = (int)comp_width - DESKTOP_CELL_W - 16 - col * DESKTOP_CELL_W;
    *y = 40 + 8 + row * DESKTOP_CELL_H;
}

/* Reload the desktop listing (throttled by the caller) */
static void desktop_reload(void)
{
    desktop_icon_count = 0;
    memset(desktop_icons, 0, sizeof(desktop_icons));

    /* The disk icon always comes first */
    DesktopIcon *disk = &desktop_icons[desktop_icon_count++];
    disk->is_disk = true;
    strcpy(disk->name, "ojjyHD");
    strcpy(disk->path, "/");
    disk->type = VFS_TYPE_DIR;

    char desk_dir[160];
    users_make_home_path(desk_dir, sizeof(desk_dir), "Desktop");
    VfsDir *dir = vfs_opendir(desk_dir);
    if (!dir) return;

    VfsDirEntry entry;
    while (vfs_readdir(dir, &entry) == 0 &&
           desktop_icon_count < DESKTOP_MAX_ICONS) {
        DesktopIcon *icon = &desktop_icons[desktop_icon_count];
        char name_copy[256];
        strncpy(name_copy, entry.name, sizeof(name_copy) - 1);
        name_copy[sizeof(name_copy) - 1] = '\0';

        if (vfs_join_path(icon->path, sizeof(icon->path),
                          desk_dir, name_copy) != 0) {
            continue;
        }
        strncpy(icon->name, name_copy, sizeof(icon->name) - 1);
        icon->name[sizeof(icon->name) - 1] = '\0';
        icon->type = entry.type;
        icon->is_disk = false;
        desktop_icon_count++;
    }
    vfs_closedir(dir);

    if (desktop_selected >= desktop_icon_count) {
        desktop_selected = -1;
    }
}

/* Find which desktop icon (if any) is at this position */
static int desktop_icon_at(int x, int y)
{
    for (int i = 0; i < desktop_icon_count; i++) {
        int ix, iy;
        desktop_icon_rect(i, &ix, &iy);
        if (x >= ix && x <= ix + DESKTOP_ICON_SIZE + 20 &&
            y >= iy && y <= iy + DESKTOP_ICON_SIZE + 26) {
            return i;
        }
    }
    return -1;
}

/* Handle a click on the desktop. Returns true if the click was consumed. */
static bool desktop_handle_click(int x, int y, uint64_t now_ms)
{
    int idx = desktop_icon_at(x, y);
    if (idx < 0) {
        desktop_selected = -1;
        return false;
    }

    desktop_selected = idx;

    /* Double-click detection */
    if (idx == desktop_last_click_idx &&
        now_ms - desktop_last_click_ms < DESKTOP_DBLCLICK_MS) {
        DesktopIcon *icon = &desktop_icons[idx];
        desktop_last_click_ms = 0;
        desktop_last_click_idx = -1;

        if (icon->is_disk) {
            /* Open Finder at the filesystem root */
            int fidx = app_registry_find_by_bundle_id("com.ojjyos.finder");
            if (fidx >= 0) {
                launch_app_index(fidx, now_ms);
            } else {
                app_open_window(APP_FINDER, "Finder");
            }
            int widx = app_window_index[APP_FINDER];
            if (widx >= 0) {
                finder_set_path(&app_states[widx].finder, "/");
            }
        } else if (icon->type == VFS_TYPE_DIR || icon->type == VFS_TYPE_BUNDLE) {
            int fidx = app_registry_find_by_bundle_id("com.ojjyos.finder");
            if (fidx >= 0) {
                launch_app_index(fidx, now_ms);
            } else {
                app_open_window(APP_FINDER, "Finder");
            }
            int widx = app_window_index[APP_FINDER];
            if (widx >= 0) {
                finder_set_path(&app_states[widx].finder, icon->path);
            }
        } else {
            /* Regular file: open in TextEdit */
            strncpy(last_opened_path, icon->path, sizeof(last_opened_path) - 1);
            last_opened_path[sizeof(last_opened_path) - 1] = '\0';
            int tidx = app_registry_find_by_bundle_id("com.ojjyos.textedit");
            if (tidx >= 0) {
                launch_app_index(tidx, now_ms);
            } else {
                app_open_window(APP_TEXTEDIT, "TextEdit");
            }
            int widx = app_window_index[APP_TEXTEDIT];
            if (widx >= 0) {
                textedit_load_file(&app_states[widx].textedit, icon->path);
            }
        }
    } else {
        desktop_last_click_ms = now_ms;
        desktop_last_click_idx = idx;
    }

    return true;
}

static void draw_desktop_icons(void)
{
    /* Reload at most once a second */
    uint64_t now = timer_get_ticks();
    if (desktop_last_reload == 0 || now - desktop_last_reload >= 1000) {
        desktop_reload();
        desktop_last_reload = now;
    }

    for (int i = 0; i < desktop_icon_count; i++) {
        DesktopIcon *icon = &desktop_icons[i];
        int x, y;
        desktop_icon_rect(i, &x, &y);

        if (i == desktop_selected) {
            draw_rounded_rect_blend(x - 6, y - 6, DESKTOP_ICON_SIZE + 32,
                                    DESKTOP_ICON_SIZE + 34, 8, theme->accent, 60);
        }

        if (icon->is_disk) {
            /* Hand-drawn disk icon: blue rounded square with a lighter band */
            draw_rounded_rect_blend(x, y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, 10,
                                    theme->accent, 230);
            draw_rounded_rect_blend(x + 4, y + 4, DESKTOP_ICON_SIZE - 8, 18, 6,
                                    theme->accent_soft, 160);
            fb_draw_string(x + 4, y + 30, "ojjyHD", COLOR_WHITE, theme->accent);
        } else if (icon->type == VFS_TYPE_DIR) {
            if (icon_folder_loaded) {
                draw_icon_scaled(icon_folder, BUNDLE_ICON_SIZE, x, y, DESKTOP_ICON_SIZE);
            } else {
                fb_fill_rect(x, y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, theme->accent_soft);
            }
        } else if (icon->type == VFS_TYPE_BUNDLE) {
            bool drew = false;
            for (int a = 0; a < app_registry_count(); a++) {
                AppInfo *app = app_registry_get(a);
                if (app && strcmp(app->path, icon->path) == 0 && app->icon.valid) {
                    draw_icon_scaled(app->icon.pixels, BUNDLE_ICON_SIZE, x, y, DESKTOP_ICON_SIZE);
                    drew = true;
                    break;
                }
            }
            if (!drew) {
                fb_fill_rect(x, y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, theme->accent_soft);
            }
        } else {
            if (icon_file_loaded) {
                draw_icon_scaled(icon_file, BUNDLE_ICON_SIZE, x, y, DESKTOP_ICON_SIZE);
            } else {
                fb_fill_rect(x, y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, theme->accent_soft);
            }
        }

        fb_draw_string(x, y + DESKTOP_ICON_SIZE + 6, icon->name,
                       theme->text, blend(theme->midnight, theme->accent, 30));
    }
}

static void draw_spotlight(int anim)
{
    int x = ((int)comp_width - SPOTLIGHT_WIDTH) / 2;
    int y = 120 - overlay_offset(anim, 24);
    int r = 16;

    uint8_t panel_alpha = overlay_alpha(80, anim);
    draw_shadow(x, y, SPOTLIGHT_WIDTH, SPOTLIGHT_HEIGHT, r);
    draw_rounded_rect_blend(x, y, SPOTLIGHT_WIDTH, SPOTLIGHT_HEIGHT, r, theme->glass_aqua, panel_alpha);
    fb_draw_string(x + 18, y + 20, spotlight_query[0] ? spotlight_query : "Search", theme->text,
                   blend(theme->glass_aqua, theme->dock_tint, 60));

    int list_y = y + SPOTLIGHT_HEIGHT + 8;
    /* No stray empty panel when there are no results */
    int list_h = spotlight_count > 0 ? spotlight_count * 28 + 12 : 0;
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
    if (settings_get()->reduced_transparency) {
        draw_rounded_rect_blend(0, 0, (int)comp_width, (int)comp_height, 0,
                                theme->dock_tint, overlay_alpha(230, anim));
    } else {
        int blur_radius = theme->glass.blur_px[2];
        for (uint32_t y = 0; y < comp_height; y++) {
            for (uint32_t x = 0; x < comp_width; x++) {
                Color blurred = blur_sample((int)x, (int)y, blur_radius);
                Color blended = blend(blurred, theme->dock_tint, 80);
                Color base = fb_get_pixel((int)x, (int)y);
                fb_put_pixel((int)x, (int)y, blend(base, blended, overlay_alpha(220, anim)));
            }
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
                            theme->dock_tint, overlay_alpha(165, anim));

    SettingsState *settings = settings_get();

    fb_draw_string(x + 16, y + 12, "Control Center", theme->text, theme->dock_tint);

    int toggle_y = y + 40;
    fb_draw_string(x + 16, toggle_y,
                   settings->wifi_enabled ? "Wi-Fi: On (no radio)" : "Wi-Fi: Off (no radio)",
                   theme->text_muted, theme->dock_tint);
    fb_draw_string(x + 16, toggle_y + 22,
                   settings->bluetooth_enabled ? "Bluetooth: On (no radio)" : "Bluetooth: Off (no radio)",
                   theme->text_muted, theme->dock_tint);
    fb_draw_string(x + 16, toggle_y + 44,
                   settings->dark_mode ? "Appearance: Dark" : "Appearance: Light",
                   theme->text, theme->dock_tint);
    fb_draw_string(x + 16, toggle_y + 66,
                   settings->reduced_transparency ? "Transparency: Reduced" : "Transparency: Glass",
                   theme->text, theme->dock_tint);

    /* Real power controls: Restart and Shut Down */
    int btn_y = y + CONTROL_CENTER_HEIGHT - 44;
    int btn_w = (CONTROL_CENTER_WIDTH - 40) / 2;
    int btn_h = 28;
    draw_rounded_rect_blend(x + 16, btn_y, btn_w, btn_h, 8, theme->accent_soft, overlay_alpha(180, anim));
    fb_draw_string(x + 16 + (btn_w - 7 * 8) / 2, btn_y + 8, "Restart", theme->text, theme->accent_soft);

    draw_rounded_rect_blend(x + 24 + btn_w, btn_y, btn_w, btn_h, 8, RGB(215, 76, 70), overlay_alpha(190, anim));
    fb_draw_string(x + 24 + btn_w + (btn_w - 9 * 8) / 2, btn_y + 8, "Shut Down", COLOR_WHITE, RGB(215, 76, 70));
}

static void mission_control_card_rect(int idx, int total, int *cx, int *cy, int *cw, int *ch)
{
    int cols = (total <= 4) ? total : 4;
    if (cols < 1) cols = 1;
    int rows = (total + cols - 1) / cols;
    int card_w = 210;
    int card_h = 132;
    int gap_x = 24;
    int gap_y = 24;

    int row = idx / cols;
    int col = idx % cols;
    int cols_in_row = (row == rows - 1 && (total % cols) != 0) ? (total % cols) : cols;

    int row_w = cols_in_row * card_w + (cols_in_row - 1) * gap_x;
    int total_h = rows * card_h + (rows - 1) * gap_y;

    int start_x = ((int)comp_width - row_w) / 2;
    int start_y = ((int)comp_height - total_h) / 2;

    *cx = start_x + col * (card_w + gap_x);
    *cy = start_y + row * (card_h + gap_y);
    *cw = card_w;
    *ch = card_h;
}

static void draw_mission_control(int anim)
{
    draw_rounded_rect_blend(0, 0, (int)comp_width, (int)comp_height, 0,
                            theme->dock_tint, overlay_alpha(160, anim));

    const char *header = "Mission Control";
    fb_draw_string(((int)comp_width - (int)strlen(header) * 8) / 2, 64,
                   header, theme->text, theme->dock_tint);

    if (window_count == 0) {
        const char *empty = "No open windows — click anywhere or press Esc to return";
        fb_draw_string(((int)comp_width - (int)strlen(empty) * 8) / 2, (int)comp_height / 2,
                       empty, theme->text_muted, theme->dock_tint);
        return;
    }

    for (int i = 0; i < window_count; i++) {
        int cx, cy, cw, ch;
        mission_control_card_rect(i, window_count, &cx, &cy, &cw, &ch);
        cy -= overlay_offset(anim, 16);

        CompositorWindow *win = &windows[i];
        AppWindowState *state = &app_states[i];

        draw_shadow(cx, cy, cw, ch, 14);
        Color card_bg = (i == active_window_index)
            ? blend(theme->glass_aqua, theme->accent, 65)
            : theme->glass_aqua;
        draw_rounded_rect_blend(cx, cy, cw, ch, 14, card_bg, overlay_alpha(210, anim));

        /* Header bar inside card */
        draw_rounded_rect_blend(cx + 8, cy + 8, cw - 16, 24, 8,
                                theme->accent_soft, overlay_alpha(140, anim));

        /* App icon */
        int aidx = app_registry_find_by_name(win->title);
        AppInfo *app = app_registry_get(aidx);
        if (app && app->icon.valid) {
            draw_icon_scaled(app->icon.pixels, BUNDLE_ICON_SIZE, cx + 16, cy + 42, 40);
        } else {
            fb_fill_rect(cx + 16, cy + 42, 40, 40, theme->accent);
        }

        fb_draw_string(cx + 16, cy + 14, win->title, theme->text, theme->accent_soft);

        const char *status_label = win->minimized ? "Minimized" :
                                   (i == active_window_index ? "Active Window" : "Open Window");
        fb_draw_string(cx + 68, cy + 52, status_label, theme->text, card_bg);

        char dim_buf[32];
        char num[12];
        dim_buf[0] = '\0';
        utoa((uint32_t)win->w, num, 10);
        str_append(dim_buf, sizeof(dim_buf), num);
        str_append(dim_buf, sizeof(dim_buf), "x");
        utoa((uint32_t)win->h, num, 10);
        str_append(dim_buf, sizeof(dim_buf), num);
        fb_draw_string(cx + 68, cy + 72, dim_buf, theme->text_muted, card_bg);

        fb_draw_string(cx + 16, cy + ch - 22, "Click to focus", theme->text_muted, card_bg);
        (void)state;
    }
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

    /* Show up to 8 running apps (the old 5-icon cap hid the selection
     * whenever more than 5 apps were running) */
    int shown = MIN(running_count, 8);
    int width = 40 + shown * 64;
    int height = 120;
    int x = ((int)comp_width - width) / 2;
    int y = 160 - overlay_offset(anim, 18);

    draw_shadow(x, y, width, height, 16);
    draw_rounded_rect_blend(x, y, width, height, 16, theme->dock_tint, overlay_alpha(140, anim));

    int base_x = x + 20;
    int icon_y = y + 34;
    for (int i = 0; i < shown; i++) {
        AppInfo *app = app_registry_get(running[i]);
        int icon_x = base_x + i * 64;
        int size = 40;
        if (i == app_switcher_index % shown) {
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

    /* Drawn at 2x so the pointer is clearly visible on a 1:1
     * emulated display (1024x768 ramfb on a Retina screen). */
    for (int row = 0; row < 12; row++) {
        for (int col = 0; col < 8; col++) {
            if (cursor[row] & (0x80 >> col)) {
                fb_fill_rect(x + col * 2, y + row * 2, 2, 2, COLOR_BLACK);
            }
        }
    }
    for (int row = 1; row < 11; row++) {
        for (int col = 1; col < 7; col++) {
            uint8_t inner = cursor[row] & (0x80 >> col);
            uint8_t left = cursor[row] & (0x80 >> (col - 1));
            if (inner && left) {
                fb_fill_rect(x + col * 2, y + row * 2, 2, 2, COLOR_WHITE);
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
    AppInfo *app = app_registry_get(index);
    if (!app) return;

    /* Built-in apps open their window directly. bundle_launch() is only
     * used for console-mode launches (e.g. the About app from the shell):
     * gating window creation on it made every app silently unlaunchable. */
    app->running = true;
    strncpy(active_app_name, app->name, sizeof(active_app_name) - 1);
    active_app_name[sizeof(active_app_name) - 1] = '\0';
    app->bounce_until = now_ms + 600;

    AppType type = app_type_from_bundle(app->bundle.manifest.bundle_id);
    if (type != APP_DEMO) {
        app_open_window(type, app->name);
    } else {
        bundle_launch(&app->bundle);
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

    char cal_path[160];
    users_make_home_path(cal_path, sizeof(cal_path), "Documents/Calendar.txt");
    VfsFile *file = vfs_open(cal_path, VFS_O_READ);
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

    /* A final line without a trailing newline must not be lost */
    if (line_len > 0) {
        line[line_len] = '\0';
        if (line[0] != '\0') {
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
        }
    }
    vfs_close(file);
    cal->loaded = true;
    return true;
}

/*
 * Append a calendar text field, replacing '|' (the record separator)
 * so a user-typed pipe cannot corrupt the file on reload.
 */
static void calendar_append_field(char *line, size_t size, const char *text)
{
    char clean[64];
    size_t j = 0;
    for (const char *p = text; *p && j < sizeof(clean) - 1; p++) {
        clean[j++] = (*p == '|') ? '/' : *p;
    }
    clean[j] = '\0';
    str_append(line, size, clean);
}

static void calendar_save(CalendarState *cal)
{
    if (!cal) return;
    char cal_path[160];
    users_make_home_path(cal_path, sizeof(cal_path), "Documents/Calendar.txt");
    VfsFile *file = vfs_open(cal_path,
                             VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    if (!file) return;

    for (int i = 0; i < cal->event_count; i++) {
        CalendarEvent *ev = &cal->events[i];
        /* Worst case: 10 (date) + 8 (time) + 48 + 48 + 64 + 5 ('|'x4,'\n') ~ 185 */
        char line[256];
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
        calendar_append_field(line, sizeof(line), ev->title);
        str_append(line, sizeof(line), "|");
        calendar_append_field(line, sizeof(line), ev->location);
        str_append(line, sizeof(line), "|");
        calendar_append_field(line, sizeof(line), ev->notes);
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

/*
 * Move the window at `idx` to the top of the z-order.
 * Fixes up drag state, the active-window index, the per-app index and
 * fires the focus hook. (AppWindowState is ~110KB: use the static
 * scratch buffer, never the stack.)
 */
static void raise_window_to_top(int idx)
{
    if (idx < 0 || idx >= window_count) return;
    if (idx == window_count - 1) {
        active_window_index = idx;
        return;
    }

    /* Park the moving window's state in the static scratch buffer */
    CompositorWindow temp_win = windows[idx];
    memcpy(&state_scratch, &app_states[idx], sizeof(AppWindowState));

    for (int j = idx; j < window_count - 1; j++) {
        windows[j] = windows[j + 1];
        memcpy(&app_states[j], &app_states[j + 1], sizeof(AppWindowState));
    }

    windows[window_count - 1] = temp_win;
    memcpy(&app_states[window_count - 1], &state_scratch, sizeof(AppWindowState));

    if (drag_index == idx) drag_index = window_count - 1;
    else if (drag_index > idx) drag_index--;

    if (resize_index == idx) resize_index = window_count - 1;
    else if (resize_index > idx) resize_index--;

    active_window_index = window_count - 1;
    rebuild_app_window_index();

    if (wm_hooks.on_focus) {
        wm_hooks.on_focus(windows[window_count - 1].id);
    }
}

/*
 * Destroy the window with the given id.
 * Returns 0 on success, -1 if the id does not exist.
 */
static int destroy_window_internal(int id)
{
    int idx = -1;
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == id) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return -1;

    AppType closed_type = app_states[idx].type;

    if (wm_hooks.on_destroy) {
        wm_hooks.on_destroy(id);
    }

    /* Shift everything above idx down one slot */
    for (int j = idx; j < window_count - 1; j++) {
        windows[j] = windows[j + 1];
        memcpy(&app_states[j], &app_states[j + 1], sizeof(AppWindowState));
    }
    window_count--;

    memset(&windows[window_count], 0, sizeof(CompositorWindow));
    memset(&app_states[window_count], 0, sizeof(AppWindowState));

    /* Fix up indices */
    if (drag_index == idx) {
        dragging = false;
        drag_index = -1;
    } else if (drag_index > idx) {
        drag_index--;
    }
    if (resize_index == idx) {
        resizing = false;
        resize_index = -1;
    } else if (resize_index > idx) {
        resize_index--;
    }
    if (active_window_index == idx) {
        active_window_index = window_count - 1;
    } else if (active_window_index > idx) {
        active_window_index--;
    }
    rebuild_app_window_index();
    if (closed_type > APP_DEMO && closed_type < APP_COUNT && app_window_index[closed_type] < 0) {
        proc_terminate_app((int)closed_type);
    }
    return 0;
}

/*
 * Close the open window (if any) of a given app type.
 */
static void close_app_window(AppType type)
{
    if (type < 0 || type >= APP_COUNT) return;
    int idx = app_window_index[type];
    if (idx >= 0 && idx < window_count) {
        destroy_window_internal(windows[idx].id);
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
    const char *home = users_current_home();
    size_t home_len = strlen(home);
    char path[128];
    if (strncmp(term->cwd, home, home_len) == 0 &&
        (term->cwd[home_len] == '\0' || term->cwd[home_len] == '/')) {
        strcpy(path, "~");
        strncpy(path + 1, term->cwd + home_len, sizeof(path) - 2);
        path[sizeof(path) - 1] = '\0';
    } else {
        strncpy(path, term->cwd, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    out[0] = '\0';
    str_append(out, size, users_current_username());
    str_append(out, size, "@ojjyos:");
    str_append(out, size, path[0] ? path : "/");
    str_append(out, size, " %");
}

/*
 * Resolve a user-typed path (absolute, ~, ~/..., or relative to cwd)
 * into an absolute path. Returns 0 on success, -1 if the result would
 * not fit in `size` (in which case `out` must not be used).
 */
static int terminal_resolve_path(const TerminalState *term, const char *input, char *out, size_t size)
{
    if (!input || !out || size == 0) return -1;

    if (input[0] == '/') {
        strncpy(out, input, size - 1);
        out[size - 1] = '\0';
        normalize_path(out, size);
        return 0;
    }

    if (strcmp(input, "~") == 0) {
        strncpy(out, users_current_home(), size - 1);
        out[size - 1] = '\0';
        normalize_path(out, size);
        return 0;
    }

    if (strncmp(input, "~/", 2) == 0) {
        users_make_home_path(out, size, input + 2);
        normalize_path(out, size);
        return strlen(out) > 0 ? 0 : -1;
    }

    if (term->cwd[0]) {
        if (vfs_join_path(out, size, term->cwd, input) != 0) {
            return -1;  /* Path too long - out is untouched, do not use it */
        }
    } else {
        out[0] = '\0';
        str_append(out, size, "/");
        str_append(out, size, input);
    }

    normalize_path(out, size);
    return 0;
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

    /* Read the WHOLE file (the old single 63-byte read showed only the
     * first line of any longer file). The line ring drops old output. */
    char line[TERMINAL_LINE_MAX];
    int guard = 0;
    while (guard++ < 200) {
        ssize_t bytes = vfs_read(file, line, sizeof(line) - 1);
        if (bytes <= 0) break;

        size_t start = 0;
        while (start < (size_t)bytes) {
            size_t end = start;
            while (end < (size_t)bytes && line[end] != '\n') end++;
            size_t seg = end - start;
            if (seg > TERMINAL_LINE_MAX - 1) seg = TERMINAL_LINE_MAX - 1;
            char chunk[TERMINAL_LINE_MAX];
            memcpy(chunk, line + start, seg);
            chunk[seg] = '\0';
            terminal_append_line(term, chunk);
            start = end + 1;  /* Skip the newline */
        }
    }
    vfs_close(file);
}

static const char *app_name_from_type(AppType type);

static void terminal_exec(TerminalState *term, const char *cmd)
{
    if (!term || !cmd) return;

    if (strcmp(cmd, "run") == 0 || strncmp(cmd, "run ", 4) == 0) {
        static UserResult result;
        int rc = user_run(cmd[3] ? cmd + 4 : "", &result);
        if (rc) terminal_append_line(term, user_error_name(rc));
        else {
            char line[TERMINAL_LINE_MAX]; size_t n = 0;
            for (size_t i = 0; i < result.output_length; i++) {
                char c = result.output[i];
                if (c == '\n' || n == sizeof(line) - 1) {
                    line[n] = 0; terminal_append_line(term, line); n = 0;
                }
                if (c != '\n') line[n++] = c;
            }
            if (n) { line[n] = 0; terminal_append_line(term, line); }
            strcpy(line, "App ");
            char number[24]; utoa(result.pid, number, 10);
            str_append(line, sizeof(line), number);
            str_append(line, sizeof(line), ": ");
            str_append(line, sizeof(line), user_status_name(result.status));
            str_append(line, sizeof(line), " code=");
            itoa(result.exit_code, number, 10);
            str_append(line, sizeof(line), number);
            terminal_append_line(term, line);
        }
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        terminal_append_line(term, "Commands: ls, cd, pwd, cat, echo, touch,");
        terminal_append_line(term, "  mkdir, rm, mv, cp, open, clear, sync,");
        terminal_append_line(term, "  ps, top, kill, heap, whoami, users,");
        terminal_append_line(term, "  adduser, passwd, su, lock, run <name>");
        return;
    }

    if (strcmp(cmd, "ps") == 0 || strcmp(cmd, "top") == 0) {
        terminal_append_line(term, "PID  USER       STATE     TICKS   MEM(KB)  NAME");
        int n = proc_count();
        for (int i = 0; i < n; i++) {
            const ProcEntry *pe = proc_get_by_index(i);
            if (!pe) continue;
            char pline[80];
            char num[20];
            pline[0] = '\0';
            utoa(pe->pid, num, 10);
            str_append(pline, sizeof(pline), num);
            while (strlen(pline) < 5) str_append(pline, sizeof(pline), " ");
            str_append(pline, sizeof(pline), pe->owner);
            while (strlen(pline) < 16) str_append(pline, sizeof(pline), " ");
            str_append(pline, sizeof(pline), proc_state_name(pe->state));
            while (strlen(pline) < 26) str_append(pline, sizeof(pline), " ");
            utoa(pe->cpu_ticks, num, 10);
            str_append(pline, sizeof(pline), num);
            while (strlen(pline) < 34) str_append(pline, sizeof(pline), " ");
            utoa(pe->memory_bytes / 1024, num, 10);
            str_append(pline, sizeof(pline), num);
            while (strlen(pline) < 43) str_append(pline, sizeof(pline), " ");
            str_append(pline, sizeof(pline), pe->name);
            terminal_append_line(term, pline);
        }
        return;
    }

    if (strncmp(cmd, "kill ", 5) == 0) {
        const char *arg = cmd + 5;
        while (*arg == ' ') arg++;
        int consumed = 0;
        int pid = parse_int(arg, &consumed);
        if (consumed <= 0 || pid <= 0) {
            terminal_append_line(term, "Usage: kill <pid>");
            return;
        }
        int killed_app = -1;
        int rc = proc_kill((uint32_t)pid, &killed_app);
        if (rc == 0) {
            if (killed_app > APP_DEMO && killed_app < APP_COUNT) {
                const char *cname = app_name_from_type((AppType)killed_app);
                int ridx = app_registry_find_by_name(cname);
                close_app_window((AppType)killed_app);
                AppInfo *app = app_registry_get(ridx);
                if (app) app->running = false;
            }
            terminal_append_line(term, "Process terminated.");
        } else if (rc == -2) {
            terminal_append_line(term, "kill: operation not permitted (system daemon)");
        } else {
            terminal_append_line(term, "kill: no such process");
        }
        return;
    }

    if (strcmp(cmd, "heap") == 0 || strcmp(cmd, "mem") == 0) {
        char mline[80];
        char num[24];
        uint64_t total_mb = pmm_get_total_memory() / (1024 * 1024);
        uint64_t free_mb = pmm_get_free_memory() / (1024 * 1024);
        mline[0] = '\0';
        str_append(mline, sizeof(mline), "PMM RAM:   ");
        utoa((total_mb >= free_mb) ? (total_mb - free_mb) : 0, num, 10);
        str_append(mline, sizeof(mline), num);
        str_append(mline, sizeof(mline), " MB used / ");
        utoa(total_mb, num, 10);
        str_append(mline, sizeof(mline), num);
        str_append(mline, sizeof(mline), " MB total");
        terminal_append_line(term, mline);

        mline[0] = '\0';
        str_append(mline, sizeof(mline), "KHeap:     ");
        utoa(kheap_get_used() / 1024, num, 10);
        str_append(mline, sizeof(mline), num);
        str_append(mline, sizeof(mline), " KB used / ");
        utoa(kheap_get_total() / 1024, num, 10);
        str_append(mline, sizeof(mline), num);
        str_append(mline, sizeof(mline), " KB (");
        utoa(kheap_get_alloc_count(), num, 10);
        str_append(mline, sizeof(mline), num);
        str_append(mline, sizeof(mline), " blocks, ");
        str_append(mline, sizeof(mline), kheap_validate() ? "valid)" : "CORRUPT)");
        terminal_append_line(term, mline);
        return;
    }

    if (strcmp(cmd, "whoami") == 0) {
        char uline[80];
        uline[0] = '\0';
        str_append(uline, sizeof(uline), users_current_username());
        str_append(uline, sizeof(uline), " (");
        str_append(uline, sizeof(uline), users_current_full_name());
        str_append(uline, sizeof(uline), ")");
        terminal_append_line(term, uline);
        return;
    }

    if (strcmp(cmd, "users") == 0) {
        int n = users_count();
        if (n == 0) {
            terminal_append_line(term, "No registered accounts (guest session)");
            return;
        }
        for (int i = 0; i < n; i++) {
            const UserAccount *acc = users_get_by_index(i);
            if (!acc) continue;
            char uline[80];
            uline[0] = '\0';
            str_append(uline, sizeof(uline), (i == users_current_index()) ? "* " : "  ");
            str_append(uline, sizeof(uline), acc->username);
            str_append(uline, sizeof(uline), " - ");
            str_append(uline, sizeof(uline), acc->full_name);
            if (acc->is_admin) str_append(uline, sizeof(uline), " [admin]");
            terminal_append_line(term, uline);
        }
        return;
    }

    if (strncmp(cmd, "adduser ", 8) == 0) {
        const char *args = cmd + 8;
        while (*args == ' ') args++;
        const char *sp = str_find(args, " ");
        if (!sp) {
            terminal_append_line(term, "Usage: adduser <username> <password> [Full Name]");
            return;
        }
        char uname[USER_NAME_MAX];
        size_t ulen = (size_t)(sp - args);
        if (ulen >= sizeof(uname)) ulen = sizeof(uname) - 1;
        memcpy(uname, args, ulen);
        uname[ulen] = '\0';

        const char *parg = sp + 1;
        while (*parg == ' ') parg++;
        const char *sp2 = str_find(parg, " ");
        char upass[USER_PASS_MAX];
        char fname[USER_FULLNAME_MAX];
        if (sp2) {
            size_t plen = (size_t)(sp2 - parg);
            if (plen >= sizeof(upass)) plen = sizeof(upass) - 1;
            memcpy(upass, parg, plen);
            upass[plen] = '\0';
            const char *farg = sp2 + 1;
            while (*farg == ' ') farg++;
            strncpy(fname, farg, sizeof(fname) - 1);
            fname[sizeof(fname) - 1] = '\0';
        } else {
            strncpy(upass, parg, sizeof(upass) - 1);
            upass[sizeof(upass) - 1] = '\0';
            strncpy(fname, uname, sizeof(fname) - 1);
            fname[sizeof(fname) - 1] = '\0';
        }
        if (users_create(uname, fname, upass, true) == 0) {
            terminal_append_line(term, "Account created & SHA-256 credentials saved.");
        } else {
            terminal_append_line(term, "adduser: failed (invalid or duplicate username)");
        }
        return;
    }

    if (strncmp(cmd, "passwd ", 7) == 0) {
        const char *args = cmd + 7;
        while (*args == ' ') args++;
        const char *sp = str_find(args, " ");
        if (!sp) {
            terminal_append_line(term, "Usage: passwd <old_password> <new_password>");
            return;
        }
        char oldp[USER_PASS_MAX];
        size_t olen = (size_t)(sp - args);
        if (olen >= sizeof(oldp)) olen = sizeof(oldp) - 1;
        memcpy(oldp, args, olen);
        oldp[olen] = '\0';
        const char *newp = sp + 1;
        while (*newp == ' ') newp++;
        if (users_change_password(users_current_username(), oldp, newp) == 0) {
            terminal_append_line(term, "Password updated and saved.");
        } else {
            terminal_append_line(term, "passwd: incorrect old password or invalid account");
        }
        return;
    }

    if (strncmp(cmd, "su ", 3) == 0) {
        const char *args = cmd + 3;
        while (*args == ' ') args++;
        const char *sp = str_find(args, " ");
        char uname[USER_NAME_MAX];
        const char *upass = "";
        if (sp) {
            size_t ulen = (size_t)(sp - args);
            if (ulen >= sizeof(uname)) ulen = sizeof(uname) - 1;
            memcpy(uname, args, ulen);
            uname[ulen] = '\0';
            upass = sp + 1;
            while (*upass == ' ') upass++;
        } else {
            strncpy(uname, args, sizeof(uname) - 1);
            uname[sizeof(uname) - 1] = '\0';
        }
        if (users_login(uname, upass) == 0) {
            compositor_on_user_switched();
            terminal_append_line(term, "Switched user session.");
        } else {
            terminal_append_line(term, "su: authentication failed");
        }
        return;
    }

    if (strcmp(cmd, "lock") == 0 || strcmp(cmd, "logout") == 0) {
        users_lock_session();
        login_password[0] = '\0';
        login_status[0] = '\0';
        return;
    }

    if (strcmp(cmd, "clear") == 0) {
        term->line_count = 0;
        return;
    }

    if (strcmp(cmd, "sync") == 0) {
        system_request_sync();
        terminal_append_line(term, "Saving state to disk...");
        return;
    }

    if (strcmp(cmd, "shutdown") == 0) {
        terminal_append_line(term, "Shutting down...");
        system_request_shutdown();
        return;
    }

    if (strcmp(cmd, "reboot") == 0) {
        terminal_append_line(term, "Restarting...");
        system_request_restart();
        return;
    }

    if (strcmp(cmd, "pwd") == 0) {
        terminal_append_line(term, term->cwd[0] ? term->cwd : "/");
        return;
    }

    if (strcmp(cmd, "cd") == 0) {
        strncpy(term->cwd, users_current_home(), sizeof(term->cwd) - 1);
        term->cwd[sizeof(term->cwd) - 1] = '\0';
        return;
    }

    if (strncmp(cmd, "cd ", 3) == 0) {
        char path[256];
        if (terminal_resolve_path(term, cmd + 3, path, sizeof(path)) != 0) {
            terminal_append_line(term, "cd: path too long");
            return;
        }
        if (vfs_isdir(path)) {
            strncpy(term->cwd, path, sizeof(term->cwd) - 1);
            term->cwd[sizeof(term->cwd) - 1] = '\0';
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
            if (terminal_resolve_path(term, arg, path, sizeof(path)) != 0) {
                terminal_append_line(term, "ls: path too long");
                return;
            }
        } else {
            strncpy(path, term->cwd[0] ? term->cwd : "/", sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        }
        terminal_ls(term, path);
        return;
    }

    if (strncmp(cmd, "cat ", 4) == 0) {
        char path[256];
        if (terminal_resolve_path(term, cmd + 4, path, sizeof(path)) != 0) {
            terminal_append_line(term, "cat: path too long");
            return;
        }
        terminal_cat(term, path);
        return;
    }

    if (strncmp(cmd, "touch ", 6) == 0) {
        char path[256];
        if (terminal_resolve_path(term, cmd + 6, path, sizeof(path)) != 0) {
            terminal_append_line(term, "touch: path too long");
            return;
        }
        VfsFile *file = vfs_open(path, VFS_O_CREATE | VFS_O_WRITE);
        if (file) vfs_close(file);
        return;
    }

    if (strncmp(cmd, "rm ", 3) == 0) {
        char path[256];
        if (terminal_resolve_path(term, cmd + 3, path, sizeof(path)) != 0) {
            terminal_append_line(term, "rm: path too long");
            return;
        }
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
        char src_resolved[256];
        char dst[256];
        size_t len = (size_t)(space - args);
        if (len >= sizeof(src)) len = sizeof(src) - 1;
        memcpy(src, args, len);
        src[len] = '\0';
        const char *dst_arg = space + 1;
        /* Resolve into SEPARATE buffers: resolving src in place
         * clobbered the source string and corrupted every relative mv */
        if (terminal_resolve_path(term, src, src_resolved, sizeof(src_resolved)) != 0 ||
            terminal_resolve_path(term, dst_arg, dst, sizeof(dst)) != 0) {
            terminal_append_line(term, "mv: path too long");
            return;
        }
        if (vfs_rename(src_resolved, dst) != 0) {
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
        char src_resolved[256];
        char dst[256];
        size_t len = (size_t)(space - args);
        if (len >= sizeof(src)) len = sizeof(src) - 1;
        memcpy(src, args, len);
        src[len] = '\0';
        const char *dst_arg = space + 1;
        if (terminal_resolve_path(term, src, src_resolved, sizeof(src_resolved)) != 0 ||
            terminal_resolve_path(term, dst_arg, dst, sizeof(dst)) != 0) {
            terminal_append_line(term, "cp: path too long");
            return;
        }

        VfsFile *in = vfs_open(src_resolved, VFS_O_READ);
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
        if (terminal_resolve_path(term, cmd + 6, path, sizeof(path)) != 0) {
            terminal_append_line(term, "mkdir: path too long");
            return;
        }
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
            if (terminal_resolve_path(term, path_arg, path, sizeof(path)) != 0) {
                terminal_append_line(term, "echo: path too long");
                return;
            }
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
        if (terminal_resolve_path(term, cmd + 5, path, sizeof(path)) != 0) {
            terminal_append_line(term, "open: path too long");
            return;
        }
        if (!vfs_exists(path)) {
            char alt[256];
            if (vfs_join_path(alt, sizeof(alt), "/Applications", cmd + 5) == 0) {
                if (vfs_exists(alt)) {
                    strncpy(path, alt, sizeof(path) - 1);
                    path[sizeof(path) - 1] = '\0';
                }
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
        edit->lines[i - 1][TEXTEDIT_LINE_MAX - 1] = '\0';
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
        /* Guarantee termination when the shifted line is full-width */
        edit->lines[i][TEXTEDIT_LINE_MAX - 1] = '\0';
    }
    strncpy(edit->lines[line], text, TEXTEDIT_LINE_MAX - 1);
    edit->lines[line][TEXTEDIT_LINE_MAX - 1] = '\0';
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
        users_make_home_path(edit->file_path, sizeof(edit->file_path), "Documents/Untitled.txt");
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
            bool have_tail = edit->cursor_col < (int)strlen(edit->lines[edit->cursor_line]);
            if (have_tail) {
                strncpy(tail, &edit->lines[edit->cursor_line][edit->cursor_col], sizeof(tail) - 1);
                tail[sizeof(tail) - 1] = '\0';
                edit->lines[edit->cursor_line][edit->cursor_col] = '\0';
                /* Split: insert the tail as a new line after the cursor
                 * line (insert_line bumps line_count exactly once) */
                textedit_insert_line(edit, edit->cursor_line + 1, tail);
                edit->cursor_line++;
                edit->cursor_col = 0;
            } else {
                /* New empty line at the cursor position */
                edit->cursor_line++;
                edit->cursor_col = 0;
                if (edit->cursor_line >= edit->line_count) {
                    edit->line_count = edit->cursor_line + 1;
                }
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
        int len = (int)strlen(edit->lines[edit->cursor_line]);
        /* Insert (shift right) only while the line has room; when the
         * line is full the old code shifted nothing and silently
         * OVERWROTE the character at the cursor */
        if (edit->cursor_col < TEXTEDIT_LINE_MAX - 1 && len < TEXTEDIT_LINE_MAX - 1) {
            for (int i = len; i >= edit->cursor_col; i--) {
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
    if (strcmp(bundle_id, "com.ojjyos.web") == 0) return APP_WEB;
    if (strcmp(bundle_id, "com.ojjyos.calculator") == 0) return APP_CALCULATOR;
    if (strcmp(bundle_id, "com.ojjyos.monitor") == 0) return APP_MONITOR;
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
        case APP_WEB: return "Web";
        case APP_CALCULATOR: return "Calculator";
        case APP_MONITOR: return "Activity Monitor";
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
        finder_set_path(&state->finder, users_current_home());
        state->finder.view_mode = FINDER_VIEW_LIST;
    } else if (type == APP_SETTINGS) {
        state->settings.page = SETTINGS_PAGE_APPEARANCE;
        state->settings.user_field = 0;
        state->settings.old_pw[0] = '\0';
        state->settings.new_pw[0] = '\0';
        state->settings.msg[0] = '\0';
    } else if (type == APP_TERMINAL) {
        terminal_append_line(&state->terminal, "+--------------------------------------+");
        terminal_append_line(&state->terminal, "|     Welcome to the ojjyOS Shell      |");
        terminal_append_line(&state->terminal, "|        Created by Jonas Lee          |");
        terminal_append_line(&state->terminal, "+--------------------------------------+");
        terminal_append_line(&state->terminal, "  ~  ~  ~  Tahoe  -  Liquid  ~  ~  ~");
        terminal_append_line(&state->terminal, "Type 'help' for commands.");
        strncpy(state->terminal.cwd, users_current_home(), sizeof(state->terminal.cwd) - 1);
        state->terminal.cwd[sizeof(state->terminal.cwd) - 1] = '\0';
        state->terminal.history_count = 0;
        state->terminal.history_pos = 0;
    } else if (type == APP_TEXTEDIT) {
        textedit_clear(&state->textedit);
        strcpy(state->textedit.status, "Ready");
    } else if (type == APP_NOTES) {
        textedit_clear(&state->notes);
        users_make_home_path(state->notes.file_path, sizeof(state->notes.file_path), "Documents/Notes.txt");
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
    } else if (type == APP_WEB) {
        state->web.history_count = 0;
        state->web.history_pos = 0;
        state->web.address[0] = '\0';
        strcpy(state->web.address, "home");
        web_load_page(&state->web, "home");
    } else if (type == APP_CALCULATOR) {
        calc_reset(&state->calc);
    }

    if (type > APP_DEMO && type < APP_COUNT) {
        proc_ensure_app((int)type, app_name_from_type(type), 256ULL * 1024ULL);
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
        /* Restore if minimized, then bring to front */
        windows[idx].minimized = false;
        raise_window_to_top(idx);
        return windows[window_count - 1].id;
    }

    int w = 560;
    int h = 380;
    int x = 140 + (window_count * 18) % 90;
    int y = 90 + (window_count * 18) % 70;
    if (type == APP_FINDER) { w = 640; h = 420; }
    if (type == APP_SETTINGS) { w = 560; h = 400; }
    if (type == APP_TERMINAL) { w = 600; h = 360; }
    if (type == APP_TEXTEDIT) { w = 560; h = 360; }
    if (type == APP_NOTES) { w = 460; h = 320; }
    if (type == APP_PREVIEW) { w = 480; h = 320; }
    if (type == APP_CALENDAR) { w = 720; h = 440; }
    if (type == APP_ABOUT) { w = 460; h = 300; }
    if (type == APP_WEB) { w = 640; h = 420; }
    if (type == APP_CALCULATOR) { w = 300; h = 360; }
    if (type == APP_MONITOR) { w = 520; h = 340; }

    int id = create_window_internal(title, x, y, w, h, type);
    if (id >= 0) {
        rebuild_app_window_index();
        active_window_index = window_count - 1;
    }
    return id;
}

void compositor_on_user_switched(void)
{
    search_index_init();
    desktop_last_reload = 0;
    desktop_reload();

    for (int i = 0; i < window_count; i++) {
        AppWindowState *st = &app_states[i];
        if (st->type == APP_FINDER) {
            finder_set_path(&st->finder, users_current_home());
        } else if (st->type == APP_TERMINAL) {
            strncpy(st->terminal.cwd, users_current_home(), sizeof(st->terminal.cwd) - 1);
            st->terminal.cwd[sizeof(st->terminal.cwd) - 1] = '\0';
        } else if (st->type == APP_NOTES) {
            users_make_home_path(st->notes.file_path, sizeof(st->notes.file_path), "Documents/Notes.txt");
            textedit_load_file(&st->notes, st->notes.file_path);
        } else if (st->type == APP_CALENDAR) {
            calendar_load(&st->calendar);
        }
    }
}

void compositor_init(uint32_t width, uint32_t height)
{
    comp_width = width;
    comp_height = height;

    /* Render into the off-screen back buffer; compositor_tick presents
     * finished frames with fb_present(). Without this the whole desktop
     * repaints directly into the live scanout, which reads as constant
     * full-screen flashing under emulation. */
    fb_use_backbuffer(true);

    need_redraw = true;
    last_drawn_minute = -1;

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
    resizing = false;
    resize_index = -1;
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

    /* Desktop icons */
    desktop_last_reload = 0;
    desktop_selected = -1;
    desktop_last_click_ms = 0;
    desktop_last_click_idx = -1;
}

void compositor_set_dark_mode(bool enabled)
{
     mark_dirty();

   dark_mode = enabled;
    theme = dark_mode ? theme_dark() : theme_light();
    settings_get()->dark_mode = enabled;
    if (!wallpaper_loaded) {
        wallpaper_rebuild_cache();   /* Procedural wallpaper follows the theme */
    }
}

static void wallpaper_load_file(const char *path);

void compositor_set_wallpaper(const char *path)
{
     mark_dirty();

   wallpaper_load_file(path);
    wallpaper_rebuild_cache();
}

static void wallpaper_load_file(const char *path)
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
     mark_dirty();

   return create_window_internal(title, x, y, w, h, APP_DEMO);
}

void compositor_move_window(int id, int x, int y)
{
     mark_dirty();

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
     mark_dirty();

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
     mark_dirty();

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
     mark_dirty();

   return destroy_window_internal(id);
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

/*
 * Handle a Cmd (Super) shortcut inside an app.
 * Returns true if the key was consumed.
 */
static bool handle_app_shortcut(AppWindowState *state, KeyCode keycode)
{
    if (state->type != APP_TEXTEDIT && state->type != APP_NOTES) {
        return false;
    }
    TextEditState *edit = (state->type == APP_NOTES) ? &state->notes : &state->textedit;

    switch (keycode) {
        case KEY_S:
            textedit_save(edit);
            return true;
        case KEY_O:
            if (last_opened_path[0]) {
                textedit_load_file(edit, last_opened_path);
            }
            return true;
        case KEY_C:
            if (edit->sel_active) {
                textedit_copy_selection(edit);
                strcpy(edit->status, "Copied");
            } else {
                strncpy(text_clipboard, edit->lines[edit->cursor_line],
                        sizeof(text_clipboard) - 1);
                text_clipboard[sizeof(text_clipboard) - 1] = '\0';
                strcpy(edit->status, "Copied line");
            }
            return true;
        case KEY_X:
            if (edit->sel_active) {
                textedit_copy_selection(edit);
                textedit_delete_selection(edit);
                strcpy(edit->status, "Cut");
            } else {
                strncpy(text_clipboard, edit->lines[edit->cursor_line],
                        sizeof(text_clipboard) - 1);
                text_clipboard[sizeof(text_clipboard) - 1] = '\0';
                textedit_delete_line(edit, edit->cursor_line);
                strcpy(edit->status, "Cut line");
            }
            return true;
        case KEY_V:
            if (text_clipboard[0]) {
                textedit_insert_line(edit, edit->cursor_line + 1, text_clipboard);
                strcpy(edit->status, "Pasted line");
            }
            return true;
        default:
            return false;
    }
}

void compositor_handle_key(KeyCode keycode, char ascii, uint8_t modifiers)
{
     mark_dirty();

   if (login_screen_visible()) {
        active_menu = 0;
        ctx_menu_open = false;

        bool create_mode = login_create_mode || (users_count() == 0);
        if (create_mode) {
            if (keycode == KEY_TAB) {
                login_active_field = (login_active_field + 1) % 4;
                return;
            }
            if (keycode == KEY_ESCAPE && users_count() > 0) {
                login_create_mode = false;
                login_status[0] = '\0';
                return;
            }
            if (keycode == KEY_BACKSPACE) {
                char *buf = (login_active_field == 0) ? login_full_name :
                            (login_active_field == 1) ? login_username :
                            (login_active_field == 2) ? login_password : login_confirm;
                size_t len = strlen(buf);
                if (len > 0) buf[len - 1] = '\0';
                return;
            }
            if (keycode == KEY_ENTER) {
                if (login_username[0] == '\0') {
                    strcpy(login_status, "Please enter a username.");
                    login_status_ok = false;
                    return;
                }
                if (strcmp(login_password, login_confirm) != 0) {
                    strcpy(login_status, "Passwords do not match.");
                    login_status_ok = false;
                    return;
                }
                const char *fn = login_full_name[0] ? login_full_name : login_username;
                if (users_create(login_username, fn, login_password, true) == 0) {
                    users_login(login_username, login_password);
                    login_password[0] = '\0';
                    login_confirm[0] = '\0';
                    login_status[0] = '\0';
                    login_create_mode = false;
                    login_selected_user = users_current_index();
                    compositor_on_user_switched();
                } else {
                    strcpy(login_status, "Invalid or duplicate username (use a-z, 0-9).");
                    login_status_ok = false;
                }
                return;
            }
            if (ascii >= 32 && ascii <= 126) {
                char *buf = (login_active_field == 0) ? login_full_name :
                            (login_active_field == 1) ? login_username :
                            (login_active_field == 2) ? login_password : login_confirm;
                size_t cap = (login_active_field == 0) ? sizeof(login_full_name) :
                             (login_active_field == 1) ? sizeof(login_username) :
                             (login_active_field == 2) ? sizeof(login_password) : sizeof(login_confirm);
                if (login_active_field == 1 && ascii == ' ') {
                    return;
                }
                size_t len = strlen(buf);
                if (len + 1 < cap) {
                    buf[len] = ascii;
                    buf[len + 1] = '\0';
                }
            }
            return;
        } else {
            if (keycode == KEY_TAB) {
                int n = users_count();
                if (n > 0) {
                    login_selected_user = (login_selected_user + 1) % n;
                    login_password[0] = '\0';
                    login_status[0] = '\0';
                }
                return;
            }
            if (keycode == KEY_BACKSPACE) {
                size_t len = strlen(login_password);
                if (len > 0) login_password[len - 1] = '\0';
                return;
            }
            if (keycode == KEY_ENTER) {
                if (login_selected_user < 0 || login_selected_user >= users_count()) {
                    login_selected_user = 0;
                }
                const UserAccount *acc = users_get_by_index(login_selected_user);
                if (acc && users_login(acc->username, login_password) == 0) {
                    login_password[0] = '\0';
                    login_status[0] = '\0';
                    compositor_on_user_switched();
                } else {
                    login_password[0] = '\0';
                    strcpy(login_status, "Incorrect password. Try again.");
                    login_status_ok = false;
                }
                return;
            }
            if (ascii >= 32 && ascii <= 126) {
                size_t len = strlen(login_password);
                if (len + 1 < sizeof(login_password)) {
                    login_password[len] = ascii;
                    login_password[len + 1] = '\0';
                }
            }
            return;
        }
    }

    if (keycode == KEY_ESCAPE && (active_menu > 0 || ctx_menu_open)) {
        active_menu = 0;
        ctx_menu_open = false;
        return;
    }

    if (keycode == KEY_ESCAPE && overlay != OVERLAY_NONE) {
        overlay_set(OVERLAY_NONE);
        return;
    }

    /* Ctrl+Cmd+Q locks the screen immediately (macOS shortcut) */
    if ((modifiers & INPUT_MOD_SUPER) && (modifiers & INPUT_MOD_CTRL) && keycode == KEY_Q) {
        users_lock_session();
        login_password[0] = '\0';
        login_status[0] = '\0';
        overlay_set(OVERLAY_NONE);
        active_menu = 0;
        ctx_menu_open = false;
        return;
    }

    /* App-level Cmd shortcuts take priority over the global ones.
     * (The old global Cmd+C shadowed the editor's copy.) */
    if ((modifiers & INPUT_MOD_SUPER) && overlay == OVERLAY_NONE &&
        active_window_index >= 0 && active_window_index < window_count) {
        if (handle_app_shortcut(&app_states[active_window_index], keycode)) {
            return;
        }
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
        /* Quit the active app: mark it not running AND close its
         * window (the old code left a dead window on screen).
         * Finder, like on macOS, is never quit. */
        if (active_window_index >= 0 && active_window_index < window_count) {
            AppWindowState *state = &app_states[active_window_index];
            if (state->type != APP_FINDER) {
                const char *closing_name = app_name_from_type(state->type);
                int idx = app_registry_find_by_name(closing_name);
                close_app_window(state->type);
                AppInfo *app = app_registry_get(idx);
                if (app) app->running = false;
                strncpy(active_app_name, "Finder", sizeof(active_app_name) - 1);
            }
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
            /* Cmd shortcuts were already handled by handle_app_shortcut */
            textedit_handle_key(&state->textedit, ascii, keycode, modifiers);
        } else if (state->type == APP_NOTES) {
            textedit_handle_key(&state->notes, ascii, keycode, modifiers);
            if (state->notes.dirty) {
                textedit_save(&state->notes);
            }
        } else if (state->type == APP_FINDER) {
            FinderState *finder = &state->finder;
            /* A stale selection (e.g. after delete/refresh) must not act
             * on a slot that now holds a different item */
            if (finder->selected >= finder->entry_count) {
                finder->selected = finder->entry_count - 1;
            }
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
                        if (vfs_join_path(src, sizeof(src), finder->path, entry->name) == 0 &&
                            vfs_join_path(dst, sizeof(dst), finder->path, finder->rename_buffer) == 0 &&
                            vfs_rename(src, dst) == 0) {
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
            /* Rename: only when nothing has been typed into search
             * (otherwise 'r' could never be part of a query) */
            if (keycode == KEY_R && finder->selected >= 0 && finder->search[0] == '\0') {
                FinderEntry *entry = &finder->entries[finder->selected];
                strncpy(finder->rename_buffer, entry->name, sizeof(finder->rename_buffer) - 1);
                finder->rename_buffer[sizeof(finder->rename_buffer) - 1] = '\0';
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
                    /* Apply what was typed before switching fields,
                     * otherwise edits are silently discarded */
                    calendar_apply_edit(cal);
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
        } else if (state->type == APP_WEB) {
            WebState *web = &state->web;
            if (keycode == KEY_BACKSPACE) {
                size_t len = strlen(web->address);
                if (len > 0) {
                    web->address[len - 1] = '\0';
                }
                return;
            }
            if (keycode == KEY_ENTER) {
                if (web->address[0]) {
                    web_load_page(web, web->address);
                }
                return;
            }
            if (ascii >= 32 && ascii <= 126) {
                size_t len = strlen(web->address);
                if (len < sizeof(web->address) - 1) {
                    web->address[len] = ascii;
                    web->address[len + 1] = '\0';
                }
            }
        } else if (state->type == APP_SETTINGS) {
            if (state->settings.page == SETTINGS_PAGE_USERS && state->settings.user_field > 0) {
                if (keycode == KEY_TAB) {
                    state->settings.user_field = (state->settings.user_field == 1) ? 2 : 1;
                    return;
                }
                if (keycode == KEY_ESCAPE) {
                    state->settings.user_field = 0;
                    return;
                }
                char *buf = (state->settings.user_field == 1)
                    ? state->settings.old_pw : state->settings.new_pw;
                size_t cap = (state->settings.user_field == 1)
                    ? sizeof(state->settings.old_pw) : sizeof(state->settings.new_pw);
                if (keycode == KEY_BACKSPACE) {
                    size_t len = strlen(buf);
                    if (len > 0) buf[len - 1] = '\0';
                    return;
                }
                if (keycode == KEY_ENTER) {
                    if (users_change_password(users_current_username(),
                                              state->settings.old_pw,
                                              state->settings.new_pw) == 0) {
                        strcpy(state->settings.msg, "Password updated & saved.");
                        state->settings.msg_ok = true;
                        state->settings.old_pw[0] = '\0';
                        state->settings.new_pw[0] = '\0';
                        state->settings.user_field = 0;
                    } else {
                        strcpy(state->settings.msg, "Incorrect current password.");
                        state->settings.msg_ok = false;
                    }
                    return;
                }
                if (ascii >= 32 && ascii <= 126) {
                    size_t len = strlen(buf);
                    if (len + 1 < cap) {
                        buf[len] = ascii;
                        buf[len + 1] = '\0';
                    }
                }
            }
        } else if (state->type == APP_CALCULATOR) {
            CalculatorState *calc = &state->calc;
            if (ascii >= '0' && ascii <= '9') {
                calc_press_digit(calc, ascii);
            } else if (ascii == '+' || ascii == '-' || ascii == '*' || ascii == '/') {
                calc_press_op(calc, ascii);
            } else if (ascii == '=' || keycode == KEY_ENTER) {
                calc_press_eq(calc);
            } else if (ascii == 'c' || ascii == 'C' || keycode == KEY_ESCAPE) {
                calc_reset(calc);
            } else if (keycode == KEY_BACKSPACE) {
                size_t len = strlen(calc->display);
                if (len > 1 && !calc->clear_next && !calc->has_error) {
                    calc->display[len - 1] = '\0';
                } else {
                    strcpy(calc->display, "0");
                    calc->clear_next = true;
                }
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
     mark_dirty();

   cursor_x = x;
    cursor_y = y;

    /* Drag hover: highlight drop targets in any Finder window with an
     * active drag (not just the active window) */
    for (int wi = 0; wi < window_count; wi++) {
        AppWindowState *state = &app_states[wi];
        CompositorWindow *win = &windows[wi];
        if (state->type != APP_FINDER || !state->finder.drag_active) {
            continue;
        }
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
                int row = (y - (content_y + 44)) / 18 + finder->scroll;
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
                int idx = row * cols + col + finder->scroll;
                if (col >= 0 && row >= 0 &&
                    idx >= finder->scroll && idx < finder->entry_count) {
                    FinderEntry *entry = &finder->entries[idx];
                    if (entry->type == VFS_TYPE_DIR) {
                        finder->drag_hover_index = idx;
                    }
                }
            }
        }
    }

    if (up) {
        /* Clear drag state in ANY Finder window (the active window may
         * have changed between press and release, e.g. a file click
         * raised TextEdit - the old code leaked drag_active forever) */
        for (int wi = 0; wi < window_count; wi++) {
            AppWindowState *state = &app_states[wi];
            CompositorWindow *win = &windows[wi];
            if (state->type != APP_FINDER || !state->finder.drag_active) {
                continue;
            }
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
                    int row = (y - (content_y + 44)) / 18 + finder->scroll;
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
                    int idx = row * cols + col + finder->scroll;
                    if (col >= 0 && row >= 0 &&
                        idx >= finder->scroll && idx < finder->entry_count) {
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
        if (login_screen_visible()) {
            bool create_mode = login_create_mode || (users_count() == 0);
            if (create_mode) {
                int cw = 440;
                int ch = 340;
                int cx = ((int)comp_width - cw) / 2;
                int cy = ((int)comp_height - ch) / 2;
                for (int f = 0; f < 4; f++) {
                    int fy = cy + 74 + f * 40;
                    int bx = cx + 132;
                    int bw = cw - 160;
                    if (x >= bx && x <= bx + bw && y >= fy && y <= fy + 26) {
                        login_active_field = f;
                        return;
                    }
                }
                int btn_y = cy + ch - 46;
                int btn_w = (users_count() > 0) ? 230 : (cw - 56);
                if (x >= cx + 28 && x <= cx + 28 + btn_w && y >= btn_y && y <= btn_y + 30) {
                    compositor_handle_key(KEY_ENTER, 0, 0);
                    return;
                }
                if (users_count() > 0) {
                    int cancel_x = cx + 28 + btn_w + 12;
                    int cancel_w = cw - 56 - btn_w - 12;
                    if (x >= cancel_x && x <= cancel_x + cancel_w && y >= btn_y && y <= btn_y + 30) {
                        login_create_mode = false;
                        login_status[0] = '\0';
                        return;
                    }
                }
                return;
            } else {
                int cw = 360;
                int ch = 280;
                int cx = ((int)comp_width - cw) / 2;
                int cy = ((int)comp_height - ch) / 2;
                int pw_x = cx + 36;
                int pw_w = cw - 72;
                int sign_y = cy + 190;
                if (x >= pw_x && x <= pw_x + pw_w && y >= sign_y && y <= sign_y + 28) {
                    compositor_handle_key(KEY_ENTER, 0, 0);
                    return;
                }
                int bot_y = cy + ch - 42;
                int half_w = (pw_w - 10) / 2;
                if (x >= pw_x && x <= pw_x + half_w && y >= bot_y && y <= bot_y + 24) {
                    int n = users_count();
                    if (n > 0) {
                        login_selected_user = (login_selected_user + 1) % n;
                        login_password[0] = '\0';
                        login_status[0] = '\0';
                    }
                    return;
                }
                if (x >= pw_x + half_w + 10 && x <= pw_x + half_w + 10 + half_w &&
                    y >= bot_y && y <= bot_y + 24) {
                    login_create_mode = true;
                    login_active_field = 0;
                    login_full_name[0] = '\0';
                    login_username[0] = '\0';
                    login_password[0] = '\0';
                    login_confirm[0] = '\0';
                    login_status[0] = '\0';
                    return;
                }
                return;
            }
        }

        if (ctx_menu_open) {
            int count = 7;
            int w = 216;
            int h = count * 22 + 12;
            int mx = ctx_menu_x;
            int my = ctx_menu_y;
            if (mx + w > (int)comp_width - 8) mx = (int)comp_width - w - 8;
            if (my + h > (int)comp_height - 8) my = (int)comp_height - h - 8;
            ctx_menu_open = false;
            if (x >= mx && x <= mx + w && y >= my + 6 && y < my + 6 + count * 22) {
                int item = (y - (my + 6)) / 22;
                if (item == 0) {
                    char fpath[160];
                    users_make_home_path(fpath, sizeof(fpath), "Desktop/New Folder");
                    if (vfs_exists(fpath)) {
                        users_make_home_path(fpath, sizeof(fpath), "Desktop/New Folder 2");
                    }
                    vfs_mkdir(fpath);
                    desktop_reload();
                } else if (item == 1) {
                    char npath[160];
                    users_make_home_path(npath, sizeof(npath), "Desktop/Note.txt");
                    if (!vfs_exists(npath)) {
                        VfsFile *f = vfs_open(npath, VFS_O_CREATE | VFS_O_WRITE);
                        if (f) {
                            const char *txt = "New desktop note\n";
                            vfs_write(f, txt, strlen(txt));
                            vfs_close(f);
                        }
                    }
                    desktop_reload();
                    app_open_window(APP_TEXTEDIT, "TextEdit");
                    if (app_window_index[APP_TEXTEDIT] >= 0) {
                        textedit_load_file(&app_states[app_window_index[APP_TEXTEDIT]].textedit, npath);
                    }
                } else if (item == 2) {
                    app_open_window(APP_FINDER, "Finder");
                    if (app_window_index[APP_FINDER] >= 0) {
                        finder_set_path(&app_states[app_window_index[APP_FINDER]].finder, users_current_home());
                    }
                } else if (item == 3) {
                    app_open_window(APP_TERMINAL, "Terminal");
                } else if (item == 4) {
                    app_open_window(APP_CALCULATOR, "Calculator");
                } else if (item == 5) {
                    settings_toggle_dark_mode();
                    compositor_set_dark_mode(settings_get()->dark_mode);
                    compositor_set_wallpaper(settings_get()->dark_mode
                        ? "/System/Wallpapers/Tahoe Dark.raw"
                        : "/System/Wallpapers/Tahoe Light.raw");
                } else if (item == 6) {
                    users_lock_session();
                    login_password[0] = '\0';
                    login_status[0] = '\0';
                }
                return;
            }
        }

        if (active_menu > 0) {
            int mx = (active_menu == 1) ? 6 :
                     (active_menu == 2) ? 136 :
                     (active_menu == 3) ? 180 :
                     (active_menu == 4) ? 224 : 268;
            int mw = 210;
            int mcount = (active_menu == 1) ? 7 : (active_menu == 2) ? 5 : 4;
            int my = MENU_BAR_HEIGHT + 4;
            if (x >= mx && x <= mx + mw && y >= my + 6 && y < my + 6 + mcount * 22) {
                int item = (y - (my + 6)) / 22;
                int menu = active_menu;
                active_menu = 0;
                if (menu == 1) {
                    if (item == 0) app_open_window(APP_ABOUT, "About ojjyOS");
                    else if (item == 1) app_open_window(APP_SETTINGS, "Settings");
                    else if (item == 2) app_open_window(APP_MONITOR, "Activity Monitor");
                    else if (item == 3) {
                        users_lock_session();
                        login_password[0] = '\0';
                        login_status[0] = '\0';
                    } else if (item == 4) {
                        users_logout();
                        login_password[0] = '\0';
                        login_status[0] = '\0';
                    } else if (item == 5) {
                        system_request_restart();
                    } else if (item == 6) {
                        system_request_shutdown();
                    }
                } else if (menu == 2) {
                    if (item == 0) app_open_window(APP_FINDER, "Finder");
                    else if (item == 1) app_open_window(APP_TEXTEDIT, "TextEdit");
                    else if (item == 2) {
                        app_open_window(APP_FINDER, "Finder");
                        if (app_window_index[APP_FINDER] >= 0) {
                            finder_set_path(&app_states[app_window_index[APP_FINDER]].finder,
                                            users_current_home());
                        }
                    } else if (item == 3) {
                        if (active_window_index >= 0 && active_window_index < window_count) {
                            AppWindowState *st = &app_states[active_window_index];
                            if (st->type == APP_TEXTEDIT) textedit_save(&st->textedit);
                            else if (st->type == APP_NOTES) textedit_save(&st->notes);
                        }
                    } else if (item == 4) {
                        compositor_handle_key(KEY_Q, 0, INPUT_MOD_SUPER);
                    }
                } else if (menu == 3) {
                    if (item == 0) compositor_handle_key(KEY_C, 0, INPUT_MOD_SUPER);
                    else if (item == 1) compositor_handle_key(KEY_X, 0, INPUT_MOD_SUPER);
                    else if (item == 2) compositor_handle_key(KEY_V, 0, INPUT_MOD_SUPER);
                    else if (item == 3 && active_window_index >= 0 && active_window_index < window_count) {
                        AppWindowState *st = &app_states[active_window_index];
                        if (st->type == APP_TEXTEDIT) textedit_clear_selection(&st->textedit);
                        else if (st->type == APP_NOTES) textedit_clear_selection(&st->notes);
                    }
                } else if (menu == 4) {
                    if (item == 0) {
                        settings_toggle_dark_mode();
                        compositor_set_dark_mode(settings_get()->dark_mode);
                        compositor_set_wallpaper(settings_get()->dark_mode
                            ? "/System/Wallpapers/Tahoe Dark.raw"
                            : "/System/Wallpapers/Tahoe Light.raw");
                    } else if (item == 1) {
                        settings_toggle_reduced_transparency();
                    } else if (item == 2) {
                        overlay_set(OVERLAY_MISSION_CONTROL);
                    } else if (item == 3) {
                        overlay_set(OVERLAY_LAUNCHPAD);
                    }
                } else if (menu == 5) {
                    if (item == 0 && active_window_index >= 0 && active_window_index < window_count) {
                        windows[active_window_index].minimized = true;
                    } else if (item == 1 && active_window_index >= 0 && active_window_index < window_count) {
                        CompositorWindow *win = &windows[active_window_index];
                        if (win->maximized) {
                            win->x = win->saved_x;
                            win->y = win->saved_y;
                            win->w = win->saved_w;
                            win->h = win->saved_h;
                            win->maximized = false;
                        } else {
                            win->saved_x = win->x;
                            win->saved_y = win->y;
                            win->saved_w = win->w;
                            win->saved_h = win->h;
                            win->x = 12;
                            win->y = MENU_BAR_HEIGHT + 8;
                            win->w = (int)comp_width - 24;
                            win->h = (int)comp_height - MENU_BAR_HEIGHT - DOCK_HEIGHT - 36;
                            win->maximized = true;
                        }
                    } else if (item == 2) {
                        app_open_window(APP_CALCULATOR, "Calculator");
                    } else if (item == 3) {
                        app_open_window(APP_MONITOR, "Activity Monitor");
                    }
                }
                return;
            }
            if (y > MENU_BAR_HEIGHT) {
                active_menu = 0;
            }
        }

        if (y <= MENU_BAR_HEIGHT) {
            if (x >= 4 && x < 52) {
                active_menu = (active_menu == 1) ? 0 : 1;
                overlay_set(OVERLAY_NONE);
                return;
            }
            if (x >= 135 && x < 179) {
                active_menu = (active_menu == 2) ? 0 : 2;
                overlay_set(OVERLAY_NONE);
                return;
            }
            if (x >= 179 && x < 223) {
                active_menu = (active_menu == 3) ? 0 : 3;
                overlay_set(OVERLAY_NONE);
                return;
            }
            if (x >= 223 && x < 267) {
                active_menu = (active_menu == 4) ? 0 : 4;
                overlay_set(OVERLAY_NONE);
                return;
            }
            if (x >= 267 && x < 330) {
                active_menu = (active_menu == 5) ? 0 : 5;
                overlay_set(OVERLAY_NONE);
                return;
            }
            if (x >= (int)comp_width - 230 && x < (int)comp_width - 140) {
                active_menu = 0;
                overlay_set(OVERLAY_NONE);
                app_open_window(APP_SETTINGS, "Settings");
                if (app_window_index[APP_SETTINGS] >= 0) {
                    app_states[app_window_index[APP_SETTINGS]].settings.page = SETTINGS_PAGE_USERS;
                }
                return;
            }
            if (x >= (int)comp_width - 140) {
                active_menu = 0;
                overlay_set(overlay == OVERLAY_CONTROL_CENTER ? OVERLAY_NONE : OVERLAY_CONTROL_CENTER);
                return;
            }
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
                int toggle_y = panel_y + 40;
                int btn_y = panel_y + CONTROL_CENTER_HEIGHT - 44;
                int btn_w = (CONTROL_CENTER_WIDTH - 40) / 2;
                int btn_h = 28;
                if (y >= toggle_y && y < toggle_y + 20) {
                    settings_toggle_wifi();
                } else if (y >= toggle_y + 22 && y < toggle_y + 42) {
                    settings_toggle_bluetooth();
                } else if (y >= toggle_y + 44 && y < toggle_y + 64) {
                    settings_toggle_dark_mode();
                    compositor_set_dark_mode(settings_get()->dark_mode);
                    compositor_set_wallpaper(settings_get()->dark_mode
                        ? "/System/Wallpapers/Tahoe Dark.raw"
                        : "/System/Wallpapers/Tahoe Light.raw");
                } else if (y >= toggle_y + 66 && y < toggle_y + 86) {
                    settings_toggle_reduced_transparency();
                } else if (y >= btn_y && y <= btn_y + btn_h) {
                    if (x >= panel_x + 16 && x <= panel_x + 16 + btn_w) {
                        overlay_set(OVERLAY_NONE);
                        system_request_restart();
                    } else if (x >= panel_x + 24 + btn_w && x <= panel_x + 24 + 2 * btn_w) {
                        overlay_set(OVERLAY_NONE);
                        system_request_shutdown();
                    }
                }
                return;
            }
            overlay_set(OVERLAY_NONE);
            return;
        }

        if (overlay == OVERLAY_MISSION_CONTROL) {
            for (int i = 0; i < window_count; i++) {
                int cx, cy, cw, ch;
                mission_control_card_rect(i, window_count, &cx, &cy, &cw, &ch);
                if (x >= cx && x <= cx + cw && y >= cy && y <= cy + ch) {
                    windows[i].minimized = false;
                    strncpy(active_app_name, app_name_from_type(app_states[i].type),
                            sizeof(active_app_name) - 1);
                    active_app_name[sizeof(active_app_name) - 1] = '\0';
                    raise_window_to_top(i);
                    overlay_set(OVERLAY_NONE);
                    return;
                }
            }
            overlay_set(OVERLAY_NONE);
            return;
        }

        if (overlay == OVERLAY_APP_SWITCHER) {
            /* Click an icon to activate it; click anywhere else to
             * dismiss (the old code let clicks fall through to the
             * windows and dock behind the overlay) */
            int count = app_registry_count();
            int running[APP_REGISTRY_MAX];
            int running_count = 0;
            for (int i = 0; i < count; i++) {
                AppInfo *app = app_registry_get(i);
                if (app && app->running) running[running_count++] = i;
            }
            if (running_count > 0) {
                int shown = MIN(running_count, 8);
                int width = 40 + shown * 64;
                int px = ((int)comp_width - width) / 2;
                int py = 160;
                for (int i = 0; i < shown; i++) {
                    int icon_x = px + 20 + i * 64;
                    if (x >= icon_x - 6 && x <= icon_x + 46 &&
                        y >= py + 28 && y <= py + 80) {
                        launch_app_index(running[i], timer_get_ticks());
                        break;
                    }
                }
            }
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
                    int half = icon_base / 2;
                    /* Hit box matches the drawn icon (the old +/-size box
                     * was 2x the icon and overlapped neighbors) */
                    if (x >= icon_center_x - half && x <= icon_center_x + half &&
                        y >= icon_center_y - half && y <= icon_center_y + half) {
                        launch_app_index(i, timer_get_ticks());
                        return;
                    }
                }
            }
        }

        bool hit_window = false;
        for (int i = window_count - 1; i >= 0; i--) {
            CompositorWindow *win = &windows[i];
            AppWindowState *state = &app_states[i];
                if (win->minimized) {
                    continue;  /* Minimized windows are not on screen */
                }
                if (x >= win->x && x < win->x + win->w && y >= win->y && y < win->y + win->h) {
                    hit_window = true;
                    active_window_index = i;
                    strncpy(active_app_name, app_name_from_type(state->type),
                            sizeof(active_app_name) - 1);
                    if (wm_hooks.on_focus) {
                        wm_hooks.on_focus(win->id);
                    }

                if (!win->maximized &&
                    x >= win->x + win->w - 18 && y >= win->y + win->h - 18) {
                    resizing = true;
                    resize_index = i;
                    resize_start_x = x;
                    resize_start_y = y;
                    resize_orig_w = win->w;
                    resize_orig_h = win->h;
                    if (i != window_count - 1) {
                        raise_window_to_top(i);
                    }
                    break;
                } else if (y < win->y + 32) {
                    /* Traffic lights: close (red), minimize (yellow), maximize/zoom (green) */
                    if (x >= win->x + 10 && x <= win->x + 18 && y >= win->y + 10 && y <= win->y + 18) {
                        AppType closing = state->type;
                        destroy_window_internal(win->id);
                        if (app_registry_find_by_name(active_app_name) >= 0) {
                            /* Mark the app not running if this was its window */
                            int aidx = app_registry_find_by_name(app_name_from_type(closing));
                            AppInfo *app = app_registry_get(aidx);
                            if (app) app->running = false;
                        }
                        if (active_window_index < 0) {
                            strncpy(active_app_name, "Finder", sizeof(active_app_name) - 1);
                        }
                        return;
                    } else if (x >= win->x + 22 && x <= win->x + 30 && y >= win->y + 10 && y <= win->y + 18) {
                        win->minimized = true;
                        return;
                    } else if (x >= win->x + 34 && x <= win->x + 42 && y >= win->y + 10 && y <= win->y + 18) {
                        if (win->maximized) {
                            win->x = win->saved_x;
                            win->y = win->saved_y;
                            win->w = win->saved_w;
                            win->h = win->saved_h;
                            win->maximized = false;
                        } else {
                            win->saved_x = win->x;
                            win->saved_y = win->y;
                            win->saved_w = win->w;
                            win->saved_h = win->h;
                            win->x = 12;
                            win->y = MENU_BAR_HEIGHT + 8;
                            win->w = (int)comp_width - 24;
                            win->h = (int)comp_height - MENU_BAR_HEIGHT - DOCK_HEIGHT - 36;
                            win->maximized = true;
                        }
                        if (i != window_count - 1) {
                            raise_window_to_top(i);
                        }
                        return;
                    }

                    if (!win->maximized) {
                        dragging = true;
                        drag_index = i;
                        drag_dx = x - win->x;
                        drag_dy = y - win->y;
                    }
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
                        } else if (rel_y >= 92 && rel_y < 112) {
                            finder_set_path(&state->finder, users_current_home());
                        } else if (rel_y >= 112 && rel_y < 132) {
                            char dpath[160];
                            users_make_home_path(dpath, sizeof(dpath), "Desktop");
                            finder_set_path(&state->finder, dpath);
                        }
                    } else {
                        int main_x = content_x + sidebar_w + 10;
                        if (x >= main_x) {
                            if (state->finder.view_mode == FINDER_VIEW_LIST) {
                                /* Rows are drawn from content_y+44 (the old
                                 * +36 offset made clicks select the wrong row) */
                                int row = (y - (content_y + 44)) / 18 + state->finder.scroll;
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
                                int idx = row * cols + col + state->finder.scroll;
                                if (col >= 0 && row >= 0 &&
                                    idx >= state->finder.scroll && idx < state->finder.entry_count) {
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
                    int content_h = win->h - 52;
                    int sidebar_w = 160;
                    if (x >= content_x && x <= content_x + sidebar_w) {
                        int rel_y = y - (content_y + 16);
                        int idx = rel_y / 20;
                        if (idx >= 0 && idx < SETTINGS_PAGE_COUNT) {
                            state->settings.page = (SettingsPage)idx;
                            state->settings.user_field = 0;
                        }
                    } else {
                        int main_x = content_x + sidebar_w + 10;
                        if (state->settings.page == SETTINGS_PAGE_APPEARANCE &&
                            x >= main_x + 210 && x <= main_x + 250 && y >= content_y + 38 && y <= content_y + 52) {
                            settings_toggle_dark_mode();
                            compositor_set_dark_mode(settings_get()->dark_mode);
                            compositor_set_wallpaper(settings_get()->dark_mode
                                ? "/System/Wallpapers/Tahoe Dark.raw"
                                : "/System/Wallpapers/Tahoe Light.raw");
                        }
                        if (state->settings.page == SETTINGS_PAGE_APPEARANCE &&
                            x >= main_x + 210 && x <= main_x + 250 && y >= content_y + 62 && y <= content_y + 76) {
                            settings_toggle_time_format();
                        }
                        if (state->settings.page == SETTINGS_PAGE_APPEARANCE &&
                            x >= main_x + 230 && x <= main_x + 270 && y >= content_y + 86 && y <= content_y + 100) {
                            settings_toggle_reduced_transparency();
                        }
                        if (state->settings.page == SETTINGS_PAGE_APPEARANCE &&
                            x >= main_x + 230 && x <= main_x + 270 && y >= content_y + 110 && y <= content_y + 124) {
                            settings_toggle_reduced_motion();
                        }
                        if (state->settings.page == SETTINGS_PAGE_USERS) {
                            if (x >= main_x + 92 && x <= main_x + 242 &&
                                y >= content_y + 118 && y <= content_y + 138) {
                                state->settings.user_field = 1;
                            } else if (x >= main_x + 92 && x <= main_x + 242 &&
                                       y >= content_y + 144 && y <= content_y + 164) {
                                state->settings.user_field = 2;
                            } else if (x >= main_x + 250 && x <= main_x + 336 &&
                                       y >= content_y + 130 && y <= content_y + 154) {
                                if (users_change_password(users_current_username(),
                                                          state->settings.old_pw,
                                                          state->settings.new_pw) == 0) {
                                    strcpy(state->settings.msg, "Password updated & saved.");
                                    state->settings.msg_ok = true;
                                    state->settings.old_pw[0] = '\0';
                                    state->settings.new_pw[0] = '\0';
                                    state->settings.user_field = 0;
                                } else {
                                    strcpy(state->settings.msg, "Incorrect current password.");
                                    state->settings.msg_ok = false;
                                }
                            }
                            int by = content_y + content_h - 36;
                            if (y >= by && y <= by + 24) {
                                if (x >= main_x + 16 && x <= main_x + 128) {
                                    login_create_mode = true;
                                    login_active_field = 0;
                                    login_full_name[0] = '\0';
                                    login_username[0] = '\0';
                                    login_password[0] = '\0';
                                    login_confirm[0] = '\0';
                                    login_status[0] = '\0';
                                } else if (x >= main_x + 136 && x <= main_x + 232) {
                                    users_lock_session();
                                    login_password[0] = '\0';
                                    login_status[0] = '\0';
                                } else if (x >= main_x + 240 && x <= main_x + 338) {
                                    users_logout();
                                    login_password[0] = '\0';
                                    login_status[0] = '\0';
                                }
                            }
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
                } else if (state->type == APP_CALCULATOR) {
                    CalculatorState *calc = &state->calc;
                    int content_x = win->x + 12;
                    int content_y = win->y + 40;
                    int content_w = win->w - 24;
                    int content_h = win->h - 52;
                    int lcd_x = content_x + 12;
                    int lcd_y = content_y + 10;
                    int lcd_h = 44;
                    int grid_y = lcd_y + lcd_h + 10;
                    int grid_h = content_h - (lcd_h + 28);
                    int cell_w = (content_w - 24 - 3 * 6) / 4;
                    int cell_h = (grid_h - 4 * 6) / 5;
                    if (cell_h < 22) cell_h = 22;

                    for (int r = 0; r < 5; r++) {
                        for (int c = 0; c < 4; c++) {
                            int bx = lcd_x + c * (cell_w + 6);
                            int by = grid_y + r * (cell_h + 6);
                            if (x >= bx && x <= bx + cell_w && y >= by && y <= by + cell_h) {
                                if (r == 0 && c == 0) calc_reset(calc);
                                else if (r == 0 && c == 1) calc_press_neg(calc);
                                else if (r == 0 && c == 2) {
                                    int64_t v = calc_parse_display(calc) / 100;
                                    calc_format_int64(v, calc->display, sizeof(calc->display));
                                } else if (r == 0 && c == 3) calc_press_op(calc, '/');
                                else if (r == 1 && c == 0) calc_press_digit(calc, '7');
                                else if (r == 1 && c == 1) calc_press_digit(calc, '8');
                                else if (r == 1 && c == 2) calc_press_digit(calc, '9');
                                else if (r == 1 && c == 3) calc_press_op(calc, '*');
                                else if (r == 2 && c == 0) calc_press_digit(calc, '4');
                                else if (r == 2 && c == 1) calc_press_digit(calc, '5');
                                else if (r == 2 && c == 2) calc_press_digit(calc, '6');
                                else if (r == 2 && c == 3) calc_press_op(calc, '-');
                                else if (r == 3 && c == 0) calc_press_digit(calc, '1');
                                else if (r == 3 && c == 1) calc_press_digit(calc, '2');
                                else if (r == 3 && c == 2) calc_press_digit(calc, '3');
                                else if (r == 3 && c == 3) calc_press_op(calc, '+');
                                else if (r == 4 && c == 0) calc_press_digit(calc, '0');
                                else if (r == 4 && c == 1) {
                                    calc_press_digit(calc, '0');
                                    calc_press_digit(calc, '0');
                                } else if (r == 4 && c == 2) {
                                    size_t len = strlen(calc->display);
                                    if (len > 1 && !calc->clear_next && !calc->has_error) {
                                        calc->display[len - 1] = '\0';
                                    } else {
                                        strcpy(calc->display, "0");
                                        calc->clear_next = true;
                                    }
                                } else if (r == 4 && c == 3) calc_press_eq(calc);
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

                        /* Only clicks INSIDE the 7x6 grid may change the
                         * selected day (the old unchecked col/row also
                         * reacted to clicks in the sidebar/agenda) */
                        if (x >= grid_x && x < grid_x + grid_w &&
                            y >= grid_y + 14 && y < grid_y + 14 + cell_h * 6) {
                            int rel_x = x - grid_x;
                            int rel_y = y - (grid_y + 14);
                            int col = rel_x / cell_w;
                            int row = rel_y / cell_h;
                            if (col >= 0 && col < 7 && row >= 0 && row < 6) {
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
                    }

                    int agenda_x = content_x + content_w - agenda_w;
                    if (x >= agenda_x && x <= agenda_x + agenda_w && y >= content_y + 30 && y <= content_y + content_h - 40) {
                        int indices[8];
                        int count = calendar_events_for_day(cal, cal->year, cal->month, cal->selected_day, indices, 8);
                        int row = (y - (content_y + 30)) / 18;
                        /* Only the 6 rows that are actually drawn are
                         * clickable (the old row < count bound allowed
                         * selecting an event from uninitialized memory) */
                        int shown = MIN(count, 6);
                        if (row >= 0 && row < shown) {
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
                } else if (state->type == APP_WEB) {
                    WebState *web = &state->web;
                    int content_x = win->x + 12;
                    int content_y = win->y + 40;
                    int content_w = win->w - 24;

                    int bar_y = content_y + 8;
                    /* Back / forward buttons */
                    if (y >= bar_y && y <= bar_y + 22) {
                        if (x >= content_x + 8 && x <= content_x + 30) {
                            web_go_history(web, -1);
                            return;
                        }
                        if (x >= content_x + 34 && x <= content_x + 56) {
                            web_go_history(web, 1);
                            return;
                        }
                    }

                    /* Link lines */
                    int page_y = content_y + 40;
                    if (y >= page_y + 8) {
                        int row = (y - (page_y + 8)) / 18 + web->scroll;
                        if (row >= 0 && row < web->line_count && web->links[row][0]) {
                            web_load_page(web, web->links[row]);
                            return;
                        }
                    }
                    (void)content_w;
                }

                if (i != window_count - 1) {
                    raise_window_to_top(i);
                }
                break;
            }
        }

        /* Click landed on the desktop (no window or dock consumed it) */
        if (!hit_window) {
            desktop_handle_click(x, y, timer_get_ticks());
        }
    }

    if (up) {
        dragging = false;
        drag_index = -1;
        resizing = false;
        resize_index = -1;
    }

    if (dragging && drag_index >= 0 && drag_index < window_count) {
        windows[drag_index].x = x - drag_dx;
        windows[drag_index].y = y - drag_dy;
    }

    if (resizing && resize_index >= 0 && resize_index < window_count) {
        CompositorWindow *win = &windows[resize_index];
        int nw = resize_orig_w + (x - resize_start_x);
        int nh = resize_orig_h + (y - resize_start_y);
        int max_w = (int)comp_width - win->x - 8;
        int max_h = (int)comp_height - DOCK_HEIGHT - 24 - win->y;
        if (nw < 360) nw = 360;
        if (nh < 220) nh = 220;
        if (nw > max_w && max_w >= 360) nw = max_w;
        if (nh > max_h && max_h >= 220) nh = max_h;
        win->w = nw;
        win->h = nh;
        if (wm_hooks.on_resize) {
            wm_hooks.on_resize(win->id, nw, nh);
        }
    }
}

void compositor_handle_mouse_move(int32_t dx, int32_t dy)
{
     mark_dirty();

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

    if (resizing && resize_index >= 0 && resize_index < window_count) {
        CompositorWindow *win = &windows[resize_index];
        int nw = resize_orig_w + (cursor_x - resize_start_x);
        int nh = resize_orig_h + (cursor_y - resize_start_y);
        int max_w = (int)comp_width - win->x - 8;
        int max_h = (int)comp_height - DOCK_HEIGHT - 24 - win->y;
        if (nw < 360) nw = 360;
        if (nh < 220) nh = 220;
        if (nw > max_w && max_w >= 360) nw = max_w;
        if (nh > max_h && max_h >= 220) nh = max_h;
        win->w = nw;
        win->h = nh;
        if (wm_hooks.on_resize) {
            wm_hooks.on_resize(win->id, nw, nh);
        }
    }
}

/*
 * Current compositor cursor position. The compositor is the single
 * source of truth for the on-screen cursor (it applies the configured
 * mouse speed); main.c must hit-test button events at THIS position,
 * not at the input subsystem's raw tracking position.
 */
void compositor_get_cursor(int32_t *x, int32_t *y)
{
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

/*
 * Mouse wheel / trackpad scroll.
 * Scrolls the content of the active (non-minimized) window.
 */
void compositor_handle_scroll(int32_t dx, int32_t dy)
{
     mark_dirty();

   (void)dx;

    if (overlay != OVERLAY_NONE) return;
    if (active_window_index < 0 || active_window_index >= window_count) return;

    CompositorWindow *win = &windows[active_window_index];
    if (win->minimized) return;
    AppWindowState *state = &app_states[active_window_index];

    /* Three lines per wheel notch, macOS-style */
    int delta = (dy > 0) ? -3 : 3;

    switch (state->type) {
        case APP_FINDER:
            state->finder.scroll += delta;
            if (state->finder.scroll < 0) state->finder.scroll = 0;
            break;
        case APP_TERMINAL:
            state->terminal.scroll += delta;
            if (state->terminal.scroll < 0) state->terminal.scroll = 0;
            break;
        case APP_TEXTEDIT:
            state->textedit.scroll += delta;
            if (state->textedit.scroll < 0) state->textedit.scroll = 0;
            break;
        case APP_NOTES:
            state->notes.scroll += delta;
            if (state->notes.scroll < 0) state->notes.scroll = 0;
            break;
        case APP_CALENDAR:
            /* No scrollable region in the calendar yet */
            break;
        case APP_WEB:
            state->web.scroll += delta;
            if (state->web.scroll < 0) state->web.scroll = 0;
            break;
        default:
            break;
    }

    /* Draw functions clamp scroll to the valid range */
}

/*
 * Key release notification (used for macOS-style behaviors such as
 * dismissing the app switcher when Super is let go).
 */
void compositor_handle_key_release(KeyCode keycode, uint8_t modifiers)
{
     mark_dirty();

   bool super_now = (modifiers & INPUT_MOD_SUPER) != 0;

    if ((keycode == KEY_LSUPER || keycode == KEY_RSUPER) && !super_now) {
        if (overlay == OVERLAY_APP_SWITCHER) {
            /* Released Super while holding the switcher: activate selection */
            int count = app_registry_count();
            int running[APP_REGISTRY_MAX];
            int running_count = 0;
            for (int i = 0; i < count; i++) {
                AppInfo *app = app_registry_get(i);
                if (app && app->running) running[running_count++] = i;
            }
            if (running_count > 0) {
                launch_app_index(running[app_switcher_index % running_count], timer_get_ticks());
            }
            overlay_set(OVERLAY_NONE);
        }
    }
}

/*
 * Dirty tracking: with software rendering under emulation, redrawing
 * the whole screen every frame makes the visible repaint sweep look
 * like constant flashing. We now render to the off-screen back buffer
 * and only redraw (and present) when something actually changed:
 * input, animations, dock bounce, or the minute hand of the clock.
 */
static void mark_dirty(void)
{
    need_redraw = true;
}

static bool any_dock_bounce(uint64_t now_ms)
{
    int count = app_registry_count();
    for (int i = 0; i < count; i++) {
        AppInfo *app = app_registry_get(i);
        if (app && app->bounce_until > now_ms) {
            return true;
        }
    }
    return false;
}

static bool update_animations(void)
{
    bool changed = false;
    bool no_motion = settings_get()->reduced_motion;

    for (int i = 0; i < window_count; i++) {
        CompositorWindow *win = &windows[i];
        if (win->animating) {
            changed = true;
            if (no_motion) {
                win->anim_open = 1000;
                win->animating = false;
            } else {
                win->anim_open += 40;
                if (win->anim_open >= 1000) {
                    win->anim_open = 1000;
                    win->animating = false;
                }
            }
        }
    }

    int step = no_motion ? 1000 : 120;
    int target_spotlight = (overlay == OVERLAY_SPOTLIGHT) ? 1000 : 0;
    int target_launchpad = (overlay == OVERLAY_LAUNCHPAD) ? 1000 : 0;
    int target_control = (overlay == OVERLAY_CONTROL_CENTER) ? 1000 : 0;
    int target_mission = (overlay == OVERLAY_MISSION_CONTROL) ? 1000 : 0;
    int target_switcher = (overlay == OVERLAY_APP_SWITCHER) ? 1000 : 0;

    if (anim_spotlight < target_spotlight) { anim_spotlight = MIN(1000, anim_spotlight + step); changed = true; }
    if (anim_spotlight > target_spotlight) { anim_spotlight = MAX(0, anim_spotlight - step); changed = true; }

    if (anim_launchpad < target_launchpad) { anim_launchpad = MIN(1000, anim_launchpad + step); changed = true; }
    if (anim_launchpad > target_launchpad) { anim_launchpad = MAX(0, anim_launchpad - step); changed = true; }

    if (anim_control_center < target_control) { anim_control_center = MIN(1000, anim_control_center + step); changed = true; }
    if (anim_control_center > target_control) { anim_control_center = MAX(0, anim_control_center - step); changed = true; }

    if (anim_mission_control < target_mission) { anim_mission_control = MIN(1000, anim_mission_control + step); changed = true; }
    if (anim_mission_control > target_mission) { anim_mission_control = MAX(0, anim_mission_control - step); changed = true; }

    if (anim_app_switcher < target_switcher) { anim_app_switcher = MIN(1000, anim_app_switcher + step); changed = true; }
    if (anim_app_switcher > target_switcher) { anim_app_switcher = MAX(0, anim_app_switcher - step); changed = true; }
    return changed;
}

void compositor_handle_right_click(int x, int y)
{
    mark_dirty();
    cursor_x = x;
    cursor_y = y;

    if (login_screen_visible()) {
        return;
    }

    active_menu = 0;
    overlay_set(OVERLAY_NONE);
    ctx_menu_open = true;
    ctx_menu_x = x;
    ctx_menu_y = y;
}

void compositor_tick(uint64_t now_ms)
{
    static bool login_frame_visible;
    uint64_t delta = now_ms - last_frame_ms;
    if (delta < 33) {
        return;
    }

    last_frame_ms = now_ms;
    int focused_type = (active_window_index >= 0 && active_window_index < window_count)
                       ? (int)app_states[active_window_index].type : -1;
    proc_tick(delta > 100 ? 33 : delta, focused_type);
    bool anim_changed = update_animations();

    /* Clock hands: redraw when the displayed minute rolls over */
    RtcTime tick_time;
    rtc_read_time(&tick_time);
    if (tick_time.minute != last_drawn_minute) {
        last_drawn_minute = tick_time.minute;
        need_redraw = true;
    }

    /* Dirty tracking: skip the frame entirely when nothing changed.
     * This is what keeps the software-rendered desktop from
     * flashing a full-screen repaint sweep under emulation. */
    bool animating = anim_changed || any_dock_bounce(now_ms);
    if (!need_redraw && !animating) {
        return;
    }
    need_redraw = false;

    draw_wallpaper();

    if (login_screen_visible()) {
        draw_login_screen();
        draw_cursor(cursor_x, cursor_y);
        fb_present();
        login_frame_visible = true;
        return;
    }

    draw_desktop_icons();  /* Desktop sits under windows */

    if (anim_launchpad > 0) {
        draw_launchpad(anim_launchpad);
    } else if (mission_control_active || anim_mission_control > 0) {
        for (int i = 0; i < window_count; i++) {
            if (!windows[i].minimized) {
                draw_window(i);
            }
        }
        draw_mission_control(anim_mission_control);
    } else {
        for (int i = 0; i < window_count; i++) {
            if (!windows[i].minimized) {
                draw_window(i);
            }
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

    draw_menu_dropdown();
    draw_context_menu();

    draw_cursor(cursor_x, cursor_y);
    fb_present();
    if (login_frame_visible) {
        serial_printf("[UI] Desktop presented\n");
        login_frame_visible = false;
    }
}
