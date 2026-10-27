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
#include "../drivers/input.h"
#include "../serial.h"
#include "../memory.h"
#include "../system.h"
#include "../users.h"
#include "../proc.h"
#include "../user.h"
#include "netcmd.h"
#include "calculator.h"
#include "app_metrics.h"
#include "notes.h"
#include "textedit.h"
#include "browser.h"
#include "draw.h"
#include "wallpaper.h"
#include "../image/jpeg.h"
#include "../image/wallpack.h"
#include "../uifont.h"
#include "icons.h"
#include "../net/net.h"

/* One window per app type at most (11 apps), so this never blocks a launch. */
#define COMPOSITOR_MAX_WINDOWS  12

#ifndef ABS
#define ABS(v) ((v) < 0 ? -(v) : (v))
#endif
#define MENU_BAR_HEIGHT         26
#define DOCK_HEIGHT             72
#define DOCK_ICON_BASE          36
#define DOCK_ICON_MAX           56
#define DOCK_HOVER_RADIUS       90
#define SPOTLIGHT_WIDTH         520
#define SPOTLIGHT_HEIGHT        58
#define CONTROL_CENTER_WIDTH    320
#define CONTROL_CENTER_HEIGHT   236
#define LAUNCHPAD_ICON_SIZE     56
#define LAUNCHPAD_COLS          4
#define LAUNCHPAD_ROWS          3
#define SEARCH_QUERY_MAX        48

static uint32_t comp_width = 0;
static uint32_t comp_height = 0;

static CompositorWindow windows[COMPOSITOR_MAX_WINDOWS];
static int window_count = 0;

static const ThemeTokens *theme = NULL;
static bool dark_mode = false;

/* Desktop pictures. Choices 0/1 are the built-in waves (drawn at any
 * size); 2.. are the JPEG pack on disk (image/wallpack.h). */
#define WP_BUILTIN 2
static bool wallpaper_loaded = false;      /* a picture (not the waves) is shown */
static int wp_choice = 0;
static uint32_t *wp_src;                   /* decoded picture, 0x00RRGGBB */
static int wp_src_w, wp_src_h;

/* Screen-sized wallpaper caches, rebuilt only when the wallpaper or theme
 * changes. Per-frame sampling becomes a single array read instead of a
 * divide per pixel (wallpaper) or ~100 reads per pixel (glass blur). */
/* Caches hold PHYSICAL pixels (2x on HiDPI screens). */
#define WP_CACHE_MAX_PIXELS     (3840 * 2400)
#define WP_CACHE_MAX_DIM        4096
#define WP_BLUR_RADIUS          10
/* Allocated at the real screen size from the page allocator (not BSS: the
 * kernel image must fit the RAM the firmware leaves free at its load address). */
static Color *wp_cache;
static Color *wp_blur;
static Color *wp_glass;        /* wp_blur pre-tinted with the window glass colour */
static Color wp_line_tmp[WP_CACHE_MAX_DIM];
static bool wp_cache_valid = false;
static bool wp_tahoe_dark = false;
static void wallpaper_rebuild_cache(void);
static void wallpaper_retint(void);

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
static bool pointer_absolute;            /* last move came from a tablet/USB pointer */
static uint64_t last_input_ms;           /* any key/pointer/scroll (fast-loop window) */
static bool last_tick_animating;

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
    SETTINGS_PAGE_NETWORK,
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
    int wp_scroll;              /* Wallpaper grid scroll (logical px) */
    int wp_scroll_max;          /* from the last layout */
} SettingsStateUi;

#define TERMINAL_MAX_LINES 200
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

/* The editor grid itself (TextEdit + Notes) lives in ui/textedit.c/h; the
 * clipboard is a UI-wide singleton the compositor owns. */
static char text_clipboard[TEXTEDIT_LINE_MAX];
static uint8_t last_modifiers;

void ui_clipboard_set(const char *text)
{
    strncpy(text_clipboard, text, sizeof(text_clipboard) - 1);
    text_clipboard[sizeof(text_clipboard) - 1] = '\0';
}

const char *ui_clipboard_get(void) { return text_clipboard; }
bool ui_super_held(void) { return (last_modifiers & INPUT_MOD_SUPER) != 0; }

typedef struct {
    int index;                  /* desktop picture being viewed */
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
#define CALENDAR_HEADER_H 60

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

/* Web app: the browser keeps one global instance in ui/browser.c. */
typedef struct {
    int unused;
} WebState;

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

/* The ~/Notes library. Notes has at most one window, so the list lives
 * here once instead of in every AppWindowState slot. */
static NotesLibrary notes_lib;
static bool notes_follow_selection;     /* scroll the sidebar to the open note */

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

static int ease_out(int t);
static bool draw_window_bare;      /* layer rendering: skip the drop shadow */
typedef struct {
    int count, hover;
    int x[APP_REGISTRY_MAX], size[APP_REGISTRY_MAX];
    int base, bottom;           /* icon baseline (bottom edge) */
    int bg_x, bg_y, bg_w, bg_h;
} DockLayout;
static void dock_layout(DockLayout *L);
static const char *app_name_from_type(AppType type);
static void draw_window(int idx);
static void draw_search_glyph(int x, int y, int size, Color c, uint8_t a);
static void draw_icon_id(IconId id, int x, int y, int size, uint8_t alpha);
static void draw_app_icon(const AppInfo *app, int x, int y, int size);
static uint8_t overlay_alpha(uint8_t base, int anim);
static int overlay_offset(int anim, int max_offset);
static AppType app_type_from_bundle(const char *bundle_id);
static int app_open_window(AppType type, const char *title);
static void finder_refresh(FinderState *state);
static void finder_update_preview(FinderState *state);
static bool path_in_trash(const char *path);
static void trash_dir(char *out, size_t size);
static void empty_trash(void);
static void textedit_load_file(TextEditState *edit, const char *path);
static void notes_save_open(TextEditState *edit);
static TextEditState *notes_editor(void);
static void notes_open_note(TextEditState *edit, int index);
static size_t str_append(char *dest, size_t size, const char *src);
static const char *str_find(const char *haystack, const char *needle);
static void terminal_make_prompt(const TerminalState *term, char *out, size_t size);
static void open_path(const char *path);
static bool str_contains_ci(const char *haystack, const char *needle);
static void normalize_path(char *path, size_t size);
static bool calendar_load(CalendarState *cal);
static void calendar_save(CalendarState *cal);
static int days_in_month(int year, int month);
static int weekday_of_date(int year, int month, int day);
static void raise_window_to_top(int idx);
static int destroy_window_internal(int id);
static void launch_app_index(int index, uint64_t now_ms);
static void finder_set_path(FinderState *state, const char *path);
static bool desktop_handle_click(int x, int y, uint64_t now_ms);
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
    if (!wallpaper_loaded || !wp_src) {
        return wallpaper_sample_procedural(x, y);
    }

    int sx = (x * wp_src_w) / (int)comp_width;
    int sy = (y * wp_src_h) / (int)comp_height;

    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= wp_src_w) sx = wp_src_w - 1;
    if (sy >= wp_src_h) sy = wp_src_h - 1;

    return wp_src[(size_t)sy * wp_src_w + sx] & 0xFFFFFF;
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
        int k = fb_ui_scale();
        x = clamp_coord(x * k, (int)fb_physical_width());
        y = clamp_coord(y * k, (int)fb_physical_height());
        return wp_cache[y * (int)fb_physical_width() + x];
    }
    return wallpaper_sample_uncached(x, y);
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
    uint32_t w = fb_physical_width(), h = fb_physical_height();
    int blur_radius = WP_BLUR_RADIUS * fb_ui_scale();
    if (w == 0 || h == 0 || (uint64_t)w * h > WP_CACHE_MAX_PIXELS ||
        w > WP_CACHE_MAX_DIM || h > WP_CACHE_MAX_DIM) {
        return;   /* Fall back to the slow uncached path */
    }
    if (!wp_cache) wp_cache = (Color *)pmm_alloc_bytes((size_t)w * h * sizeof(Color));
    if (!wp_blur) wp_blur = (Color *)pmm_alloc_bytes((size_t)w * h * sizeof(Color));
    if (!wp_glass) wp_glass = (Color *)pmm_alloc_bytes((size_t)w * h * sizeof(Color));
    if (!wp_cache || !wp_blur || !wp_glass) return;

    if (wallpaper_loaded) {
        wallpaper_scale_cover(wp_cache, (int)w, (int)h, wp_src, wp_src_w, wp_src_h);
    } else {
        wallpaper_render_tahoe(wp_cache, (int)w, (int)h, wp_tahoe_dark);
    }
    memcpy(wp_blur, wp_cache, (size_t)w * h * sizeof(Color));

    /* Separable box blur, two passes each way (approximates a Gaussian) */
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t y = 0; y < h; y++) {
            box_blur_line(&wp_blur[y * w], (int)w, 1, blur_radius);
        }
        for (uint32_t x = 0; x < w; x++) {
            box_blur_line(&wp_blur[x], (int)h, (int)w, blur_radius);
        }
    }
    wp_cache_valid = true;
    wallpaper_retint();
}

/* Window glass = blurred wallpaper + theme tint (redone on theme changes). */
static void wallpaper_retint(void)
{
    if (!wp_cache_valid) return;
    size_t n = (size_t)fb_physical_width() * fb_physical_height();
    for (size_t i = 0; i < n; i++)
        wp_glass[i] = blend(wp_blur[i], theme->glass_aqua, theme->glass.opacity[1]);
}

static int wallpaper_choice_count(void)
{
    return WP_BUILTIN + wallpack_count();
}

static const char *wallpaper_choice_name(int i)
{
    if (i == 0) return "ojjyOS Waves Light";
    if (i == 1) return "ojjyOS Waves Dark";
    const WallpackEntry *e = wallpack_entry(i - WP_BUILTIN);
    return e ? e->name : "";
}

static int wallpaper_choice_kind(int i)
{
    if (i < WP_BUILTIN) return i ? WALLPACK_DARK : WALLPACK_LIGHT;
    const WallpackEntry *e = wallpack_entry(i - WP_BUILTIN);
    return e ? (int)e->kind : WALLPACK_ANY;
}

static int wallpaper_find(const char *name)
{
    if (!name || !name[0]) return -1;
    for (int i = 0; i < wallpaper_choice_count(); i++)
        if (strcmp(wallpaper_choice_name(i), name) == 0) return i;
    return -1;
}

/* The same picture in the other appearance: "Tahoe Light" <-> "Tahoe Dark",
 * "... Day" <-> "... Night", "... Dawn" <-> "... Dusk". */
static int wallpaper_counterpart(int i, bool dark)
{
    int kind = wallpaper_choice_kind(i);
    if (kind == WALLPACK_ANY || kind == (dark ? WALLPACK_DARK : WALLPACK_LIGHT)) return i;
    static const char *const pairs[3][2] = { { "Light", "Dark" }, { "Day", "Night" }, { "Dawn", "Dusk" } };
    const char *name = wallpaper_choice_name(i);
    size_t len = strlen(name);
    for (int p = 0; p < 3; p++) {
        const char *from = pairs[p][dark ? 0 : 1], *to = pairs[p][dark ? 1 : 0];
        size_t fl = strlen(from);
        if (len <= fl || len - fl + strlen(to) >= 64 || strcmp(name + len - fl, from) != 0) continue;
        char other[64];
        memcpy(other, name, len - fl);
        other[len - fl] = '\0';
        memcpy(other + len - fl, to, strlen(to) + 1);
        int j = wallpaper_find(other);
        if (j >= 0) return j;
    }
    return i;
}

/* Decode pack picture `i` into wp_src. Buffers are sized once for the
 * largest picture in the pack and then reused. */
static bool wallpaper_decode(int i, uint32_t **dst, int *dw, int *dh)
{
    static uint8_t *jpeg_buf;
    static size_t jpeg_cap;
    const WallpackEntry *e = wallpack_entry(i - WP_BUILTIN);
    if (!e) return false;
    if (!jpeg_buf) {
        for (int k = 0; k < wallpack_count(); k++) {
            size_t need = ((size_t)wallpack_entry(k)->image_bytes + 511) & ~(size_t)511;
            if (need > jpeg_cap) jpeg_cap = need;
        }
        jpeg_buf = (uint8_t *)pmm_alloc_bytes(jpeg_cap);
        if (!jpeg_buf) return false;
    }
    int len = wallpack_read(i - WP_BUILTIN, false, jpeg_buf, jpeg_cap);
    int w = 0, h = 0;
    if (len <= 0 || !jpeg_info(jpeg_buf, (size_t)len, &w, &h) ||
        (uint32_t)w != e->width || (uint32_t)h != e->height) return false;
    if (!*dst) {
        size_t most = 0;
        for (int k = 0; k < wallpack_count(); k++) {
            const WallpackEntry *o = wallpack_entry(k);
            if ((size_t)o->width * o->height > most) most = (size_t)o->width * o->height;
        }
        *dst = (uint32_t *)pmm_alloc_bytes(most * sizeof(uint32_t));
        if (!*dst) return false;
    }
    if (!jpeg_decode(jpeg_buf, (size_t)len, *dst, (size_t)w * h)) return false;
    *dw = w;
    *dh = h;
    return true;
}

/* Show choice `i` (falls back to the waves if a picture can't be read). */
static void wallpaper_show(int i)
{
    if (i < 0 || i >= wallpaper_choice_count()) i = dark_mode ? 1 : 0;
    if (i >= WP_BUILTIN) {
        uint64_t t0 = timer_get_ticks();
        if (wallpaper_decode(i, &wp_src, &wp_src_w, &wp_src_h)) {
            serial_printf("[UI] Desktop picture '%s' %dx%d (%d ms)\n", wallpaper_choice_name(i),
                          wp_src_w, wp_src_h, (int)(timer_get_ticks() - t0));
        } else {
            serial_printf("[UI] Desktop picture '%s' unreadable\n", wallpaper_choice_name(i));
            i = dark_mode ? 1 : 0;
        }
    }
    wp_choice = i;
    wallpaper_loaded = i >= WP_BUILTIN;
    wp_tahoe_dark = i == 1;
    wallpaper_rebuild_cache();
    mark_dirty();
}

/* A choice made by the user: remembered, and Light/Dark pictures switch
 * the appearance to match (as the dark Tahoe picture always did). */
static void wallpaper_choose(int i)
{
    wallpaper_show(i);
    SettingsState *st = settings_get();
    strncpy(st->wallpaper, wallpaper_choice_name(wp_choice), sizeof(st->wallpaper) - 1);
    st->wallpaper[sizeof(st->wallpaper) - 1] = '\0';
    int kind = wallpaper_choice_kind(wp_choice);
    if (kind != WALLPACK_ANY && (kind == WALLPACK_DARK) != dark_mode)
        compositor_set_dark_mode(kind == WALLPACK_DARK);
    settings_save();
}

/* Light/Dark toggle (menu, Control Center, Settings): paired pictures follow. */
static void toggle_appearance(void)
{
    settings_toggle_dark_mode();
    compositor_set_dark_mode(settings_get()->dark_mode);
    int other = wallpaper_counterpart(wp_choice, dark_mode);
    if (other != wp_choice) wallpaper_choose(other);
}

/* All shapes go through the anti-aliased span renderer (gfx.c). */
static GfxSurface *surface(void)
{
    static GfxSurface s;
    fb_get_surface(&s);
    return &s;
}

static void draw_rounded_rect_blend(int x, int y, int w, int h, int r, Color color, uint8_t alpha)
{
    gfx_round_rect(surface(), x, y, w, h, r, color, alpha);
}

/* Inter UI text (uifont.h); y is the top of the line box. */
static int ui_text(int x, int y, const char *s, TextStyle st, Color c)
{
    return text_draw(surface(), x, y, s, st, c);
}

/* Text vertically centred in the box [top, top + h). */
static int ui_text_mid(int x, int top, int h, const char *s, TextStyle st, Color c)
{
    return text_draw(surface(), x, text_y_center(top, h, st), s, st, c);
}

/* Frosted glass panel: blurred wallpaper + theme tint + hairline rim. */
/* Liquid Glass (Tahoe): the live frame behind the panel - windows included -
 * is blurred, saturated and bent at the rim, with a specular edge. `tint`
 * is the old frosted opacity; Liquid Glass is clearer, so it is scaled down.
 * Reduce Transparency keeps a solid panel. */
static uint32_t *glass_scratch;

static void glass_params(GfxGlass *g, uint8_t tint, uint8_t fade)
{
    bool dark = settings_get()->dark_mode;
    g->tint = dark ? RGB(28, 34, 46) : COLOR_WHITE;
    g->tint_alpha = (uint8_t)(dark ? 70 + tint * 45 / 100 : 30 + tint * 40 / 100);
    if (settings_get()->glass_tint) g->tint_alpha = (uint8_t)MIN(255, g->tint_alpha * 3 / 2);
    g->blur = 10;
    g->refract = 7;
    g->band = 12;
    g->specular = dark ? 120 : 200;
    g->saturate = 80;
    g->alpha = fade;
}

static void ui_glass_fade(int x, int y, int w, int h, int r, uint8_t tint, uint8_t fade)
{
    if (!fade) return;
    if (!settings_get()->reduced_transparency) {
        if (!glass_scratch) {
            size_t px = (size_t)fb_physical_width() * fb_physical_height();
            glass_scratch = (uint32_t *)pmm_alloc_bytes(px * sizeof(uint32_t));
            gfx_set_scratch(glass_scratch, glass_scratch ? px : 0);
        }
        GfxGlass g;
        glass_params(&g, tint, fade);
        gfx_liquid_glass(surface(), x, y, w, h, r, &g);
        gfx_round_rect_stroke(surface(), x, y, w, h, r, settings_get()->dark_mode ? COLOR_WHITE : COLOR_BLACK,
                              (uint8_t)((settings_get()->dark_mode ? 30 : 18) * fade / 255));
    } else {
        draw_rounded_rect_blend(x, y, w, h, r, theme->dock_tint, (uint8_t)(245 * fade / 255));
        gfx_round_rect_stroke(surface(), x, y, w, h, r, COLOR_WHITE,
                              (uint8_t)((settings_get()->dark_mode ? 40 : 110) * fade / 255));
    }
}

static void ui_glass(int x, int y, int w, int h, int r, uint8_t tint)
{
    ui_glass_fade(x, y, w, h, r, tint, 255);
}

static void draw_shadow(int x, int y, int w, int h, int radius)
{
    gfx_shadow(surface(), x, y, w, h, radius, 22, 8, theme->shadow, 90);
}

/* Window chrome layout shared by drawing and hit-testing. Traffic lights
 * are 12px discs; the title follows them; Finder puts its back/forward and
 * view buttons in the unified titlebar after the title. */
#define TL_SIZE   12
#define TL_TOP    12
#define TL_X(i)   (14 + (i) * 20)
#define TITLE_X   80
static int finder_toolbar_x(const CompositorWindow *win, int base_x)
{
    return base_x + TITLE_X + text_width(win->title, TEXT_BODY_BOLD) + 18;
}
static bool hit_traffic_light(const CompositorWindow *win, int i, int x, int y)
{
    return x >= win->x + TL_X(i) - 2 && x <= win->x + TL_X(i) + TL_SIZE + 2 &&
           y >= win->y + TL_TOP - 2 && y <= win->y + TL_TOP + TL_SIZE + 2;
}

/* Settings > Appearance rows: 0 appearance, 1 accent swatches, 2 liquid
 * glass, 3 menu bar, 4 window corners, 5 terminal theme, 6-8 switches. */
#define APPEARANCE_ROW_Y(i) (40 + (i) * 30)
#define APPEARANCE_ROWS 9
#define ACCENT_SWATCH  18
#define ACCENT_PITCH   30
static int accent_row_x(int main_x, int main_w)
{
    int total = (theme_accent_count() - 1) * ACCENT_PITCH + ACCENT_SWATCH;
    return main_x + main_w - 16 - total;
}
static const char *const term_theme_names[4] = { "Tahoe Dark", "Light", "Ocean", "Classic Green" };
static int termtheme_index(void)
{
    const char *name = settings_get()->termtheme;
    for (int i = 0; i < 4; i++) {
        if (strcmp(name, term_theme_names[i]) == 0) return i;
    }
    return 0;
}
/* ---- Liquid Glass controls (LG4) ----------------------------------
 * Small controls don't sample the backdrop (too slow per control); they
 * are a translucent fill + the specular rim of the glass material + a
 * hairline edge, optionally raised with a soft shadow. With Reduce
 * Transparency they become solid. */
typedef enum { CTL_GLASS, CTL_PRIMARY, CTL_KNOB, CTL_TRACK_ON } CtlKind;
typedef struct {
    uint8_t kind, alpha, raised, dark, solid, scale, pad[2];
    int16_t w, h, r, pad2;
    Color color;
} CtlSpec;

/* Draw one control at logical (m, m) of surface t (or at (x, y) of the screen). */
static void ctl_render(GfxSurface *t, const CtlSpec *c, int x, int y)
{
    int w = c->w, h = c->h, r = c->r;
    switch (c->kind) {
    case CTL_GLASS:
        if (c->raised) gfx_shadow(t, x, y, w, h, r, 4, 1, COLOR_BLACK, c->dark ? 70 : 40);
        gfx_round_rect(t, x, y, w, h, r, c->color, c->solid ? 255 : c->alpha);
        if (!c->solid) gfx_round_rect_rim(t, x, y, w, h, r, c->dark ? 70 : 150);
        gfx_round_rect_stroke(t, x, y, w, h, r, c->dark ? COLOR_WHITE : COLOR_BLACK, c->dark ? 34 : 22);
        break;
    case CTL_PRIMARY:
        gfx_shadow(t, x, y, w, h, r, 5, 2, c->color, 70);
        gfx_round_rect_vgradient(t, x, y, w, h, r, fb_blend(c->color, COLOR_WHITE, 46), c->color, 255);
        if (!c->solid) gfx_round_rect_rim(t, x, y, w, h, r, 140);
        break;
    case CTL_KNOB:
        gfx_shadow(t, x, y, w, h, r, 3, 1, COLOR_BLACK, 70);
        gfx_round_rect(t, x, y, w, h, r, COLOR_WHITE, 255);
        if (!c->solid) gfx_round_rect_rim(t, x, y, w, h, r, 90);
        break;
    case CTL_TRACK_ON:
        gfx_round_rect(t, x, y, w, h, r, c->color, 255);
        if (!c->solid) gfx_round_rect_rim(t, x, y, w, h, r, 110);
        break;
    }
}

/* Controls barely change, so each distinct one is drawn once (on black and
 * on white -> exact straight-alpha sprite, gfx_sprite_extract) and blended
 * from the cache afterwards: ~0.5 ms -> ~0.02 ms per control. Pool memory
 * is PMM (never freed); when it fills up the whole cache starts over. */
#define CTL_MARGIN      8           /* logical px around the shape for shadows */
#define CTL_SPRITES     64
#define CTL_POOL_PX     (2u * 1024 * 1024)
#define CTL_MAX_PX      (160u * 1024)
typedef struct { CtlSpec spec; uint32_t *argb; int pw, ph; } CtlSprite;
static CtlSprite ctl_cache[CTL_SPRITES];
static int ctl_cache_count;
static uint32_t *ctl_pool, *ctl_black, *ctl_white;
static size_t ctl_pool_used;

static void ctl_draw(CtlSpec c, int x, int y)
{
    int k = fb_ui_scale();
    c.scale = (uint8_t)k;
    c.dark = settings_get()->dark_mode;
    c.solid = settings_get()->reduced_transparency;
    int pw = (c.w + 2 * CTL_MARGIN) * k, ph = (c.h + 2 * CTL_MARGIN) * k;
    size_t px = (size_t)pw * ph;
    if (!ctl_pool) {
        ctl_pool = (uint32_t *)pmm_alloc_bytes(CTL_POOL_PX * sizeof(uint32_t));
        ctl_black = (uint32_t *)pmm_alloc_bytes(CTL_MAX_PX * sizeof(uint32_t));
        ctl_white = (uint32_t *)pmm_alloc_bytes(CTL_MAX_PX * sizeof(uint32_t));
    }
    if (!ctl_pool || !ctl_black || !ctl_white || px > CTL_MAX_PX || c.w <= 0 || c.h <= 0) {
        ctl_render(surface(), &c, x, y);            /* uncacheable: draw directly */
        return;
    }
    for (int i = 0; i < ctl_cache_count; i++) {
        if (memcmp(&ctl_cache[i].spec, &c, sizeof(c)) == 0) {
            gfx_blit_argb(surface(), ctl_cache[i].argb, pw, ph, (x - CTL_MARGIN) * k, (y - CTL_MARGIN) * k);
            return;
        }
    }
    if (ctl_cache_count == CTL_SPRITES || ctl_pool_used + px > CTL_POOL_PX) {
        ctl_cache_count = 0;
        ctl_pool_used = 0;
    }
    for (size_t i = 0; i < px; i++) { ctl_black[i] = 0; ctl_white[i] = 0xFFFFFF; }
    GfxSurface t = { ctl_black, pw, ph, pw, 0, 0, pw, ph, k };
    ctl_render(&t, &c, CTL_MARGIN, CTL_MARGIN);
    t.pixels = ctl_white;
    ctl_render(&t, &c, CTL_MARGIN, CTL_MARGIN);
    CtlSprite *sp = &ctl_cache[ctl_cache_count++];
    sp->spec = c;
    sp->argb = ctl_pool + ctl_pool_used;
    sp->pw = pw;
    sp->ph = ph;
    ctl_pool_used += px;
    gfx_sprite_extract(ctl_black, ctl_white, sp->argb, px);
    gfx_blit_argb(surface(), sp->argb, pw, ph, (x - CTL_MARGIN) * k, (y - CTL_MARGIN) * k);
}

static CtlSpec ctl_spec(CtlKind kind, int w, int h, int r, Color color, uint8_t alpha, bool raised)
{
    CtlSpec c;
    memset(&c, 0, sizeof(c));
    c.kind = (uint8_t)kind;
    c.w = (int16_t)w; c.h = (int16_t)h; c.r = (int16_t)(r * 2 > h ? h / 2 : r);
    c.color = color;
    c.alpha = alpha;
    c.raised = raised;
    return c;
}

static void draw_glass_control(int x, int y, int w, int h, int r, Color fill, uint8_t fill_a, bool raised)
{
    ctl_draw(ctl_spec(CTL_GLASS, w, h, r, fill, fill_a, raised), x, y);
}

/* Neutral glass fill for buttons/fields on any background. */
static Color glass_fill(uint8_t *alpha)
{
    bool dark = settings_get()->dark_mode;
    *alpha = dark ? 46 : 150;
    return dark ? RGB(200, 214, 236) : COLOR_WHITE;
}

/* Capsule push button with centred label: primary = accent glass. */
static void draw_push_button(int x, int y, int w, int h, const char *label, bool primary)
{
    int r = h / 2;
    if (primary) {
        ctl_draw(ctl_spec(CTL_PRIMARY, w, h, r, theme->accent, 255, true), x, y);
    } else {
        uint8_t a;
        Color f = glass_fill(&a);
        draw_glass_control(x, y, w, h, r, f, a, true);
    }
    TextStyle st = primary ? TEXT_BODY_BOLD : TEXT_BODY_MEDIUM;
    int tw = text_width(label, st);
    ui_text_mid(x + (w - tw) / 2, y, h, label, st, primary ? COLOR_WHITE : theme->text);
}

/* Tahoe switch: glass track (accent when on) and a wide glass knob. */
static void draw_switch(int x, int y, bool on)
{
    bool dark = settings_get()->dark_mode;
    if (on) {
        ctl_draw(ctl_spec(CTL_TRACK_ON, 38, 20, 10, theme->accent, 255, false), x, y);
    } else {
        draw_glass_control(x, y, 38, 20, 10, dark ? RGB(90, 100, 118) : RGB(200, 207, 218), dark ? 200 : 230, false);
    }
    int kx = on ? x + 15 : x + 2;
    ctl_draw(ctl_spec(CTL_KNOB, 21, 16, 8, COLOR_WHITE, 255, true), kx, y + 2);
}

/* "512 bytes", "4 KB", "1.2 MB" */
static void format_size(uint64_t bytes, char *out, size_t size)
{
    char num[24];
    out[0] = '\0';
    if (bytes < 1024) {
        utoa(bytes, num, 10); str_append(out, size, num);
        str_append(out, size, bytes == 1 ? " byte" : " bytes");
    } else if (bytes < 1024 * 1024) {
        utoa((bytes + 512) / 1024, num, 10); str_append(out, size, num); str_append(out, size, " KB");
    } else {
        uint64_t tenths = (bytes * 10 + 512 * 1024) / (1024 * 1024);
        utoa(tenths / 10, num, 10); str_append(out, size, num); str_append(out, size, ".");
        utoa(tenths % 10, num, 10); str_append(out, size, num); str_append(out, size, " MB");
    }
}

/* Chevron "<" (dir -1) or ">" (dir +1), 6x10, anti-aliased. */
static void draw_chevron(int cx, int cy, int dir, Color c, uint8_t a)
{
    for (int k = 0; k <= 4; k++) {
        draw_rounded_rect_blend(cx + dir * (k - 2) - 1, cy - 5 + k, 2, 2, 1, c, a);
        draw_rounded_rect_blend(cx + dir * (k - 2) - 1, cy + 4 - k, 2, 2, 1, c, a);
    }
}

static IconId finder_entry_icon(VfsFileType type)
{
    return type == VFS_TYPE_DIR ? ICON_FOLDER : ICON_FILE;
}

/* Draw a directory entry's icon (apps use their app icon). */
static void finder_draw_entry_icon(const FinderState *state, const FinderEntry *entry, int x, int y, int size)
{
    if (entry->type == VFS_TYPE_BUNDLE) {
        char path[256];
        if (vfs_join_path(path, sizeof(path), state->path, entry->name) == 0) {
            for (int a = 0; a < app_registry_count(); a++) {
                AppInfo *app = app_registry_get(a);
                if (app && strcmp(app->path, path) == 0) { draw_app_icon(app, x, y, size); return; }
            }
        }
        draw_icon_id(ICON_FILE, x, y, size, 255);
        return;
    }
    draw_icon_id(finder_entry_icon(entry->type), x, y, size, 255);
}

static void draw_finder_window(const CompositorWindow *win, FinderState *state, int content_x, int content_y, int content_w, int content_h)
{
    if (state->needs_refresh) {
        finder_refresh(state);
    }
    bool dark = settings_get()->dark_mode;
    Color panel = dark ? RGB(18, 24, 36) : COLOR_WHITE;

    /* Click geometry (unchanged): sidebar rows at content_y + 30 + i*20,
     * list rows of 18 px from content_y + 44, icon grid 40 px + 18 gap. */
    int sidebar_w = 150;
    int preview_w = 180;
    draw_rounded_rect_blend(content_x, content_y, sidebar_w, content_h, 12, panel, dark ? 70 : 70);
    ui_text(content_x + 12, content_y + 10, "Favorites", TEXT_SMALL_BOLD, theme->text_muted);
    {
        /* Highlight the sidebar place the window is showing. */
        char desktop[160], trash[160];
        users_make_home_path(desktop, sizeof(desktop), "Desktop");
        trash_dir(trash, sizeof(trash));
        const char *places[6] = { "/Applications", "/System", "/Users", users_current_home(), desktop, trash };
        const char *labels[6] = { "Applications", "System", "Users", "Home", "Desktop", "Trash" };
        const IconId icons[6] = { ICON_FOLDER, ICON_DISK, ICON_FOLDER, ICON_FOLDER, ICON_FOLDER, ICON_TRASH };
        for (int i = 0; i < 6; i++) {
            bool here = strcmp(state->path, places[i]) == 0;
            int ry = content_y + 29 + i * 20;
            if (here) draw_rounded_rect_blend(content_x + 6, ry, sidebar_w - 12, 20, 6,
                                              dark ? COLOR_WHITE : theme->midnight, dark ? 30 : 20);
            draw_icon_id(icons[i], content_x + 12, ry + 2, 16, 255);
            ui_text_mid(content_x + 34, ry, 20, labels[i], here ? TEXT_BODY_BOLD : TEXT_BODY, theme->text);
        }
    }

    int main_x = content_x + sidebar_w + 10;
    int main_w = content_w - sidebar_w - preview_w - 20;

    draw_rounded_rect_blend(main_x, content_y, main_w, content_h, 12, panel, dark ? 50 : 120);
    text_draw_fit(surface(), main_x + 12, content_y + 9, state->path[0] ? state->path : "/",
                  TEXT_SMALL, theme->text_muted, main_w - 24);

    int preview_x = main_x + main_w + 10;
    draw_rounded_rect_blend(preview_x, content_y, preview_w, content_h, 12, panel, dark ? 60 : 90);
    if (state->preview_ready) {
        /* Big icon for the selection, then name, kind, size and a peek */
        bool is_dir = strcmp(state->preview_type, "Folder") == 0;
        draw_icon_id(is_dir ? ICON_FOLDER : ICON_FILE, preview_x + (preview_w - 64) / 2, content_y + 14, 64, 255);
        int nw = MIN(text_width(state->preview_name, TEXT_BODY_BOLD), preview_w - 20);
        text_draw_fit(surface(), preview_x + (preview_w - nw) / 2, content_y + 84, state->preview_name,
                      TEXT_BODY_BOLD, theme->text, preview_w - 20);
        char info[48];
        info[0] = '\0';
        str_append(info, sizeof(info), state->preview_type);
        if (state->preview_size > 0) {
            char sz[24];
            format_size(state->preview_size, sz, sizeof(sz));
            str_append(info, sizeof(info), "  \xE2\x80\xA2  ");
            str_append(info, sizeof(info), sz);
        }
        int iw = MIN(text_width(info, TEXT_SMALL), preview_w - 20);
        text_draw_fit(surface(), preview_x + (preview_w - iw) / 2, content_y + 104, info, TEXT_SMALL, theme->text_muted, preview_w - 20);
        for (int i = 0; i < 3; i++) {
            if (state->preview_lines[i][0]) {
                text_draw_fit(surface(), preview_x + 12, content_y + 132 + i * 17, state->preview_lines[i],
                              TEXT_SMALL, theme->text, preview_w - 24);
            }
        }
    } else {
        int nw = text_width("No Selection", TEXT_BODY);
        ui_text(preview_x + (preview_w - nw) / 2, content_y + content_h / 2 - 8, "No Selection", TEXT_BODY, theme->text_muted);
    }
    {
        char trash[160];
        trash_dir(trash, sizeof(trash));
        if (strcmp(state->path, trash) == 0) {
            bool has_items = state->entry_count > 0;
            int by = content_y + content_h - 36, bw = preview_w - 24;
            gfx_round_rect_vgradient(surface(), preview_x + 12, by, bw, 24, 8, RGB(240, 104, 96), RGB(214, 64, 58),
                                     has_items ? 255 : 110);
            int tw = text_width("Empty Trash", TEXT_BODY_BOLD);
            ui_text_mid(preview_x + 12 + (bw - tw) / 2, by, 24, "Empty Trash", TEXT_BODY_BOLD, COLOR_WHITE);
        }
    }

    if (state->view_mode == FINDER_VIEW_LIST) {
        /* Columns follow the panel width so they never spill into Preview. */
        int size_x = main_x + main_w - 64;
        int type_x = size_x - 64;
        int name_x = main_x + 34;
        ui_text(name_x, content_y + 27, "Name", TEXT_SMALL_BOLD, theme->text_muted);
        ui_text(type_x, content_y + 27, "Kind", TEXT_SMALL_BOLD, theme->text_muted);
        ui_text(size_x, content_y + 27, "Size", TEXT_SMALL_BOLD, theme->text_muted);
        draw_rounded_rect_blend(main_x + 8, content_y + 41, main_w - 16, 1, 0, COLOR_BLACK, 26);

        int visible = (content_h - 48) / 18;
        if (visible < 1) visible = 1;
        int max_scroll = state->entry_count - visible;
        if (max_scroll < 0) max_scroll = 0;
        if (state->scroll > max_scroll) state->scroll = max_scroll;
        if (state->scroll < 0) state->scroll = 0;

        int row_y = content_y + 44;
        for (int i = state->scroll; i < state->entry_count && i < state->scroll + visible; i++) {
            FinderEntry *entry = &state->entries[i];
            bool sel = i == state->selected;
            if (sel) {
                draw_rounded_rect_blend(main_x + 6, row_y - 1, main_w - 12, 18, 5, theme->accent, 235);
            } else if (state->drag_active && i == state->drag_hover_index && entry->type == VFS_TYPE_DIR) {
                draw_rounded_rect_blend(main_x + 6, row_y - 1, main_w - 12, 18, 5, theme->accent, 70);
            } else if (((i - state->scroll) & 1)) {
                draw_rounded_rect_blend(main_x + 6, row_y - 1, main_w - 12, 18, 5, dark ? COLOR_WHITE : theme->midnight, dark ? 8 : 9);
            }
            finder_draw_entry_icon(state, entry, main_x + 12, row_y, 16);
            Color fg = sel ? COLOR_WHITE : theme->text, mg = sel ? RGB(220, 235, 255) : theme->text_muted;
            text_draw_fit(surface(), name_x, text_y_center(row_y - 1, 18, TEXT_BODY), entry->name, TEXT_BODY, fg,
                          type_x - name_x - 8);

            const char *type = "Document";
            if (entry->type == VFS_TYPE_DIR) type = "Folder";
            if (entry->type == VFS_TYPE_BUNDLE) type = "Application";
            text_draw_fit(surface(), type_x, text_y_center(row_y - 1, 18, TEXT_SMALL), type, TEXT_SMALL, mg, size_x - type_x - 6);

            char sizebuf[24];
            if (entry->type == VFS_TYPE_FILE) format_size(entry->size, sizebuf, sizeof(sizebuf));
            else strcpy(sizebuf, "--");
            ui_text_mid(size_x, row_y - 1, 18, sizebuf, TEXT_SMALL, mg);

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
            bool sel = i == state->selected;
            if (sel || (state->drag_active && i == state->drag_hover_index && entry->type == VFS_TYPE_DIR))
                draw_rounded_rect_blend(ix - 4, iy - 4, icon + 8, icon + 8, 8, dark ? COLOR_WHITE : theme->midnight, 30);
            finder_draw_entry_icon(state, entry, ix, iy, icon);
            int lw = MIN(text_width(entry->name, TEXT_SMALL), icon + gap - 2);
            int lx = ix + icon / 2 - lw / 2;
            if (sel) draw_rounded_rect_blend(lx - 4, iy + icon + 4, lw + 8, 16, 5, theme->accent, 235);
            text_draw_fit(surface(), lx, iy + icon + 5, entry->name, TEXT_SMALL, sel ? COLOR_WHITE : theme->text, icon + gap - 2);
        }
    }

    int toolbar_y = content_y - 4;       /* = titlebar y + 36, as before */
    int tb_x = finder_toolbar_x(win, content_x - 12);
    bool can_back = state->history_pos > 0;
    bool can_fwd = state->history_pos + 1 < state->history_count;
    uint8_t ga;
    Color gf = glass_fill(&ga);
    /* Back/forward share one glass capsule (hit boxes unchanged). */
    draw_glass_control(tb_x, toolbar_y - 29, 46, 22, 11, gf, ga, true);
    draw_rounded_rect_blend(tb_x + 23, toolbar_y - 25, 1, 14, 0, theme->text_muted, 60);
    draw_chevron(tb_x + 11, toolbar_y - 18, -1, can_back ? theme->text : theme->text_muted, can_back ? 255 : 120);
    draw_chevron(tb_x + 35, toolbar_y - 18, 1, can_fwd ? theme->text : theme->text_muted, can_fwd ? 255 : 120);
    draw_glass_control(tb_x + 52, toolbar_y - 29, 52, 22, 11, gf, ga, true);
    {
        const char *mode = state->view_mode == FINDER_VIEW_LIST ? "List" : "Icons";
        int mw = text_width(mode, TEXT_SMALL_BOLD);
        ui_text_mid(tb_x + 52 + (52 - mw) / 2, toolbar_y - 28, 20, mode, TEXT_SMALL_BOLD, theme->text);
    }

    int search_x = main_x + main_w - 150;
    if (search_x < tb_x + 112) search_x = tb_x + 112;
    draw_glass_control(search_x, toolbar_y - 29, 140, 22, 11, gf, ga, false);
    if (state->rename_mode) {
        ui_text_mid(search_x + 8, toolbar_y - 28, 20, state->rename_buffer, TEXT_BODY, theme->text);
        int rw = text_width("Rename:", TEXT_SMALL_BOLD);
        ui_text_mid(search_x - 8 - rw, toolbar_y - 28, 20, "Rename:", TEXT_SMALL_BOLD, theme->text_muted);
    } else {
        draw_search_glyph(search_x + 7, toolbar_y - 24, 12, theme->text_muted, 255);
        ui_text_mid(search_x + 26, toolbar_y - 28, 20, state->search[0] ? state->search : "Search",
                    TEXT_BODY, state->search[0] ? theme->text : theme->text_muted);
    }

    if (state->drag_active) {
        const char *name = vfs_basename(state->drag_path);
        int dw = text_width(name, TEXT_BODY_MEDIUM) + 16;
        draw_rounded_rect_blend(cursor_x + 10, cursor_y + 10, dw, 22, 6, theme->accent, 220);
        ui_text_mid(cursor_x + 18, cursor_y + 10, 22, name, TEXT_BODY_MEDIUM, COLOR_WHITE);
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

/* ------------------------------------------------------------------ */
/* Network status (Wi-Fi tile, menu bar glyph, Settings > Network).    */
/* QEMU has no Wi-Fi radio: the tile drives the real virtual adapter.  */
/* ------------------------------------------------------------------ */

static char cc_message[48];
static uint64_t cc_message_until;

static const char *network_status_text(void)
{
    const NetInfo *ni = net_info();
    if (!ni->present) return "No adapter";
    if (!ni->enabled) return "Off";
    return ni->configured ? "Connected" : "Connecting...";
}

static void set_wifi(bool on)
{
    net_set_enabled(on);
    settings_get()->wifi_enabled = on;
    settings_save();
    mark_dirty();
}

/* A small Wi-Fi fan: dot + two widening arcs (bars), slashed when off. */
static void draw_wifi_glyph(int x, int y, Color c, bool off)
{
    /* Fan of three arcs over a dot, 16x14; dimmed and slashed when off. */
    GfxSurface *s = surface();
    int cx = x + 8, cy = y + 13;
    uint8_t a = off ? 90 : 255;
    gfx_arc_up(s, cx, cy, 0, 3, 45, c, a);
    gfx_arc_up(s, cx, cy, 6, 8, 45, c, a);
    gfx_arc_up(s, cx, cy, 11, 13, 45, c, a);
    if (off)
        for (int i = 0; i < 15; i++) draw_rounded_rect_blend(x + i, y + i - 1, 2, 2, 1, c, 255);
}

/* ---- Settings controls shared by drawing and clicks ---- */
#define SEG_W   240
#define SEG_H   24

/* Segmented control right-aligned in a settings row. */
static int seg_x(int main_x, int main_w) { return main_x + main_w - 16 - SEG_W; }

static void draw_segmented(int x, int y, const char *const *labels, int n, int selected)
{
    bool dark = settings_get()->dark_mode;
    /* Glass capsule track; the selected segment is a raised glass pill. */
    draw_glass_control(x, y, SEG_W, SEG_H, SEG_H / 2, dark ? COLOR_WHITE : theme->midnight, dark ? 22 : 16, false);
    int cw = SEG_W / n;
    for (int i = 0; i < n; i++) {
        int cx = x + i * cw;
        if (i == selected)
            draw_glass_control(cx + 2, y + 2, cw - 4, SEG_H - 4, (SEG_H - 4) / 2,
                               COLOR_WHITE, dark ? 62 : 245, true);
        int tw = text_width(labels[i], i == selected ? TEXT_BODY_MEDIUM : TEXT_BODY);
        ui_text_mid(cx + (cw - tw) / 2, y, SEG_H, labels[i], i == selected ? TEXT_BODY_MEDIUM : TEXT_BODY,
                    i == selected ? theme->text : theme->text_muted);
    }
}

/* Index of the segment under (px, py), or -1. */
static int segmented_hit(int x, int y, int n, int px, int py)
{
    if (px < x || px >= x + SEG_W || py < y || py >= y + SEG_H) return -1;
    return (px - x) / (SEG_W / n);
}

/* Settings > Appearance: one row click (segment, accent swatch or switch).
 * Every change logs a `[UI] Appearance key=value` line for live tests. */
static void appearance_page_click(int main_x, int main_w, int content_y, int row, int x, int y)
{
    SettingsState *s = settings_get();
    if (row < 0) return;
    int ry = content_y + APPEARANCE_ROW_Y(row);
    if (row == 0) {
        int i = segmented_hit(seg_x(main_x, main_w), ry + 1, 3, x, y);
        if (i < 0) return;
        s->appearance = (uint8_t)i;
        if (i < 2) {
            if ((i == 1) != dark_mode) toggle_appearance();
        } else {
            compositor_appearance_auto_tick();   /* Auto applies immediately */
        }
        settings_save();
        serial_printf("[UI] Appearance mode=%s\n", i == 0 ? "light" : i == 1 ? "dark" : "auto");
    } else if (row == 1) {
        int ax = accent_row_x(main_x, main_w);
        for (int a = 0; a < theme_accent_count(); a++) {
            if (x >= ax + a * ACCENT_PITCH && x < ax + a * ACCENT_PITCH + ACCENT_SWATCH &&
                y >= ry + 3 && y < ry + 3 + ACCENT_SWATCH) {
                s->accent = (uint8_t)a;
                theme_set_accent(a);
                settings_save();
                wallpaper_retint();
                serial_printf("[UI] Appearance accent=%s\n", theme_accent_name(a));
                return;
            }
        }
    } else if (row <= 5) {
        int n = row == 5 ? 4 : 2;
        int i = segmented_hit(seg_x(main_x, main_w), ry + 1, n, x, y);
        if (i < 0) return;
        if (row == 2) {
            s->glass_tint = (uint8_t)i;
            serial_printf("[UI] Appearance glass=%s\n", i ? "tinted" : "clear");
        } else if (row == 3) {
            s->menubar = (uint8_t)i;
            serial_printf("[UI] Appearance menubar=%s\n", i ? "solid" : "glass");
        } else if (row == 4) {
            s->corners = (uint8_t)i;
            serial_printf("[UI] Appearance corners=%s\n", i ? "round" : "standard");
        } else {
            settings_set_termtheme(term_theme_names[i]);
            serial_printf("[UI] Appearance termtheme=%s\n", term_theme_names[i]);
        }
        settings_save();
    } else if (row == 6) {
        settings_toggle_reduced_transparency();
    } else if (row == 7) {
        settings_toggle_reduced_motion();
    } else if (row == 8) {
        settings_toggle_time_format();
    }
}

/* Dock: size presets and magnification levels (relative to the size). */
static const uint8_t dock_sizes[3] = { 40, 48, 56 };
static int dock_size_index(void)
{
    int s = settings_get()->dock_size;
    return s < 44 ? 0 : s < 52 ? 1 : 2;
}
static int dock_magnify_level(void)
{
    int d = (int)settings_get()->dock_magnify - (int)MAX(40, settings_get()->dock_size);
    return d < 8 ? 0 : d < 24 ? 1 : 2;
}
static void dock_apply(int size_index, int level)
{
    SettingsState *st = settings_get();
    st->dock_size = dock_sizes[size_index];
    st->dock_magnify = (uint8_t)MIN(96, st->dock_size + level * 16);
    settings_save();
}

/* Wallpaper thumbnails: the built-in waves are rendered at the thumbnail's
 * physical size; pack pictures decode their 320x200 JPEG thumbnail once. */
#define WP_THUMB_W  144
#define WP_THUMB_H  90
static uint32_t *wp_thumbs[WP_BUILTIN + WALLPACK_MAX];
static int wp_thumb_px[WP_BUILTIN + WALLPACK_MAX][2];
static bool wp_thumb_failed[WP_BUILTIN + WALLPACK_MAX];

static void wallpaper_thumb_load(int which)
{
    int k = fb_ui_scale();
    if (which < WP_BUILTIN) {
        int w = WP_THUMB_W * k, h = WP_THUMB_H * k;
        wp_thumbs[which] = (uint32_t *)pmm_alloc_bytes((size_t)w * h * sizeof(uint32_t));
        if (!wp_thumbs[which]) { wp_thumb_failed[which] = true; return; }
        wallpaper_render_tahoe(wp_thumbs[which], w, h, which == 1);
        wp_thumb_px[which][0] = w;
        wp_thumb_px[which][1] = h;
        return;
    }
    static uint8_t *jpeg;
    const size_t jpeg_cap = 256 * 1024;
    if (!jpeg) jpeg = (uint8_t *)pmm_alloc_bytes(jpeg_cap);
    int len = jpeg ? wallpack_read(which - WP_BUILTIN, true, jpeg, jpeg_cap) : -1;
    int w = 0, h = 0;
    if (len > 0 && jpeg_info(jpeg, (size_t)len, &w, &h) && w <= 1024 && h <= 1024) {
        wp_thumbs[which] = (uint32_t *)pmm_alloc_bytes((size_t)w * h * sizeof(uint32_t));
        if (wp_thumbs[which] && jpeg_decode(jpeg, (size_t)len, wp_thumbs[which], (size_t)w * h)) {
            wp_thumb_px[which][0] = w;
            wp_thumb_px[which][1] = h;
            return;
        }
    }
    wp_thumb_failed[which] = true;   /* don't retry every frame */
}

static void draw_wallpaper_thumb(int which, int x, int y, bool selected)
{
    if (which < 0 || which >= WP_BUILTIN + WALLPACK_MAX) return;
    if (!wp_thumbs[which] && !wp_thumb_failed[which]) wallpaper_thumb_load(which);
    if (selected) gfx_round_rect_stroke(surface(), x - 3, y - 3, WP_THUMB_W + 6, WP_THUMB_H + 6, 11, theme->accent, 255),
                  gfx_round_rect_stroke(surface(), x - 2, y - 2, WP_THUMB_W + 4, WP_THUMB_H + 4, 10, theme->accent, 255);
    gfx_shadow(surface(), x, y, WP_THUMB_W, WP_THUMB_H, 8, 8, 3, COLOR_BLACK, 70);
    if (wp_thumbs[which])
        gfx_blit_scaled(surface(), wp_thumbs[which], wp_thumb_px[which][0], 0, 0, wp_thumb_px[which][0],
                        wp_thumb_px[which][1], x, y, WP_THUMB_W, WP_THUMB_H, 8, 255);
    else
        draw_rounded_rect_blend(x, y, WP_THUMB_W, WP_THUMB_H, 8, theme->text_muted, 60);
}

/* Settings > Wallpaper: a scrolling grid of every desktop picture. */
typedef struct { int x, top, bottom, cols, step_x, step_y, max_scroll; } WpGrid;

static void wallpaper_grid(int main_x, int main_w, int content_y, int content_h, WpGrid *g)
{
    int w = main_w - 32;
    g->x = main_x + 16;
    g->top = content_y + 60;
    g->bottom = content_y + content_h - 8;
    g->cols = MAX(1, (w + 16) / (WP_THUMB_W + 16));
    g->step_x = g->cols > 1 ? (w - WP_THUMB_W) / (g->cols - 1) : 0;
    g->step_y = WP_THUMB_H + 34;
    int rows = (wallpaper_choice_count() + g->cols - 1) / g->cols;
    g->max_scroll = MAX(0, rows * g->step_y + 8 - (g->bottom - g->top));
}

static void wallpaper_cell(const WpGrid *g, int scroll, int i, int *x, int *y)
{
    *x = g->x + (i % g->cols) * g->step_x;
    *y = g->top + 6 + (i / g->cols) * g->step_y - scroll;
}

static void draw_wallpaper_page(SettingsStateUi *state, int main_x, int main_w, int content_y, int content_h)
{
    ui_text(main_x + 16, content_y + 12, "Wallpaper", TEXT_HEADLINE, theme->text);
    ui_text(main_x + 16, content_y + 36, wallpack_count()
            ? "Light and Dark pictures also switch the appearance to match."
            : "Add the macOS Tahoe pictures: tools/import_wallpapers.py --fetch",
            TEXT_SMALL, theme->text_muted);
    WpGrid g;
    wallpaper_grid(main_x, main_w, content_y, content_h, &g);
    state->wp_scroll_max = g.max_scroll;
    if (state->wp_scroll > g.max_scroll) state->wp_scroll = g.max_scroll;
    if (state->wp_scroll < 0) state->wp_scroll = 0;
    fb_set_clip(main_x, g.top, main_w, g.bottom - g.top);
    for (int i = 0; i < wallpaper_choice_count(); i++) {
        int x, y;
        wallpaper_cell(&g, state->wp_scroll, i, &x, &y);
        if (y + g.step_y < g.top || y > g.bottom) continue;
        bool sel = i == wp_choice;
        draw_wallpaper_thumb(i, x, y, sel);
        const char *name = wallpaper_choice_name(i);
        TextStyle st = sel ? TEXT_BODY_MEDIUM : TEXT_SMALL;
        int nw = MIN(text_width(name, st), WP_THUMB_W);
        ui_text(x + (WP_THUMB_W - nw) / 2, y + WP_THUMB_H + 8, name, st, sel ? theme->text : theme->text_muted);
    }
    fb_reset_clip();
    if (g.max_scroll > 0) {             /* thin overlay scroll indicator */
        int track = g.bottom - g.top - 8;
        int bar = MAX(24, track * track / (track + g.max_scroll));
        int by = g.top + 4 + (track - bar) * state->wp_scroll / g.max_scroll;
        draw_rounded_rect_blend(main_x + main_w - 9, by, 5, bar, 2, theme->text_muted, 110);
    }
}

static bool wallpaper_page_click(SettingsStateUi *state, int main_x, int main_w, int content_y, int content_h,
                                 int px, int py)
{
    WpGrid g;
    wallpaper_grid(main_x, main_w, content_y, content_h, &g);
    if (py < g.top || py >= g.bottom) return false;
    for (int i = 0; i < wallpaper_choice_count(); i++) {
        int x, y;
        wallpaper_cell(&g, state->wp_scroll, i, &x, &y);
        if (px >= x && px < x + WP_THUMB_W && py >= y && py < y + WP_THUMB_H + 26) {
            wallpaper_choose(i);
            return true;
        }
    }
    return false;
}

static void draw_settings_row(int main_x, int main_w, int ry, int i, const char *label)
{
    if (i) draw_rounded_rect_blend(main_x + 16, ry - 4, main_w - 32, 1, 0, theme->text_muted, 50);
    ui_text_mid(main_x + 16, ry, 26, label, TEXT_BODY, theme->text);
}

static void draw_settings_window(SettingsStateUi *state, int content_x, int content_y, int content_w, int content_h)
{
    int sidebar_w = 160;
    bool dark = settings_get()->dark_mode;
    draw_rounded_rect_blend(content_x, content_y, sidebar_w, content_h, 12, dark ? RGB(18, 24, 36) : COLOR_WHITE, 70);

    const char *items[] = {
        "Appearance",
        "Users & Accounts",
        "Wallpaper",
        "Dock & Menu",
        "Keyboard",
        "Mouse",
        "Network",
        "About"
    };

    /* Coloured rounded-square glyph per page (macOS Settings style) */
    static const Color page_colors[8] = { RGB(52, 120, 246), RGB(88, 86, 214), RGB(48, 176, 199), RGB(110, 120, 135),
                                          RGB(142, 142, 147), RGB(255, 149, 0), RGB(0, 122, 255), RGB(100, 110, 125) };
    for (int i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        int item_y = content_y + 16 + i * 20;
        bool sel = state->page == (SettingsPage)i;
        if (sel) draw_rounded_rect_blend(content_x + 6, item_y - 2, sidebar_w - 12, 20, 6, theme->accent, 235);
        draw_rounded_rect_blend(content_x + 12, item_y + 1, 14, 14, 4, page_colors[i % 8], 255);
        draw_rounded_rect_blend(content_x + 16, item_y + 5, 6, 6, 3, COLOR_WHITE, 220);
        ui_text_mid(content_x + 32, item_y - 2, 20, items[i], TEXT_BODY, sel ? COLOR_WHITE : theme->text);
    }

    int main_x = content_x + sidebar_w + 10;
    int main_w = content_w - sidebar_w - 10;
    draw_rounded_rect_blend(main_x, content_y, main_w, content_h, 12, dark ? RGB(18, 24, 36) : COLOR_WHITE, dark ? 50 : 120);

    SettingsState *settings = settings_get();

    if (state->page == SETTINGS_PAGE_APPEARANCE) {
        ui_text(main_x + 16, content_y + 12, "Appearance", TEXT_HEADLINE, theme->text);
        static const char *const appearances[3] = { "Light", "Dark", "Auto" };
        static const char *const glasses[2] = { "Clear", "Tinted" };
        static const char *const menubars[2] = { "Glass", "Solid" };
        static const char *const corner_opts[2] = { "Standard", "Round" };
        static const char *const term_labels[4] = { "Tahoe", "Light", "Ocean", "Green" };
        static const char *const row_labels[6] = {
            "Appearance", "Accent", "Liquid Glass", "Menu Bar", "Window Corners", "Terminal Theme"
        };
        static const char *const switch_labels[3] = { "Reduce Transparency", "Reduce Motion", "24-Hour Clock" };
        for (int i = 0; i < APPEARANCE_ROWS; i++) {
            int ry = content_y + APPEARANCE_ROW_Y(i);
            if (i) draw_rounded_rect_blend(main_x + 16, ry - 4, main_w - 32, 1, 0, theme->text_muted, 50);
            const char *label = i <= 5 ? row_labels[i] : switch_labels[i - 6];
            ui_text(main_x + 16, ry + 5, label, TEXT_BODY, theme->text);
            if (i == 1) {
                int ax = accent_row_x(main_x, main_w);
                for (int a = 0; a < theme_accent_count(); a++) {
                    draw_rounded_rect_blend(ax + a * ACCENT_PITCH, ry + 3, ACCENT_SWATCH, ACCENT_SWATCH,
                                            ACCENT_SWATCH / 2, theme_accent_swatch(a), 255);
                    if (a == settings->accent) {
                        gfx_round_rect_stroke(surface(), ax + a * ACCENT_PITCH - 2, ry + 1,
                                              ACCENT_SWATCH + 4, ACCENT_SWATCH + 4,
                                              ACCENT_SWATCH / 2 + 2, theme->text, 210);
                    }
                }
            } else if (i <= 5) {
                const char *const *opts = i == 0 ? appearances : i == 2 ? glasses :
                                         i == 3 ? menubars : i == 4 ? corner_opts : term_labels;
                int n = i == 0 ? 3 : i == 5 ? 4 : 2;
                int sel = i == 0 ? settings->appearance : i == 2 ? settings->glass_tint :
                          i == 3 ? settings->menubar : i == 4 ? settings->corners : termtheme_index();
                draw_segmented(seg_x(main_x, main_w), ry + 1, opts, n, sel);
            } else {
                bool value = i == 6 ? settings->reduced_transparency :
                             i == 7 ? settings->reduced_motion : settings->time_24h;
                draw_switch(main_x + main_w - 58, ry + 3, value);
            }
        }
    } else if (state->page == SETTINGS_PAGE_USERS) {
        ui_text(main_x + 16, content_y + 10, "Users & Credentials", TEXT_HEADLINE, theme->text);

        /* Active user card */
        draw_rounded_rect_blend(main_x + 14, content_y + 34, main_w - 28, 52, 10, theme->glass_aqua, 120);
        draw_rounded_rect_blend(main_x + 22, content_y + 42, 36, 36, 10, theme->accent, 230);
        char initials[3] = { 'U', '\0', '\0' };
        const char *fn = users_current_full_name();
        if (fn && fn[0]) initials[0] = (fn[0] >= 'a' && fn[0] <= 'z') ? (char)(fn[0] - 32) : fn[0];
        ui_text(main_x + 36, content_y + 54, initials, TEXT_BODY, COLOR_WHITE);

        char uline[96];
        uline[0] = '\0';
        str_append(uline, sizeof(uline), fn);
        str_append(uline, sizeof(uline), " (@");
        str_append(uline, sizeof(uline), users_current_username());
        str_append(uline, sizeof(uline), ")");
        ui_text(main_x + 68, content_y + 44, uline, TEXT_BODY, theme->text);
        ui_text(main_x + 68, content_y + 62, users_current_home(), TEXT_BODY, theme->text_muted);

        /* Password change form */
        ui_text(main_x + 16, content_y + 98, "Change Password (SHA-256):", TEXT_BODY, theme->text);

        ui_text(main_x + 16, content_y + 122, "Current:", TEXT_BODY, theme->text_muted);
        draw_rounded_rect_blend(main_x + 92, content_y + 118, 150, 20, 6,
                                state->user_field == 1 ? theme->accent_soft : theme->glass_aqua, 170);
        char masked[32];
        mask_password_str(state->old_pw, masked, sizeof(masked));
        ui_text(main_x + 98, content_y + 122, masked[0] ? masked : (state->user_field == 1 ? "|" : "click to type"), TEXT_BODY, masked[0] ? theme->text : theme->text_muted);

        ui_text(main_x + 16, content_y + 148, "New PW:", TEXT_BODY, theme->text_muted);
        draw_rounded_rect_blend(main_x + 92, content_y + 144, 150, 20, 6,
                                state->user_field == 2 ? theme->accent_soft : theme->glass_aqua, 170);
        mask_password_str(state->new_pw, masked, sizeof(masked));
        ui_text(main_x + 98, content_y + 148, masked[0] ? masked : (state->user_field == 2 ? "|" : "click to type"), TEXT_BODY, masked[0] ? theme->text : theme->text_muted);

        draw_push_button(main_x + 250, content_y + 130, 86, 24, "Save PW", true);

        if (state->msg[0]) {
            ui_text(main_x + 16, content_y + 172, state->msg, TEXT_BODY, state->msg_ok ? theme->accent : RGB(220, 72, 68));
        }

        /* Accounts list */
        ui_text(main_x + 16, content_y + 194, "Registered Accounts:", TEXT_BODY, theme->text_muted);
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
            ui_text(main_x + 24, content_y + 212 + ui * 16, aline, TEXT_BODY, theme->text);
        }

        /* Bottom action buttons: Add Account, Lock Screen, Switch User */
        int by = content_y + content_h - 36;
        draw_push_button(main_x + 16, by, 112, 24, "+ Add Account", true);
        draw_push_button(main_x + 136, by, 96, 24, "Lock Screen", false);
        draw_push_button(main_x + 240, by, 98, 24, "Switch User", false);
    } else if (state->page == SETTINGS_PAGE_WALLPAPER) {
        draw_wallpaper_page(state, main_x, main_w, content_y, content_h);
    } else if (state->page == SETTINGS_PAGE_DOCK) {
        ui_text(main_x + 16, content_y + 12, "Dock & Menu Bar", TEXT_HEADLINE, theme->text);
        static const char *const sizes[3] = { "Small", "Medium", "Large" };
        static const char *const mags[3] = { "Off", "Some", "More" };
        int ry0 = content_y + APPEARANCE_ROW_Y(0), ry1 = content_y + APPEARANCE_ROW_Y(1);
        draw_settings_row(main_x, main_w, ry0, 0, "Size");
        draw_segmented(seg_x(main_x, main_w), ry0 + 1, sizes, 3, dock_size_index());
        draw_settings_row(main_x, main_w, ry1, 1, "Magnification");
        draw_segmented(seg_x(main_x, main_w), ry1 + 1, mags, 3, dock_magnify_level());
        ui_text(main_x + 16, ry1 + 40, "Point at the Dock to see it magnify.",
                TEXT_SMALL, theme->text_muted);
    } else if (state->page == SETTINGS_PAGE_KEYBOARD) {
        ui_text(main_x + 16, content_y + 12, "Keyboard", TEXT_HEADLINE, theme->text);
        int ry = content_y + APPEARANCE_ROW_Y(0);
        draw_settings_row(main_x, main_w, ry, 0, "Keyboard shortcuts");
        draw_switch(main_x + main_w - 58, ry + 3, settings->shortcuts_enabled);
        static const char *const keys[][2] = {
            { "Spotlight", "\xE2\x8C\x98 Space" }, { "Switch apps", "\xE2\x8C\x98 Tab" },
            { "Mission Control", "\xE2\x8C\x98 M" }, { "Launchpad", "\xE2\x8C\x98 L" },
            { "Close window", "\xE2\x8C\x98 Q" }, { "Lock screen", "\xE2\x8C\x83 \xE2\x8C\x98 Q" },
            { "Save (TextEdit)", "\xE2\x8C\x98 S" }, { "Console", "\xE2\x87\xA7 Esc" },
        };
        int ky = ry + 44;
        ui_text(main_x + 16, ky - 4, "Shortcuts", TEXT_SMALL_BOLD, theme->text_muted);
        for (unsigned i = 0; i < ARRAY_SIZE(keys); i++) {
            int col = (int)(i % 2), rowi = (int)(i / 2);
            int cw = (main_w - 32) / 2, cx = main_x + 16 + col * cw, cy = ky + 14 + rowi * 24;
            ui_text(cx, cy, keys[i][0], TEXT_BODY, settings->shortcuts_enabled ? theme->text : theme->text_muted);
            int kw = text_width(keys[i][1], TEXT_BODY_MEDIUM);
            draw_rounded_rect_blend(cx + cw - kw - 24, cy - 2, kw + 12, 20, 5, settings_get()->dark_mode ? COLOR_WHITE : theme->midnight, 20);
            ui_text(cx + cw - kw - 18, cy, keys[i][1], TEXT_BODY_MEDIUM, theme->text_muted);
        }
    } else if (state->page == SETTINGS_PAGE_MOUSE) {
        ui_text(main_x + 16, content_y + 12, "Mouse & Trackpad", TEXT_HEADLINE, theme->text);
        static const char *const speeds[4] = { "Slow", "Normal", "Fast", "Fastest" };
        int ry = content_y + APPEARANCE_ROW_Y(0);
        draw_settings_row(main_x, main_w, ry, 0, "Tracking speed");
        draw_segmented(seg_x(main_x, main_w), ry + 1, speeds, 4, MAX(1, MIN(settings->mouse_speed, 4)) - 1);
        ui_text(main_x + 16, ry + 40, "Applies to relative mice (Intel PS/2). In a VM the pointer follows", TEXT_SMALL, theme->text_muted);
        ui_text(main_x + 16, ry + 56, "the Mac cursor exactly, so speed is set on the Mac.", TEXT_SMALL, theme->text_muted);
    } else if (state->page == SETTINGS_PAGE_NETWORK) {
        const NetInfo *ni = net_info();
        char line[80], a[20], b[20];
        ui_text(main_x + 16, content_y + 12, "Network", TEXT_HEADLINE, theme->text);
        int ry = content_y + APPEARANCE_ROW_Y(0);
        ui_text(main_x + 16, ry + 5, "Wi-Fi", TEXT_BODY, theme->text);
        ui_text(main_x + 72, ry + 5, network_status_text(), TEXT_BODY, theme->text_muted);
        if (ni->present) draw_switch(main_x + main_w - 58, ry + 3, ni->enabled);
        int ly = ry + 34;
        if (ni->present) {
            net_format_mac(ni->mac, a, sizeof(a));
            line[0] = '\0'; str_append(line, sizeof(line), "Adapter   "); str_append(line, sizeof(line), ni->driver);
            str_append(line, sizeof(line), "  "); str_append(line, sizeof(line), a);
            ui_text(main_x + 16, ly, line, TEXT_BODY, theme->text_muted); ly += 18;
            if (ni->configured) {
                net_format_ip(ni->ip, a, sizeof(a)); net_format_ip(ni->netmask, b, sizeof(b));
                line[0] = '\0'; str_append(line, sizeof(line), "Address   "); str_append(line, sizeof(line), a);
                str_append(line, sizeof(line), " / "); str_append(line, sizeof(line), b);
                ui_text(main_x + 16, ly, line, TEXT_BODY, theme->text); ly += 18;
                net_format_ip(ni->gateway, a, sizeof(a)); net_format_ip(ni->dns, b, sizeof(b));
                line[0] = '\0'; str_append(line, sizeof(line), "Router    "); str_append(line, sizeof(line), a);
                str_append(line, sizeof(line), "   DNS "); str_append(line, sizeof(line), b);
                ui_text(main_x + 16, ly, line, TEXT_BODY, theme->text); ly += 18;
            }
            char n1[24], n2[24];
            utoa(ni->rx_bytes / 1024, n1, 10); utoa(ni->tx_bytes / 1024, n2, 10);
            line[0] = '\0'; str_append(line, sizeof(line), "Traffic   received "); str_append(line, sizeof(line), n1);
            str_append(line, sizeof(line), " KB, sent "); str_append(line, sizeof(line), n2); str_append(line, sizeof(line), " KB");
            ui_text(main_x + 16, ly, line, TEXT_BODY, theme->text_muted); ly += 26;
            draw_rounded_rect_blend(main_x + 16, ly, 120, 22, 8, theme->accent_soft, ni->enabled ? 220 : 100);
            ui_text(main_x + 28, ly + 3, "Renew Lease", TEXT_BODY, theme->text);
            ly += 36;
        } else {
            ui_text(main_x + 16, ly, "No network adapter was found.", TEXT_BODY, theme->text_muted); ly += 18;
            ui_text(main_x + 16, ly, "Start QEMU with virtio-net (run-qemu.sh does).", TEXT_BODY, theme->text_muted);
            ly += 36;
        }
        ui_text(main_x + 16, ly, "Bluetooth", TEXT_BODY, theme->text);
        ui_text(main_x + 112, ly, "No Bluetooth hardware", TEXT_BODY, theme->text_muted);
        ly += 30;
        ui_text(main_x + 16, ly, "In a VM the connection is a virtual", TEXT_BODY, theme->text_muted);
        ui_text(main_x + 16, ly + 16, "Ethernet adapter, shown here as Wi-Fi.", TEXT_BODY, theme->text_muted);
    } else if (state->page == SETTINGS_PAGE_ABOUT) {
        ui_text(main_x + 16, content_y + 12, "About", TEXT_HEADLINE, theme->text);
        ui_text(main_x + 16, content_y + 42, "ojjyOS v3", TEXT_BODY, theme->text_muted);
        ui_text(main_x + 16, content_y + 60, "Created by Jonas Lee", TEXT_BODY, theme->text_muted);
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

/* TextEdit and Notes: monospace text on a "paper" sheet (the editor works on
 * a character grid), a document header, visible caret and a status bar.
 * Notes gets a yellow notepad header and ruled lines. */
#define EDIT_LINE_H  18
static void draw_textedit_window(TextEditState *edit, int content_x, int content_y, int content_w, int content_h, bool notes)
{
    bool dark = settings_get()->dark_mode;
    Color paper = dark ? RGB(24, 30, 42) : (notes ? RGB(255, 252, 238) : COLOR_WHITE);
    Color ink = dark ? theme->text : RGB(30, 34, 42);

    /* Header: icon, document name, edited pill */
    int hy = content_y, hh = 30;
    if (notes) gfx_round_rect_vgradient(surface(), content_x, hy, content_w, hh, 10,
                                        dark ? RGB(120, 96, 30) : RGB(255, 222, 89), dark ? RGB(96, 76, 22) : RGB(248, 196, 40), 255);
    draw_icon_id(notes ? ICON_NOTES : ICON_TEXTEDIT, content_x + 10, hy + 7, 16, 255);
    const char *name = edit->file_path[0] ? vfs_basename(edit->file_path) : (notes ? "Quick Note" : "Untitled");
    char note_title[NOTES_NAME_MAX];
    if (notes && edit->file_path[0]) {          /* Notes shows titles, not file names */
        notes_title(name, note_title, sizeof(note_title));
        name = note_title;
    }
    int nw = text_draw_fit(surface(), content_x + 32, text_y_center(hy, hh, TEXT_BODY_BOLD), name, TEXT_BODY_BOLD,
                           notes && !dark ? RGB(70, 50, 10) : theme->text, content_w - 140);
    const char *pill = edit->read_only ? "Read-only" : edit->dirty ? "Edited" : NULL;
    if (pill) {
        int pw = text_width(pill, TEXT_SMALL_BOLD) + 14;
        draw_rounded_rect_blend(content_x + 40 + nw, hy + 7, pw, 16, 8,
                                edit->read_only ? theme->text_muted : theme->accent, 220);
        ui_text_mid(content_x + 47 + nw, hy + 7, 16, pill, TEXT_SMALL_BOLD, COLOR_WHITE);
    }

    /* Paper */
    int py = content_y + hh + 4, ph = content_h - hh - 4 - 24;
    gfx_shadow(surface(), content_x, py, content_w, ph, 8, 8, 2, COLOR_BLACK, dark ? 60 : 30);
    draw_rounded_rect_blend(content_x, py, content_w, ph, 8, paper, 250);

    /* Soft wrap: the document is laid out into rows of at most `cols`
     * columns, broken at spaces. The compositor remembers the view size
     * so the cursor keys step over the same rows the eye sees. */
    int cols = (content_w - 28) / FONT_WIDTH;
    if (cols < TEXTEDIT_WRAP_MIN_COLS) cols = TEXTEDIT_WRAP_MIN_COLS;
    edit->view_cols = cols;
    int row_count;
    const TextEditWrapRow *rows = textedit_wrap_rows(edit, cols, &row_count);
    int cursor_row = textedit_cursor_row(edit, rows, row_count);

    int visible = (ph - 16) / EDIT_LINE_H;
    if (visible < 1) visible = 1;
    edit->view_rows = visible;
    int max_scroll = row_count - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (edit->scroll > max_scroll) edit->scroll = max_scroll;
    if (edit->scroll < 0) edit->scroll = 0;
    /* Keep the cursor row in view while typing */
    if (cursor_row < edit->scroll) edit->scroll = cursor_row;
    if (cursor_row >= edit->scroll + visible) {
        edit->scroll = cursor_row - visible + 1;
    }

    int tx = content_x + 14, line_y = py + 8;
    fb_set_clip(content_x + 4, py + 2, content_w - 8, ph - 4);
    if (notes)          /* ruled paper across the whole page */
        for (int r = 0; r < visible; r++)
            draw_rounded_rect_blend(content_x + 8, line_y + r * EDIT_LINE_H + EDIT_LINE_H - 1, content_w - 16, 1, 0,
                                    dark ? COLOR_WHITE : RGB(200, 190, 150), dark ? 20 : 120);
    char row_text[TEXTEDIT_LINE_MAX + 1];
    for (int r = edit->scroll; r < row_count && r < edit->scroll + visible; r++) {
        const TextEditWrapRow *row = &rows[r];
        if (r == cursor_row && !edit->sel_active)
            draw_rounded_rect_blend(content_x + 6, line_y - 1, content_w - 12, EDIT_LINE_H, 4,
                                    theme->accent, dark ? 30 : 18);
        if (edit->sel_active && edit->sel_line == row->line) {
            textedit_normalize_selection(edit);
            /* The selection is a column range on one logical line; show
             * the part of it that falls inside this visual row. */
            int start = MAX(edit->sel_start, row->start);
            int end = MIN(edit->sel_end, row->start + row->len);
            if (start < end) {
                draw_rounded_rect_blend(tx + (start - row->start) * FONT_WIDTH - 1, line_y, (end - start) * FONT_WIDTH + 2, EDIT_LINE_H - 2,
                                        3, theme->accent, 90);
            }
        }
        int len = row->len;
        if (len > TEXTEDIT_LINE_MAX) len = TEXTEDIT_LINE_MAX;
        if (len > 0) memcpy(row_text, edit->lines[row->line] + row->start, (size_t)len);
        row_text[len] = '\0';
        fb_draw_string(tx, line_y + 1, row_text, ink, paper);
        if (r == cursor_row)
            draw_rounded_rect_blend(tx + (edit->cursor_col - row->start) * FONT_WIDTH, line_y, 2, EDIT_LINE_H - 2, 1, theme->accent, 255);
        line_y += EDIT_LINE_H;
    }
    fb_reset_clip();

    /* Status bar: editor status, cursor position, word and character
     * count (D6). Falls back to just the counts when the full line would
     * not fit the paper. */
    int words, chars;
    textedit_counts(edit, &words, &chars);
    char status[128];
    char num[8];
    status[0] = '\0';
    str_append(status, sizeof(status), edit->status);
    str_append(status, sizeof(status), "   Line ");
    utoa(edit->cursor_line + 1, num, 10);
    str_append(status, sizeof(status), num);
    str_append(status, sizeof(status), ", Column ");
    utoa(edit->cursor_col + 1, num, 10);
    str_append(status, sizeof(status), num);
    str_append(status, sizeof(status), "   ");
    utoa(words, num, 10);
    str_append(status, sizeof(status), num);
    str_append(status, sizeof(status), " words, ");
    utoa(chars, num, 10);
    str_append(status, sizeof(status), num);
    str_append(status, sizeof(status), " chars");
    if (text_width(status, TEXT_SMALL) > content_w - 24) {
        status[0] = '\0';
        utoa(words, num, 10);
        str_append(status, sizeof(status), num);
        str_append(status, sizeof(status), " words, ");
        utoa(chars, num, 10);
        str_append(status, sizeof(status), num);
        str_append(status, sizeof(status), " chars");
    }
    ui_text_mid(content_x + 4, content_y + content_h - 22, 22, status, TEXT_SMALL, theme->text_muted);
}

/* Notes: a sidebar of notes (title + first line) left of the editor.
 * notes_click() uses the same geometry. */
#define NOTES_SIDEBAR_W  180
#define NOTES_BUTTON_Y   32     /* New Note button, below content_y */
#define NOTES_BUTTON_H   24
#define NOTES_LIST_Y     66     /* first row, below content_y */
#define NOTES_ROW_H      40
#define NOTES_FOOTER_H   22     /* "More notes in Finder" */

static int notes_sidebar_w(int content_w)
{
    return MIN(NOTES_SIDEBAR_W, content_w / 3);
}

static int notes_visible_rows(int content_h)
{
    int rows = (content_h - NOTES_LIST_Y - (notes_lib.more ? NOTES_FOOTER_H : 6)) / NOTES_ROW_H;
    return MAX(rows, 0);
}

static void draw_notes_window(TextEditState *edit, int content_x, int content_y, int content_w, int content_h)
{
    bool dark = settings_get()->dark_mode;
    Color panel = dark ? RGB(18, 24, 36) : COLOR_WHITE;
    int sw = notes_sidebar_w(content_w);
    draw_rounded_rect_blend(content_x, content_y, sw, content_h, 12, panel, 70);

    ui_text(content_x + 12, content_y + 9, "Notes", TEXT_BODY_BOLD, theme->text);
    char count[8];
    utoa((uint64_t)notes_lib.count, count, 10);
    ui_text(content_x + sw - 12 - text_width(count, TEXT_SMALL), content_y + 11, count, TEXT_SMALL, theme->text_muted);
    draw_push_button(content_x + 8, content_y + NOTES_BUTTON_Y, sw - 16, NOTES_BUTTON_H, "+ New Note", true);

    int visible = notes_visible_rows(content_h);
    if (notes_follow_selection && notes_lib.selected >= 0 && visible > 0) {
        if (notes_lib.selected < notes_lib.scroll) notes_lib.scroll = notes_lib.selected;
        if (notes_lib.selected >= notes_lib.scroll + visible) notes_lib.scroll = notes_lib.selected - visible + 1;
    }
    notes_follow_selection = false;
    notes_lib.scroll = MAX(0, MIN(notes_lib.scroll, notes_lib.count - visible));

    int top = content_y + NOTES_LIST_Y;
    if (notes_lib.count == 0 && notes_lib.dir[0]) {
        const char *empty = "No notes yet";
        ui_text(content_x + (sw - text_width(empty, TEXT_BODY)) / 2, top + 14, empty, TEXT_BODY, theme->text_muted);
    }
    Color pick = dark ? RGB(150, 112, 24) : RGB(255, 214, 10);     /* Notes yellow */
    fb_set_clip(content_x, top, sw, visible * NOTES_ROW_H);
    for (int r = 0; r < visible && notes_lib.scroll + r < notes_lib.count; r++) {
        int i = notes_lib.scroll + r;
        int ry = top + r * NOTES_ROW_H;
        if (i == notes_lib.selected)
            draw_rounded_rect_blend(content_x + 6, ry + 1, sw - 12, NOTES_ROW_H - 2, 8, pick, dark ? 150 : 120);
        else if (r > 0 && i - 1 != notes_lib.selected)        /* hairline between plain rows */
            draw_rounded_rect_blend(content_x + 14, ry, sw - 28, 1, 0, dark ? COLOR_WHITE : theme->midnight, dark ? 22 : 18);
        char title[NOTES_NAME_MAX];
        notes_title(notes_lib.items[i].name, title, sizeof(title));
        text_draw_fit(surface(), content_x + 14, ry + 5, title, TEXT_BODY_BOLD, theme->text, sw - 28);
        const char *preview = notes_lib.items[i].preview[0] ? notes_lib.items[i].preview : "No additional text";
        text_draw_fit(surface(), content_x + 14, ry + 22, preview, TEXT_SMALL, theme->text_muted, sw - 28);
    }
    fb_reset_clip();
    if (notes_lib.more)
        ui_text(content_x + 12, content_y + content_h - NOTES_FOOTER_H + 4, "More notes in Finder", TEXT_SMALL, theme->text_muted);

    /* The open note, or an empty page while none is open. */
    int ex = content_x + sw + 10, ew = content_w - sw - 10;
    if (edit->file_path[0]) {
        draw_textedit_window(edit, ex, content_y, ew, content_h, true);
        return;
    }
    draw_rounded_rect_blend(ex, content_y, ew, content_h, 8, dark ? RGB(24, 30, 42) : RGB(255, 252, 238), 250);
    const char *hint = notes_lib.dir[0] ? "Click New Note or start typing" : edit->status;
    ui_text(ex + (ew - text_width(hint, TEXT_BODY)) / 2, content_y + content_h / 2 - 8, hint, TEXT_BODY, theme->text_muted);
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

    int header_h = CALENDAR_HEADER_H;
    int sidebar_w = 160;
    int agenda_w = 200;

    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 12, theme->dock_tint, 120);

    int header_x = content_x + sidebar_w + 10;
    int header_w = content_w - sidebar_w - agenda_w - 20;
    uint8_t header_alpha;
    Color header_fill = glass_fill(&header_alpha);
    draw_glass_control(header_x, content_y, header_w, header_h, 12, header_fill, header_alpha, false);

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
    ui_text(header_x + 10, content_y + 2, title, TEXT_HEADLINE, theme->text);

    /* Navigation on the right; view tabs under the title (they used to
     * collide in the ~300px header). Click geometry mirrors this. */
    int nav_y = content_y + 10;
    {
        uint8_t ga;
        Color gf = glass_fill(&ga);
        draw_glass_control(header_x + header_w - 98, nav_y, 44, 20, 10, gf, ga, true);
        draw_chevron(header_x + header_w - 87, nav_y + 10, -1, theme->text, 255);
        draw_chevron(header_x + header_w - 65, nav_y + 10, 1, theme->text, 255);
    }
    draw_push_button(header_x + header_w - 50, nav_y, 46, 20, "Today", true);

    int view_x = header_x + 6;
    const char *views[] = { "Month", "Week", "Day", "Agenda" };
    for (int i = 0; i < 4; i++) {
        int vx = view_x + i * 50;
        if (cal->view == (CalendarView)i) {
            draw_rounded_rect_blend(vx, content_y + header_h - 18, 48, 16, 8, theme->accent, 60);
        }
        ui_text(vx + 4, content_y + header_h - 16, views[i], TEXT_BODY, theme->text_muted);
    }

    draw_rounded_rect_blend(content_x, content_y, sidebar_w, content_h, 12, theme->dock_tint, 130);
    ui_text(content_x + 10, content_y + 10, "Calendars", TEXT_BODY, theme->text_muted);
    ui_text(content_x + 10, content_y + 30, "Local", TEXT_BODY, theme->text);
    ui_text(content_x + 10, content_y + 48, "Personal", TEXT_BODY, theme->text);
    ui_text(content_x + 10, content_y + 66, "Work", TEXT_BODY, theme->text);
    draw_rounded_rect_blend(content_x + 10, content_y + content_h - 36, sidebar_w - 20, 22, 10, theme->accent, 60);
    ui_text(content_x + 20, content_y + content_h - 32, "New Event", TEXT_BODY, theme->text);

    int grid_x = header_x;
    int grid_y = content_y + header_h + 6;
    int grid_w = header_w;
    int grid_h = content_h - header_h - 12;

    if (cal->view == CAL_VIEW_MONTH) {
        static const char *weekdays[] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
        int cell_w = grid_w / 7;
        int cell_h = (grid_h - 20) / 6;
        for (int i = 0; i < 7; i++) {
            ui_text(grid_x + i * cell_w + 6, grid_y, weekdays[i], TEXT_BODY, theme->text_muted);
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
                    bool today = app_date_is_today(cal->year, cal->month, day, &now);
                    if (today) draw_rounded_rect_blend(cx + 3, cy + 3, 22, 22, 11,
                                                      blend(theme->accent, theme->midnight, 95), 255);
                    char numstr[4];
                    utoa(day, numstr, 10);
                    int day_x = today ? cx + 14 - text_width(numstr, TEXT_BODY) / 2 : cx + 8;
                    ui_text(day_x, cy + 6, numstr, TEXT_BODY, today ? COLOR_WHITE : theme->text);

                    int indices[4];
                    int count = calendar_events_for_day(cal, cal->year, cal->month, day, indices, 4);
                    int chip_y = cy + 24;
                    int shown = count > 2 ? 2 : count;
                    for (int e = 0; e < shown; e++) {
                        CalendarEvent *ev = &cal->events[indices[e]];
                        draw_rounded_rect_blend(cx + 3, chip_y, cell_w - 6, 14, 6, theme->accent, 90);
                        text_draw_fit(surface(), cx + 6, text_y_center(chip_y, 14, TEXT_SMALL), ev->title, TEXT_SMALL, theme->text, cell_w - 12);
                        chip_y += 15;
                    }
                    if (count > 2) {
                        ui_text(cx + 10, chip_y + 2, "+", TEXT_BODY, theme->text_muted);
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
            ui_text(cx + 8, grid_y + 6, numstr, TEXT_BODY, theme->text);

            int indices[6];
            int count = calendar_events_for_day(cal, cal->year, cal->month, day, indices, 6);
            int ey = grid_y + 24;
            for (int e = 0; e < count && e < 4; e++) {
                CalendarEvent *ev = &cal->events[indices[e]];
                draw_rounded_rect_blend(cx + 3, ey, cell_w - 6, 14, 6, theme->accent, 90);
                text_draw_fit(surface(), cx + 6, text_y_center(ey, 14, TEXT_SMALL), ev->title, TEXT_SMALL, theme->text, cell_w - 12);
                ey += 15;
            }
        }
    } else if (cal->view == CAL_VIEW_DAY) {
        int indices[10];
        int count = calendar_events_for_day(cal, cal->year, cal->month, cal->selected_day, indices, 10);
        /* calendar_events_for_day returns the TOTAL match count; only the
         * first 10 indices are filled. Never iterate past the array. */
        int shown = MIN(count, 10);
        ui_text(grid_x + 10, grid_y + 6, "Day", TEXT_BODY, theme->text_muted);
        int ey = grid_y + 24;
        for (int i = 0; i < shown; i++) {
            CalendarEvent *ev = &cal->events[indices[i]];
            char timebuf[16];
            calendar_format_time(ev, timebuf, sizeof(timebuf));
            ui_text(grid_x + 10, ey, timebuf, TEXT_BODY, theme->text_muted);
            ui_text(grid_x + 80, ey, ev->title, TEXT_BODY, theme->text);
            ey += 18;
        }
    } else {
        int ey = grid_y + 10;
        for (int i = 0; i < cal->event_count && i < 10; i++) {
            CalendarEvent *ev = &cal->events[i];
            char timebuf[16];
            calendar_format_time(ev, timebuf, sizeof(timebuf));
            ui_text(grid_x + 8, ey, timebuf, TEXT_BODY, theme->text_muted);
            ui_text(grid_x + 72, ey, ev->title, TEXT_BODY, theme->text);
            ey += 18;
        }
    }

    int agenda_x = content_x + content_w - agenda_w;
    draw_rounded_rect_blend(agenda_x, content_y, agenda_w, content_h, 12, theme->dock_tint, 110);
    {
        static const char *short_months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        char head[32];
        head[0] = '\0';
        str_append(head, sizeof(head), "Agenda  ");
        str_append(head, sizeof(head), short_months[cal->month - 1]);
        str_append(head, sizeof(head), " ");
        char dn[4];
        utoa((uint64_t)cal->selected_day, dn, 10);
        str_append(head, sizeof(head), dn);
        ui_text(agenda_x + 10, content_y + 10, head, TEXT_BODY, theme->text_muted);
    }
    int indices[8];
    int count = calendar_events_for_day(cal, cal->year, cal->month, cal->selected_day, indices, 8);
    int ay = content_y + 32;
    if (count == 0) ui_text(agenda_x + 10, ay, "No events", TEXT_BODY, theme->text_muted);
    for (int i = 0; i < count && i < 6; i++) {
        /* Two lines per event: time, then the (clipped) title. */
        CalendarEvent *ev = &cal->events[indices[i]];
        char timebuf[16];
        calendar_format_time(ev, timebuf, sizeof(timebuf));
        if (indices[i] == cal->selected_event) {
            draw_rounded_rect_blend(agenda_x + 6, ay - 2, agenda_w - 12, 34, 8, theme->accent, 40);
        }
        ui_text(agenda_x + 12, ay, timebuf, TEXT_BODY, theme->text_muted);
        text_draw_fit(surface(), agenda_x + 12, ay + 15, ev->title, TEXT_BODY_BOLD, theme->text, agenda_w - 24);
        ay += 38;
    }

    if (cal->edit_mode && cal->selected_event >= 0) {
        draw_rounded_rect_blend(agenda_x + 8, content_y + content_h - 40, agenda_w - 16, 26, 8,
                                theme->dock_tint, 140);
        const char *label = "Title";
        if (cal->edit_field == 1) label = "Time";
        if (cal->edit_field == 2) label = "Location";
        if (cal->edit_field == 3) label = "Notes";
        ui_text(agenda_x + 12, content_y + content_h - 36, label, TEXT_BODY, theme->text_muted);
        ui_text(agenda_x + 70, content_y + content_h - 36, cal->edit_buffer[0] ? cal->edit_buffer : "", TEXT_BODY, theme->text);
        if (cal->edit_error) {
            ui_text(agenda_x + 12, content_y + content_h - 20, "Invalid time", TEXT_BODY, theme->text_muted);
        }
    }
}

/* Preview: a desktop picture viewer - full-resolution image, previous /
 * next, and "Set as Desktop Picture". */
typedef struct { int ix, iy, iw, ih, bar_y, prev_x, next_x, set_x, set_w; } PreviewLayout;

static void preview_layout(int cx, int cy, int cw, int ch, PreviewLayout *l)
{
    int bar_h = 40;
    int aw = cw - 24, ah = ch - bar_h - 20;
    l->iw = aw;
    l->ih = aw * 10 / 16;
    if (l->ih > ah) { l->ih = ah; l->iw = ah * 16 / 10; }
    l->ix = cx + (cw - l->iw) / 2;
    l->iy = cy + 12 + (ah - l->ih) / 2;
    l->bar_y = cy + ch - bar_h + 6;
    l->prev_x = cx + 12;
    l->next_x = cx + 48;
    l->set_w = 168;
    l->set_x = cx + cw - 12 - l->set_w;
}

static void draw_preview_window(PreviewState *preview, int content_x, int content_y, int content_w, int content_h)
{
    static uint32_t *pv_src;
    static int pv_index = -1, pv_w, pv_h;
    bool dark = settings_get()->dark_mode;
    int count = wallpaper_choice_count();
    if (preview->index < 0 || preview->index >= count) preview->index = 0;
    PreviewLayout l;
    preview_layout(content_x, content_y, content_w, content_h, &l);
    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 10, dark ? RGB(18, 24, 36) : COLOR_WHITE, dark ? 70 : 90);

    int k = fb_ui_scale();
    gfx_shadow(surface(), l.ix, l.iy, l.iw, l.ih, 10, 12, 4, COLOR_BLACK, 80);
    if (preview->index < WP_BUILTIN) {
        /* The waves are drawn for the viewer's exact size. */
        if (pv_index != preview->index || pv_w != l.iw * k || pv_h != l.ih * k) {
            pv_w = l.iw * k;
            pv_h = l.ih * k;
            static size_t cap;
            if ((size_t)pv_w * pv_h > cap) {
                cap = (size_t)2880 * 1800;
                if ((size_t)pv_w * pv_h > cap) cap = (size_t)pv_w * pv_h;
                pv_src = (uint32_t *)pmm_alloc_bytes(cap * sizeof(uint32_t));
            }
            if (pv_src) wallpaper_render_tahoe(pv_src, pv_w, pv_h, preview->index == 1);
            pv_index = preview->index;
        }
    } else if (pv_index != preview->index) {
        pv_index = wallpaper_decode(preview->index, &pv_src, &pv_w, &pv_h) ? preview->index : -2;
    }
    if (pv_src && pv_index == preview->index)
        gfx_blit_scaled(surface(), pv_src, pv_w, 0, 0, pv_w, pv_h, l.ix, l.iy, l.iw, l.ih, 10, 255);
    else
        draw_rounded_rect_blend(l.ix, l.iy, l.iw, l.ih, 10, theme->text_muted, 60);

    uint8_t ga;
    Color gf = glass_fill(&ga);
    draw_glass_control(l.prev_x, l.bar_y, 30, 26, 13, gf, ga, true);
    draw_glass_control(l.next_x, l.bar_y, 30, 26, 13, gf, ga, true);
    draw_chevron(l.prev_x + 15, l.bar_y + 13, -1, theme->text, 255);
    draw_chevron(l.next_x + 15, l.bar_y + 13, 1, theme->text, 255);
    const char *name = wallpaper_choice_name(preview->index);
    ui_text_mid(l.next_x + 44, l.bar_y, 26, name, TEXT_BODY_MEDIUM, theme->text);
    bool current = preview->index == wp_choice;
    draw_push_button(l.set_x, l.bar_y, l.set_w, 26,
                     current ? "Desktop Picture \xE2\x9C\x93" : "Set as Desktop Picture", !current);
}

static void preview_click(PreviewState *preview, int cx, int cy, int cw, int ch, int x, int y)
{
    PreviewLayout l;
    preview_layout(cx, cy, cw, ch, &l);
    int count = wallpaper_choice_count();
    if (y < l.bar_y || y >= l.bar_y + 26) return;
    if (x >= l.prev_x && x < l.prev_x + 30) preview->index = (preview->index + count - 1) % count;
    else if (x >= l.next_x && x < l.next_x + 30) preview->index = (preview->index + 1) % count;
    else if (x >= l.set_x && x < l.set_x + l.set_w) wallpaper_choose(preview->index);
}

/* Shared helpers for app modules in their own files (ui/draw.h). */
const ThemeTokens *ui_theme(void) { return theme; }
void ui_rounded_rect(int x, int y, int w, int h, int r, Color color, uint8_t alpha)
{
    draw_rounded_rect_blend(x, y, w, h, r, color, alpha);
}
Color ui_blend(Color bg, Color fg, uint8_t alpha) { return blend(bg, fg, alpha); }
void ui_mark_dirty(void) { mark_dirty(); }
void ui_glass_control(int x, int y, int w, int h, int r, bool raised)
{
    uint8_t a;
    Color f = glass_fill(&a);
    draw_glass_control(x, y, w, h, r, f, a, raised);
}
void ui_push_button(int x, int y, int w, int h, const char *label, bool primary)
{
    draw_push_button(x, y, w, h, label, primary);
}

/* ================================================================== */
/* About window                                                        */
/* ================================================================== */

static void draw_about_window(int content_x, int content_y, int content_w, int content_h)
{
    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 10, theme->dock_tint, 110);

    /* App icon on the right, title block on the left */
    draw_icon_id(ICON_ABOUT, content_x + content_w - 124, content_y + 4, 100, 255);

    int cx = content_x + 24;
    ui_text(cx, content_y + 8, "ojjyOS", TEXT_LARGE, theme->text);
    ui_text(cx, content_y + 50, "Created by Jonas Lee", TEXT_BODY_MEDIUM, theme->text);
    ui_glass_control(cx, content_y + 92, content_w - 48, 36, 10, false);
    ui_text_mid(cx + 12, content_y + 92, 36, "Version 3.1.0  \xE2\x80\xA2  Tahoe", TEXT_BODY_BOLD, theme->text);

    char buf[96];

    ui_text(cx, content_y + 144, "A custom operating system with a", TEXT_BODY, theme->text);
    ui_text(cx, content_y + 162, "macOS Tahoe-inspired user interface.", TEXT_BODY, theme->text);
    ui_text(cx, content_y + 188, "Built from scratch - not based on", TEXT_BODY, theme->text_muted);
    ui_text(cx, content_y + 206, "Linux, BSD, Windows or macOS.", TEXT_BODY, theme->text_muted);

    utoa(fb_get_width(), buf, 10);
    str_append(buf, sizeof(buf), " x ");
    char num[16];
    utoa(fb_get_height(), num, 10);
    str_append(buf, sizeof(buf), num);
    str_append(buf, sizeof(buf), " display");
    text_draw_fit(surface(), cx, content_y + 238, buf, TEXT_BODY, theme->text_muted, content_w - 48);

    buf[0] = '\0';
    utoa(pmm_get_total_memory() / (1024 * 1024), num, 10);
    str_append(buf, sizeof(buf), num);
    str_append(buf, sizeof(buf), " MB memory");
    text_draw_fit(surface(), cx, content_y + 258, buf, TEXT_BODY, theme->text_muted, content_w - 48);
}

/* ================================================================== */
/* Calculator app                                                      */
/* ================================================================== */

static void calculator_input(CalculatorState *calc, int key)
{
    if (calculator_press(calc, key, timer_get_ticks()))
        serial_printf("[CALC] key=%d display='%s' history='%s'\n", key, calc->display, calc->history);
}

static void draw_calculator_window(CalculatorState *calc, int content_x, int content_y, int content_w, int content_h)
{
    draw_rounded_rect_blend(content_x, content_y, content_w, content_h, 10, theme->dock_tint, 120);

    /* LCD readout */
    CalculatorLayout layout;
    if (!calculator_layout(content_w, content_h, &layout)) return;
    int lcd_x = content_x + layout.lcd_x;
    int lcd_y = content_y + layout.lcd_y;
    int lcd_w = layout.lcd_w;
    int lcd_h = layout.lcd_h;
    gfx_round_rect_vgradient(surface(), lcd_x, lcd_y, lcd_w, lcd_h, 10, RGB(36, 42, 56), RGB(20, 24, 34), 245);
    text_draw_fit(surface(), lcd_x + 12, lcd_y + 8, calc->history, TEXT_SMALL, RGB(188, 209, 238), lcd_w - 24);
    /* Big right-aligned readout; long numbers step down to a smaller face */
    TextStyle ds = text_width(calc->display, TEXT_LARGE) <= lcd_w - 44 ? TEXT_LARGE : TEXT_HEADLINE;
    int dw = MIN(text_width(calc->display, ds), lcd_w - 44);
    text_draw_fit(surface(), lcd_x + lcd_w - 14 - dw, text_y_center(lcd_y + 24, lcd_h - 24, ds), calc->display, ds, COLOR_WHITE, lcd_w - 44);

    static const char *labels[5][4] = {
        { "C",  "+/-", "%",  "/" },
        { "7",  "8",   "9",  "*" },
        { "4",  "5",   "6",  "-" },
        { "1",  "2",   "3",  "+" },
        { "0",  ".",   "BS", "=" }
    };

    int grid_y = content_y + layout.grid_y;
    int cell_w = layout.key_w;
    int cell_h = layout.key_h;

    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            int bx = lcd_x + c * (cell_w + 6);
            int by = grid_y + r * (cell_h + 6);
            const char *lbl = labels[r][c];
            static const char *const pretty[5][4] = {
                { "C", "\xC2\xB1", "%", "\xC3\xB7" }, { "7", "8", "9", "\xC3\x97" },
                { "4", "5", "6", "\xE2\x88\x92" }, { "1", "2", "3", "+" }, { "0", ".", "\xE2\x8C\xAB", "=" } };
            if (c == 3)
                gfx_round_rect_vgradient(surface(), bx, by, cell_w, cell_h, 9, RGB(255, 176, 64), RGB(245, 140, 20), 255);
            else if (r == 0)
                draw_rounded_rect_blend(bx, by, cell_w, cell_h, 9, settings_get()->dark_mode ? RGB(96, 104, 118) : RGB(196, 206, 218), 235);
            else
                draw_rounded_rect_blend(bx, by, cell_w, cell_h, 9, settings_get()->dark_mode ? RGB(60, 66, 80) : COLOR_WHITE, 220);
            if (calc->pressed_key == r * 4 + c)
                draw_rounded_rect_blend(bx, by, cell_w, cell_h, 9, c == 3 ? COLOR_WHITE : theme->accent, c == 3 ? 90 : 110);
            const char *shown = pretty[r][c];
            TextStyle ls = TEXT_TITLE;
            if (text_width(shown, ls) > cell_w - 8) shown = lbl, ls = TEXT_BODY_BOLD;
            int lx = bx + (cell_w - text_width(shown, ls)) / 2;
            ui_text_mid(lx, by, cell_h, shown, ls, (c == 3) ? RGB(52, 32, 9) : theme->text);
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
    ui_text(cx, content_y + 10, "ojjyOS Activity Monitor", TEXT_BODY, theme->text);

#if defined(__aarch64__)
    const char *arch_str = "Arch: ARM64 (EL1) on QEMU virt";      /* TCG runner, not HVF */
#else
    const char *arch_str = "Arch: x86_64 Long Mode";
#endif
    ui_text(cx, content_y + 28, arch_str, TEXT_BODY, theme->accent);

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
    ui_text(cx, content_y + 46, up_line, TEXT_BODY, theme->text_muted);

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
    ui_text(cx, content_y + 66, mem_line, TEXT_BODY, theme->text);

    int bar_x = cx + 64, bar_w = content_w - 28 - 64;
    ui_text(cx, content_y + 84, "Memory", TEXT_BODY, theme->text_muted);
    draw_rounded_rect_blend(bar_x, content_y + 86, bar_w, 12, 6, theme->glass_aqua, 140);
    int fill_w = (int)((used_mb * (uint64_t)bar_w) / total_mb);
    if (fill_w < 12) fill_w = 12;
    if (fill_w > bar_w) fill_w = bar_w;
    draw_rounded_rect_blend(bar_x, content_y + 86, fill_w, 12, 6, theme->accent, 220);

    /* Network */
    {
        const NetInfo *ni = net_info();
        char net_line[112], ip[16];
        net_line[0] = '\0';
        str_append(net_line, sizeof(net_line), "Network: ");
        if (!ni->present) str_append(net_line, sizeof(net_line), "no adapter");
        else if (!ni->enabled) str_append(net_line, sizeof(net_line), "off");
        else if (!ni->configured) str_append(net_line, sizeof(net_line), "connecting");
        else { net_format_ip(ni->ip, ip, sizeof(ip)); str_append(net_line, sizeof(net_line), ip); }
        str_append(net_line, sizeof(net_line), "   in ");
        utoa(ni->rx_bytes / 1024, num, 10);
        str_append(net_line, sizeof(net_line), num);
        str_append(net_line, sizeof(net_line), " KB   out ");
        utoa(ni->tx_bytes / 1024, num, 10);
        str_append(net_line, sizeof(net_line), num);
        str_append(net_line, sizeof(net_line), " KB");
        ui_text(cx, content_y + 104, net_line, TEXT_BODY, theme->text);
    }

    /* Kernel & User Process Table */
    ui_text(cx, content_y + 126, "Processes", TEXT_BODY, theme->text);
    ui_text(cx + 4, content_y + 144, "PID", TEXT_BODY, theme->text_muted);
    ui_text(cx + 48, content_y + 144, "Process", TEXT_BODY, theme->text_muted);
    ui_text(cx + 188, content_y + 144, "User", TEXT_BODY, theme->text_muted);
    ui_text(cx + 268, content_y + 144, "State", TEXT_BODY, theme->text_muted);
    ui_text(cx + 348, content_y + 144, "Ticks", TEXT_BODY, theme->text_muted);
    ui_text(cx + 416, content_y + 144, "Mem KB", TEXT_BODY, theme->text_muted);

    int pcount = proc_count();
    int max_rows = (content_h - 190) / 16;
    if (max_rows > 10) max_rows = 10;
    if (max_rows < 3) max_rows = 3;
    for (int i = 0; i < pcount && i < max_rows; i++) {
        const ProcEntry *pe = proc_get_by_index(i);
        if (!pe) continue;
        int ry = content_y + 162 + i * 16;
        utoa(pe->pid, num, 10);
        ui_text(cx + 4, ry, num, TEXT_BODY, theme->text_muted);
        ui_text(cx + 48, ry, pe->name, TEXT_BODY, theme->text);
        ui_text(cx + 188, ry, pe->owner, TEXT_BODY, theme->text_muted);
        ui_text(cx + 268, ry, proc_state_name(pe->state), TEXT_BODY, (pe->state == PROC_RUNNING) ? theme->accent : theme->text_muted);
        utoa(pe->cpu_ticks, num, 10);
        ui_text(cx + 348, ry, num, TEXT_BODY, theme->text_muted);
        utoa(pe->memory_bytes / 1024, num, 10);
        ui_text(cx + 416, ry, num, TEXT_BODY, theme->text_muted);
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
    ui_text(cx, content_y + content_h - 20, task_line, TEXT_BODY, theme->text_muted);
}

static void draw_window(int idx)
{
    CompositorWindow *win = &windows[idx];
    AppWindowState *state = &app_states[idx];
    int r = settings_get()->corners ? 16 : theme->glass.corner_radius[win->corner_level];
    int blur_px = theme->glass.blur_px[win->blur_level];
    uint8_t opacity = theme->glass.opacity[win->glass_level];
    uint8_t highlight = theme->glass.highlight[win->glass_level];

    /* Animations scale/fade a layer copy (draw_window_animated); the window
     * itself always renders at its real geometry. */
    int draw_w = win->w;
    int draw_h = win->h;
    int draw_x = win->x;
    int draw_y = win->y;

    bool focused = idx == window_count - 1;
    bool dark = settings_get()->dark_mode;
    if (!draw_window_bare) {
        if (focused) gfx_shadow(surface(), draw_x, draw_y, draw_w, draw_h, r, 34, 14, theme->shadow, 105);
        else gfx_shadow(surface(), draw_x, draw_y, draw_w, draw_h, r, 20, 6, theme->shadow, 60);
    }

    if (settings_get()->reduced_transparency) {
        Color solid = blend(theme->dock_tint, theme->glass_aqua, 215);
        draw_rounded_rect_blend(draw_x, draw_y, draw_w, draw_h, r, solid, 255);
    } else {
        if (wp_cache_valid && win->glass_level == 1) {
            gfx_round_rect_image(surface(), draw_x, draw_y, draw_w, draw_h, r, wp_glass, 255);
        } else if (wp_cache_valid) {
            gfx_round_rect_image(surface(), draw_x, draw_y, draw_w, draw_h, r, wp_blur, 255);
            draw_rounded_rect_blend(draw_x, draw_y, draw_w, draw_h, r, theme->glass_aqua, opacity);
        } else {
            draw_rounded_rect_blend(draw_x, draw_y, draw_w, draw_h, r, theme->glass_aqua, 230);
        }
        (void)blur_px;
    }

    /* Title bar: a soft sheen instead of a solid band, then the title.
     * LG3 keeps the title area clearer than the old solid band. */
    gfx_round_rect_vgradient(surface(), draw_x, draw_y, draw_w, 38, r,
                             COLOR_WHITE, theme->glass_aqua, dark ? 10 : 42);

    /* LG3: Liquid Glass rim - the specular band of the floating chrome,
     * drawn over the cached-glass body and the title sheen so the edge
     * stays crisp. Hairline always; rim only with transparency on. */
    gfx_round_rect_stroke(surface(), draw_x, draw_y, draw_w, draw_h, r,
                          dark ? COLOR_WHITE : theme->midnight, dark ? 26 : 16);
    if (!settings_get()->reduced_transparency)
        gfx_round_rect_rim(surface(), draw_x, draw_y, draw_w, draw_h, r, dark ? 120 : 170);
    (void)highlight;
    if (state->type != APP_WEB) {
        const char *title = win->title;
        Color tc = focused ? theme->text : theme->text_muted;
        int ty = text_y_center(draw_y, 36, TEXT_BODY_BOLD);
        if (state->type == APP_FINDER) {
            text_draw(surface(), draw_x + TITLE_X, ty, title, TEXT_BODY_BOLD, tc);
        } else {
            int avail = draw_w - 2 * (TITLE_X + 8);
            int tw = MIN(text_width(title, TEXT_BODY_BOLD), avail);
            text_draw_fit(surface(), draw_x + (draw_w - tw) / 2, ty, title, TEXT_BODY_BOLD, tc, avail);
        }
    }

    /* Traffic lights: colour + fine rim when focused, grey otherwise;
     * hovering the group shows the close/minimise/zoom symbols. */
    {
        static const Color lights[3] = { RGB(255, 95, 87), RGB(254, 188, 46), RGB(40, 200, 64) };
        static const Color rims[3] = { RGB(224, 68, 62), RGB(222, 161, 35), RGB(29, 173, 44) };
        int gx = draw_x + TL_X(0), gy = draw_y + TL_TOP;
        bool hover = cursor_x >= gx - 4 && cursor_x < draw_x + TL_X(2) + TL_SIZE + 4 &&
                     cursor_y >= gy - 4 && cursor_y < gy + TL_SIZE + 4;
        for (int i = 0; i < 3; i++) {
            int lx = draw_x + TL_X(i);
            Color fill = focused || hover ? lights[i] : (settings_get()->dark_mode ? RGB(78, 84, 96) : RGB(200, 206, 214));
            Color rim = focused || hover ? rims[i] : (settings_get()->dark_mode ? RGB(64, 70, 82) : RGB(178, 186, 196));
            draw_rounded_rect_blend(lx, gy, TL_SIZE, TL_SIZE, TL_SIZE / 2, rim, 255);
            draw_rounded_rect_blend(lx + 1, gy + 1, TL_SIZE - 2, TL_SIZE - 2, TL_SIZE / 2 - 1, fill, 255);
            if (!hover) continue;
            Color g = RGB(90, 30, 20);
            int cx = lx + TL_SIZE / 2, cy = gy + TL_SIZE / 2;
            if (i == 0) {       /* x */
                for (int k = -3; k <= 3; k++) {
                    draw_rounded_rect_blend(cx + k - 1, cy + k - 1, 2, 2, 1, g, 200);
                    draw_rounded_rect_blend(cx + k - 1, cy - k - 1, 2, 2, 1, g, 200);
                }
            } else if (i == 1) { /* - */
                draw_rounded_rect_blend(cx - 4, cy - 1, 8, 2, 1, g, 210);
            } else {             /* + (zoom) */
                draw_rounded_rect_blend(cx - 4, cy - 1, 8, 2, 1, g, 210);
                draw_rounded_rect_blend(cx - 1, cy - 4, 2, 8, 1, g, 210);
            }
        }
    }

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
            draw_textedit_window(&state->textedit, content_x, content_y, content_w, content_h, false);
            break;
        case APP_NOTES:
            draw_notes_window(&state->notes, content_x, content_y, content_w, content_h);
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
            /* the browser owns the whole window: tabs live in the title bar */
            browser_draw(draw_x, draw_y, draw_w, draw_h);
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
        ui_text(panel_x + 20, panel_y + 16, "Glass Panel", TEXT_BODY, theme->text);
        ui_text(panel_x + 20, panel_y + 36, "Tahoe material demo", TEXT_BODY, theme->text_muted);

        int dot_y = panel_y + 62;
        for (int i = 0; i < 3; i++) {
            int dot_x = panel_x + 20 + i * 18;
            fb_fill_rect(dot_x, dot_y, 10, 10, theme->accent);
        }
    }

    /* Hairline edge (light catches the glass) */
    gfx_round_rect_stroke(surface(), draw_x, draw_y, draw_w, draw_h, r, COLOR_WHITE,
                          settings_get()->dark_mode ? 36 : 120);
}

/* ------------------------------------------------------------------ */
/* Animation layers                                                    */
/* Opening windows render into an off-screen layer that is scaled and  */
/* faded in. Closing / minimising windows leave a "ghost" snapshot that */
/* shrinks and fades (or flies into the Dock) while the real window is  */
/* already gone, so app state never waits on an animation.             */
/* ------------------------------------------------------------------ */
typedef enum { GHOST_NONE, GHOST_CLOSE, GHOST_MINIMIZE, GHOST_SCREEN } GhostKind;
static struct {
    GhostKind kind;
    uint64_t start;
    int dur;
    int x, y, w, h;          /* logical rect the snapshot came from */
    int tx, ty, tw, th;      /* minimise target (Dock icon) */
    int pw, ph;              /* stored physical size */
    int radius;
} ghost;
static uint32_t *layer_buf, *ghost_buf;
static uint64_t anim_keepalive_until;

static bool layers_ready(void)
{
    size_t px = (size_t)fb_physical_width() * fb_physical_height();
    if (!px) return false;
    if (!layer_buf) layer_buf = (uint32_t *)pmm_alloc_bytes(px * sizeof(uint32_t));
    if (!ghost_buf) ghost_buf = (uint32_t *)pmm_alloc_bytes(px * sizeof(uint32_t));
    return layer_buf && ghost_buf;
}

/* Ease-in-out cubic, 0..1000 */
static int ease_in_out(int t)
{
    if (t <= 0) return 0;
    if (t >= 1000) return 1000;
    if (t < 500) return (int)((int64_t)4 * t * t * t / 1000000);
    int u = 2000 - 2 * t;
    return 1000 - (int)((int64_t)u * u * u / 2000000);
}

static bool window_on_screen(const CompositorWindow *w)
{
    return w->x >= 0 && w->y >= 0 && w->x + w->w <= (int)comp_width && w->y + w->h <= (int)comp_height;
}

/* Draw window idx (no shadow) into layer_buf over a glass-coloured base. */
static void render_window_to_layer(int idx)
{
    CompositorWindow *win = &windows[idx];
    int k = fb_ui_scale(), pw = (int)fb_physical_width();
    for (int y = win->y * k; y < (win->y + win->h) * k; y++) {
        uint32_t *dst = layer_buf + (size_t)y * pw;
        const uint32_t *src = wp_cache_valid ? wp_glass + (size_t)y * pw : NULL;
        for (int x = win->x * k; x < (win->x + win->w) * k; x++) dst[x] = src ? src[x] : theme->dock_tint;
    }
    fb_redirect(layer_buf);
    draw_window_bare = true;
    draw_window(idx);
    draw_window_bare = false;
    fb_redirect(NULL);
}

static void ghost_capture(int idx, GhostKind kind)
{
    if (idx < 0 || idx >= window_count || settings_get()->reduced_motion) return;
    CompositorWindow *win = &windows[idx];
    if (win->minimized || !window_on_screen(win) || !layers_ready()) return;
    render_window_to_layer(idx);
    int k = fb_ui_scale(), pw = (int)fb_physical_width();
    ghost.pw = win->w * k;
    ghost.ph = win->h * k;
    for (int y = 0; y < ghost.ph; y++)
        memcpy(ghost_buf + (size_t)y * ghost.pw, layer_buf + (size_t)(win->y * k + y) * pw + win->x * k,
               (size_t)ghost.pw * sizeof(uint32_t));
    ghost.x = win->x; ghost.y = win->y; ghost.w = win->w; ghost.h = win->h;
    ghost.radius = theme->glass.corner_radius[win->corner_level];
    ghost.kind = kind;
    ghost.start = timer_get_ticks();
    ghost.dur = kind == GHOST_MINIMIZE ? 340 : 190;
    ghost.tw = ghost.th = 48;
    ghost.tx = (int)comp_width / 2 - 24;
    ghost.ty = (int)comp_height - 60;
    if (kind == GHOST_MINIMIZE) {
        DockLayout L;
        dock_layout(&L);
        int a = app_registry_find_by_name(app_name_from_type(app_states[idx].type));
        if (a >= 0 && a < L.count) {
            ghost.tx = L.x[a]; ghost.ty = L.bottom - L.size[a];
            ghost.tw = ghost.th = L.size[a];
        }
    }
    mark_dirty();
}

/* Snapshot the whole current frame (login -> desktop cross-fade). */
static void ghost_capture_screen(void)
{
    if (settings_get()->reduced_motion || !layers_ready() || !fb_backbuffer()) return;
    ghost.pw = (int)fb_physical_width();
    ghost.ph = (int)fb_physical_height();
    memcpy(ghost_buf, fb_backbuffer(), (size_t)ghost.pw * ghost.ph * sizeof(uint32_t));
    ghost.x = ghost.y = 0;
    ghost.w = (int)comp_width; ghost.h = (int)comp_height;
    ghost.radius = 0;
    ghost.kind = GHOST_SCREEN;
    ghost.start = timer_get_ticks();
    ghost.dur = 420;
}

static void draw_ghost(GhostKind which, uint64_t now)
{
    if (ghost.kind != which) return;
    int t = (int)MIN((uint64_t)1000, (now - ghost.start) * 1000 / (uint64_t)MAX(1, ghost.dur));
    if (t >= 1000) { ghost.kind = GHOST_NONE; mark_dirty(); return; }
    int x = ghost.x, y = ghost.y, w = ghost.w, h = ghost.h, r = ghost.radius;
    uint8_t alpha = 255;
    if (which == GHOST_CLOSE) {
        int e = ease_out(t);
        int sc = 1000 - 70 * e / 1000;
        w = ghost.w * sc / 1000; h = ghost.h * sc / 1000;
        x = ghost.x + (ghost.w - w) / 2; y = ghost.y + (ghost.h - h) / 2;
        alpha = (uint8_t)(255 * (1000 - e) / 1000);
    } else if (which == GHOST_MINIMIZE) {
        int e = ease_in_out(t);
        x = ghost.x + (ghost.tx - ghost.x) * e / 1000;
        y = ghost.y + (ghost.ty - ghost.y) * e / 1000;
        w = ghost.w + (ghost.tw - ghost.w) * e / 1000;
        h = ghost.h + (ghost.th - ghost.h) * e / 1000;
        r = r + (12 - r) * e / 1000;
        alpha = (uint8_t)(255 - 170 * e / 1000);
    } else {
        alpha = (uint8_t)(255 * (1000 - ease_out(t)) / 1000);
    }
    gfx_blit_scaled(surface(), ghost_buf, ghost.pw, 0, 0, ghost.pw, ghost.ph, x, y, MAX(1, w), MAX(1, h), r, alpha);
}

/* Opening windows: scale 94% -> 100%, rise 8 px, fade in (layer blit). */
static void draw_window_animated(int idx)
{
    CompositorWindow *win = &windows[idx];
    if (win->anim_open >= 1000 || settings_get()->reduced_motion || !window_on_screen(win) || !layers_ready()) {
        draw_window(idx);
        return;
    }
    int e = ease_out(win->anim_open);
    int sc = 940 + 60 * e / 1000;
    int dw = win->w * sc / 1000, dh = win->h * sc / 1000;
    int dx = win->x + (win->w - dw) / 2, dy = win->y + (win->h - dh) / 2 + 8 * (1000 - e) / 1000;
    uint8_t alpha = (uint8_t)(255 * MIN(1000, e * 4 / 3) / 1000);
    int r = settings_get()->corners ? 16 : theme->glass.corner_radius[win->corner_level];
    render_window_to_layer(idx);
    gfx_shadow(surface(), dx, dy, dw, dh, r, 34, 14, theme->shadow, (uint8_t)(105 * alpha / 255));
    int k = fb_ui_scale();
    gfx_blit_scaled(surface(), layer_buf, (int)fb_physical_width(), win->x * k, win->y * k, win->w * k, win->h * k,
                    dx, dy, dw, dh, r, alpha);
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

/* Menu bar layout, shared by drawing and hit-testing (Inter text widths).
 * Left: ojjy logo, bold app name, File/Edit/View/Window. Right (from the
 * edge): clock, Control Center glyph, user name, Wi-Fi glyph. */
typedef struct {
    int logo_x, logo_w, app_x;
    int title_x[4], title_w[4];
    int wifi_x, user_x, user_w, cc_x, clock_x, clock_w;
} MenuBarLayout;

static const char *const menu_titles[4] = { "File", "Edit", "View", "Window" };

static void menu_clock_text(char *out, size_t size)
{
    static const char *const days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    RtcTime t;
    rtc_read_time(&t);
    bool is_24 = settings_get()->time_24h;
    int hour = t.hour;
    char num[8];
    out[0] = '\0';
    int wd = weekday_of_date(t.year, t.month, t.day);
    str_append(out, size, days[(wd % 7 + 7) % 7]);
    str_append(out, size, " ");
    str_append(out, size, months[(t.month >= 1 && t.month <= 12) ? t.month - 1 : 0]);
    str_append(out, size, " ");
    utoa(t.day, num, 10); str_append(out, size, num);
    str_append(out, size, "   ");
    if (!is_24) { hour %= 12; if (hour == 0) hour = 12; }
    else if (hour < 10) str_append(out, size, "0");
    utoa(hour, num, 10); str_append(out, size, num);
    str_append(out, size, ":");
    if (t.minute < 10) str_append(out, size, "0");
    utoa(t.minute, num, 10); str_append(out, size, num);
    if (!is_24) str_append(out, size, t.hour >= 12 ? " PM" : " AM");
}

static void menu_bar_layout(MenuBarLayout *L)
{
    L->logo_x = 14;
    L->logo_w = text_width("ojjy", TEXT_BODY_BOLD);
    L->app_x = L->logo_x + L->logo_w + 20;
    int x = L->app_x + text_width(active_app_name, TEXT_BODY_BOLD) + 20;
    for (int i = 0; i < 4; i++) {
        L->title_x[i] = x;
        L->title_w[i] = text_width(menu_titles[i], TEXT_BODY);
        x += L->title_w[i] + 20;
    }
    char clock[40];
    menu_clock_text(clock, sizeof(clock));
    L->clock_w = text_width(clock, TEXT_BODY_MEDIUM);
    L->clock_x = (int)comp_width - 14 - L->clock_w;
    L->cc_x = L->clock_x - 18 - 18;
    L->user_w = text_width(users_current_username(), TEXT_BODY_MEDIUM);
    L->user_x = L->cc_x - 16 - L->user_w;
    L->wifi_x = L->user_x - 16 - 16;
}

/* Kept for dropdown placement: x of menu title i (0..3). */
static int menu_title_x(int i)
{
    MenuBarLayout L;
    menu_bar_layout(&L);
    return L.title_x[i < 0 ? 0 : i > 3 ? 3 : i];
}

/* Control Center glyph: two switch pills. */
static void draw_cc_glyph(int x, int y, Color c)
{
    draw_rounded_rect_blend(x, y + 2, 16, 6, 3, c, 255);
    draw_rounded_rect_blend(x + 10, y + 3, 4, 4, 2, theme->dock_tint, 255);
    draw_rounded_rect_blend(x, y + 10, 16, 6, 3, c, 150);
    draw_rounded_rect_blend(x + 2, y + 11, 4, 4, 2, theme->dock_tint, 255);
}

static void draw_menu_bar(void)
{
    MenuBarLayout L;
    menu_bar_layout(&L);
    int H = MENU_BAR_HEIGHT;
    if (!settings_get()->reduced_transparency && !settings_get()->menubar) {
        /* Tahoe: an almost clear glass strip, no refraction or rim */
        ui_glass(0, 0, (int)comp_width, H, 0, 150);
    } else {
        draw_rounded_rect_blend(0, 0, (int)comp_width, H, 0, theme->dock_tint, 245);
    }
    draw_rounded_rect_blend(0, H - 1, (int)comp_width, 1, 0, COLOR_BLACK, 22);

    /* ojjy logo */
    if (active_menu == 1)
        draw_rounded_rect_blend(L.logo_x - 7, 3, L.logo_w + 14, H - 6, 5, theme->accent, 220);
    ui_text_mid(L.logo_x, 0, H, "ojjy", TEXT_BODY_BOLD,
                active_menu == 1 ? COLOR_WHITE : settings_get()->dark_mode ? theme->accent : theme->text);
    ui_text_mid(L.app_x, 0, H, active_app_name, TEXT_BODY_BOLD, theme->text);

    for (int i = 0; i < 4; i++) {
        bool on = active_menu == i + 2;
        if (on) draw_rounded_rect_blend(L.title_x[i] - 8, 3, L.title_w[i] + 16, H - 6, 5, theme->accent, 220);
        ui_text_mid(L.title_x[i], 0, H, menu_titles[i], TEXT_BODY, on ? COLOR_WHITE : theme->text);
    }

    if (net_info()->present) {
        const NetInfo *ni = net_info();
        bool connected = ni->enabled && ni->configured;
        draw_wifi_glyph(L.wifi_x, 6, connected ? theme->text : theme->text_muted, !ni->enabled);
    }
    ui_text_mid(L.user_x, 0, H, users_current_username(), TEXT_BODY_MEDIUM, theme->text);
    draw_cc_glyph(L.cc_x, 4, theme->text);
    char clock[40];
    menu_clock_text(clock, sizeof(clock));
    ui_text_mid(L.clock_x, 0, H, clock, TEXT_BODY_MEDIUM, theme->text);
}

/* Dropdown menus. Items are "Label" or "Label\tShortcut". */
#define MENU_ROW_H 22
static int menu_items(int menu, const char **items, int *x)
{
    int count = 0;
    *x = 6;
    if (menu == 1) {
        items[count++] = "About ojjyOS";
        items[count++] = "System Settings\xE2\x80\xA6";
        items[count++] = "Activity Monitor";
        items[count++] = "Lock Screen\t\xE2\x8C\x83\xE2\x8C\x98Q";
        items[count++] = "Switch User / Log Out\xE2\x80\xA6";
        items[count++] = "Restart\xE2\x80\xA6";
        items[count++] = "Shut Down\xE2\x80\xA6";
    } else if (menu == 2) {
        *x = menu_title_x(0) - 8;
        items[count++] = "New Finder Window";
        items[count++] = "New Text Document";
        items[count++] = "Open Home Folder";
        items[count++] = "Save Document\t\xE2\x8C\x98S";
        items[count++] = "Close Window\t\xE2\x8C\x98Q";
    } else if (menu == 3) {
        *x = menu_title_x(1) - 8;
        items[count++] = "Copy\t\xE2\x8C\x98" "C";
        items[count++] = "Cut\t\xE2\x8C\x98X";
        items[count++] = "Paste\t\xE2\x8C\x98V";
        items[count++] = "Clear Selection";
    } else if (menu == 4) {
        *x = menu_title_x(2) - 8;
        items[count++] = "Toggle Dark / Light";
        items[count++] = "Toggle Transparency";
        items[count++] = "Mission Control\t\xE2\x8C\x98M";
        items[count++] = "Launchpad\t\xE2\x8C\x98L";
    } else if (menu == 5) {
        *x = menu_title_x(3) - 8;
        items[count++] = "Minimize Window";
        items[count++] = "Zoom / Maximize";
        items[count++] = "Calculator";
        items[count++] = "Activity Monitor";
    }
    return count;
}

/* Split "Label\tShortcut"; returns label length in bytes. */
static int menu_label_len(const char *item, const char **shortcut)
{
    int n = 0;
    while (item[n] && item[n] != '\t') n++;
    *shortcut = item[n] == '\t' ? item + n + 1 : NULL;
    return n;
}

static int menu_panel_width(const char **items, int count)
{
    int w = 0;
    for (int i = 0; i < count; i++) {
        const char *sc;
        int n = menu_label_len(items[i], &sc);
        int iw = text_width_n(items[i], n, TEXT_BODY) + (sc ? text_width(sc, TEXT_BODY) + 36 : 0);
        if (iw > w) w = iw;
    }
    return MAX(200, w + 40);
}

static int menu_open_key;            /* 0 = no menu shown (reset in the tick) */
static void draw_menu_panel(int x, int y, const char **items, int count, int w, int key)
{
    /* Fade + 6 px slide over 140 ms whenever a different menu opens. */
    int open_key = menu_open_key;
    static uint64_t open_at;
    uint64_t now = timer_get_ticks();
    if (key != open_key) { menu_open_key = key; open_at = now; }
    int t = (int)MIN((uint64_t)1000, (now - open_at) * 1000 / 140);
    int e = settings_get()->reduced_motion ? 1000 : ease_out(t);
    if (e < 1000) anim_keepalive_until = now + 30;
    uint8_t fade = (uint8_t)(255 * e / 1000);
    y -= 6 * (1000 - e) / 1000;
    int h = count * MENU_ROW_H + 12;
    gfx_shadow(surface(), x, y, w, h, 10, 22, 8, theme->shadow, (uint8_t)(90 * fade / 255));
    ui_glass_fade(x, y, w, h, 10, 205, fade);
    for (int i = 0; i < count; i++) {
        int iy = y + 6 + i * MENU_ROW_H;
        bool hot = cursor_x >= x + 4 && cursor_x <= x + w - 4 && cursor_y >= iy && cursor_y < iy + MENU_ROW_H;
        if (hot) draw_rounded_rect_blend(x + 5, iy, w - 10, MENU_ROW_H, 6, theme->accent, (uint8_t)(235 * fade / 255));
        const char *sc;
        int n = menu_label_len(items[i], &sc);
        Color fg = hot ? COLOR_WHITE : theme->text;
        text_draw_n(surface(), x + 14, text_y_center(iy, MENU_ROW_H, TEXT_BODY), items[i], n, TEXT_BODY, fg, fade);
        if (sc) {
            int sw = text_width(sc, TEXT_BODY);
            text_draw_n(surface(), x + w - 14 - sw, text_y_center(iy, MENU_ROW_H, TEXT_BODY), sc, -1, TEXT_BODY,
                        hot ? COLOR_WHITE : theme->text_muted, fade);
        }
    }
}

static void draw_menu_dropdown(void)
{
    if (active_menu <= 0) return;
    const char *items[8];
    int x;
    int count = menu_items(active_menu, items, &x);
    draw_menu_panel(x, MENU_BAR_HEIGHT + 4, items, count, menu_panel_width(items, count), active_menu);
}

#define CTX_ITEMS 8
static const char *const ctx_items[CTX_ITEMS] = {
    "New Folder on Desktop",
    "New Text Note",
    "Open Home in Finder",
    "Open Terminal Here",
    "Open Calculator",
    "Change Wallpaper\xE2\x80\xA6",
    "Toggle Dark / Light Mode",
    "Lock Screen"
};

static void ctx_menu_rect(int *x, int *y, int *w, int *h)
{
    *w = menu_panel_width((const char **)ctx_items, CTX_ITEMS);
    *h = CTX_ITEMS * MENU_ROW_H + 12;
    *x = ctx_menu_x;
    *y = ctx_menu_y;
    if (*x + *w > (int)comp_width - 8) *x = (int)comp_width - *w - 8;
    if (*y + *h > (int)comp_height - 8) *y = (int)comp_height - *h - 8;
}

static void draw_context_menu(void)
{
    if (!ctx_menu_open) return;
    int x, y, w, h;
    ctx_menu_rect(&x, &y, &w, &h);
    draw_menu_panel(x, y, (const char **)ctx_items, CTX_ITEMS, w, 100 + ctx_menu_x * 7 + ctx_menu_y);
}

static bool login_screen_visible(void)
{
    return (!users_is_authenticated()) || login_create_mode || (users_count() == 0);
}

/* Text field: frosted fill, focus ring, value or placeholder, caret. */
static void draw_text_field(int x, int y, int w, int h, const char *value, const char *placeholder,
                            bool focused, TextStyle st)
{
    draw_rounded_rect_blend(x, y, w, h, 7, settings_get()->dark_mode ? RGB(20, 26, 38) : COLOR_WHITE,
                            focused ? 235 : 190);
    if (focused) {
        gfx_round_rect_stroke(surface(), x - 1, y - 1, w + 2, h + 2, 8, theme->accent, 255);
        gfx_round_rect_stroke(surface(), x - 2, y - 2, w + 4, h + 4, 9, theme->accent, 90);
    } else {
        gfx_round_rect_stroke(surface(), x, y, w, h, 7, COLOR_BLACK, 26);
    }
    int ty = text_y_center(y, h, st);
    int tw = 0;
    if (value && value[0]) tw = text_draw_fit(surface(), x + 10, ty, value, st, theme->text, w - 24);
    else if (placeholder && !focused) text_draw_fit(surface(), x + 10, ty, placeholder, st, theme->text_muted, w - 20);
    if (focused) draw_rounded_rect_blend(x + 10 + tw + 1, y + (h - 16) / 2, 2, 16, 1, theme->accent, 255);
}

static void draw_login_screen(void)
{
    /* Frosted, slightly darkened wallpaper behind the lock / setup card */
    if (wp_cache_valid && !settings_get()->reduced_transparency)
        gfx_round_rect_image(surface(), 0, 0, (int)comp_width, (int)comp_height, 0, wp_blur, 255);
    draw_rounded_rect_blend(0, 0, (int)comp_width, (int)comp_height, 0, theme->midnight, 70);

    /* Date + large clock (Tahoe lock screen) */
    {
        static const char *const days[7] = { "Sunday", "Monday", "Tuesday", "Wednesday",
                                             "Thursday", "Friday", "Saturday" };
        static const char *const months[12] = { "January", "February", "March", "April", "May", "June",
                                                "July", "August", "September", "October", "November", "December" };
        RtcTime now;
        rtc_read_time(&now);
        char date[48], num[8];
        date[0] = '\0';
        str_append(date, sizeof(date), days[(weekday_of_date(now.year, now.month, now.day) % 7 + 7) % 7]);
        str_append(date, sizeof(date), ", ");
        str_append(date, sizeof(date), months[(now.month >= 1 && now.month <= 12) ? now.month - 1 : 0]);
        str_append(date, sizeof(date), " ");
        utoa(now.day, num, 10); str_append(date, sizeof(date), num);
        int dw = text_width(date, TEXT_TITLE);
        ui_text(((int)comp_width - dw) / 2, 46, date, TEXT_TITLE, COLOR_WHITE);

        char clock_str[16];
        int hour = now.hour;
        if (!settings_get()->time_24h) { hour %= 12; if (hour == 0) hour = 12; }
        clock_str[0] = '\0';
        if (settings_get()->time_24h && hour < 10) str_append(clock_str, sizeof(clock_str), "0");
        utoa(hour, num, 10); str_append(clock_str, sizeof(clock_str), num);
        str_append(clock_str, sizeof(clock_str), ":");
        if (now.minute < 10) str_append(clock_str, sizeof(clock_str), "0");
        utoa(now.minute, num, 10); str_append(clock_str, sizeof(clock_str), num);
        int cw = text_width(clock_str, TEXT_DISPLAY);
        text_draw_n(surface(), ((int)comp_width - cw) / 2, 66 + 2, clock_str, -1, TEXT_DISPLAY, COLOR_BLACK, 50);
        ui_text(((int)comp_width - cw) / 2, 66, clock_str, TEXT_DISPLAY, COLOR_WHITE);
    }

    {
        const char *brand = "ojjyOS Tahoe  \xE2\x80\xA2  Created by Jonas Lee";
        int bw = text_width(brand, TEXT_SMALL_BOLD);
        ui_text(((int)comp_width - bw) / 2, (int)comp_height - 30, brand, TEXT_SMALL_BOLD, COLOR_WHITE);
    }

    bool create_mode = login_create_mode || (users_count() == 0);

    if (create_mode) {
        /* Geometry (fields, buttons) matches the click handler. */
        int cw = 440;
        int ch = 340;
        int cx = ((int)comp_width - cw) / 2;
        int cy = ((int)comp_height - ch) / 2;

        draw_shadow(cx, cy, cw, ch, 22);
        ui_glass(cx, cy, cw, ch, 22, 215);

        const char *title = (users_count() == 0)
            ? "Welcome to ojjyOS"
            : "Create a New Account";
        int tw = text_width(title, TEXT_HEADLINE);
        ui_text(cx + (cw - tw) / 2, cy + 16, title, TEXT_HEADLINE, theme->text);
        const char *sub = (users_count() == 0) ? "Create your account to get started."
                                               : "Passwords are salted and SHA-256 hashed on disk.";
        int sw = text_width(sub, TEXT_SMALL);
        ui_text(cx + (cw - sw) / 2, cy + 44, sub, TEXT_SMALL, theme->text_muted);

        const char *labels[4] = { "Full Name", "Username", "Password", "Confirm" };
        const char *hints[4] = { "Jonas Lee", "jonas", "Required", "Repeat password" };
        for (int f = 0; f < 4; f++) {
            int fy = cy + 74 + f * 40;
            int lw = text_width(labels[f], TEXT_BODY_MEDIUM);
            ui_text_mid(cx + 120 - lw, fy, 26, labels[f], TEXT_BODY_MEDIUM, theme->text);

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
            draw_text_field(cx + 132, fy, cw - 160, 26, val, hints[f], login_active_field == f, TEXT_BODY);
        }

        const char *status = login_status[0] ? login_status : "Tab moves between fields \xE2\x80\xA2 Return creates the account";
        Color sc = login_status[0] ? (login_status_ok ? theme->accent : RGB(225, 76, 70)) : theme->text_muted;
        int stw = text_width(status, TEXT_SMALL);
        ui_text(cx + (cw - stw) / 2, cy + 244, status, TEXT_SMALL, sc);

        int btn_y = cy + ch - 46;
        int btn_w = (users_count() > 0) ? 230 : (cw - 56);
        draw_push_button(cx + 28, btn_y, btn_w, 30, "Create Account", true);
        if (users_count() > 0) {
            int cancel_x = cx + 28 + btn_w + 12;
            int cancel_w = cw - 56 - btn_w - 12;
            draw_push_button(cancel_x, btn_y, cancel_w, 30, "Cancel", false);
        }
    } else {
        int cw = 360;
        int ch = 280;
        int cx = ((int)comp_width - cw) / 2;
        int cy = ((int)comp_height - ch) / 2;

        draw_shadow(cx, cy, cw, ch, 22);
        ui_glass(cx, cy, cw, ch, 22, 215);

        if (login_selected_user < 0 || login_selected_user >= users_count()) {
            login_selected_user = 0;
        }
        const UserAccount *acc = users_get_by_index(login_selected_user);
        const char *fname = acc ? acc->full_name : "User";
        const char *uname = acc ? acc->username : "user";

        /* Round avatar with the first initial */
        int av = 60;
        int av_x = cx + (cw - av) / 2;
        int av_y = cy + 16;
        gfx_shadow(surface(), av_x, av_y, av, av, av / 2, 10, 3, theme->shadow, 70);
        gfx_round_rect_vgradient(surface(), av_x, av_y, av, av, av / 2,
                                 blend(theme->accent, COLOR_WHITE, 80), theme->accent, 255);
        char init[2] = { 'U', '\0' };
        if (fname[0]) init[0] = (fname[0] >= 'a' && fname[0] <= 'z') ? (char)(fname[0] - 32) : fname[0];
        int iw = text_width(init, TEXT_LARGE);
        ui_text_mid(av_x + (av - iw) / 2, av_y, av, init, TEXT_LARGE, COLOR_WHITE);

        int nw = text_width(fname, TEXT_TITLE);
        ui_text(cx + (cw - nw) / 2, cy + 84, fname, TEXT_TITLE, theme->text);
        char handle[48];
        handle[0] = '@';
        handle[1] = '\0';
        str_append(handle, sizeof(handle), uname);
        if (acc && acc->is_admin) str_append(handle, sizeof(handle), "  \xE2\x80\xA2  Admin");
        int hw = text_width(handle, TEXT_SMALL);
        ui_text(cx + (cw - hw) / 2, cy + 106, handle, TEXT_SMALL, theme->text_muted);

        /* Password box (same rect as the click handler) */
        int pw_x = cx + 36;
        int pw_y = cy + 132;
        int pw_w = cw - 72;
        char masked[40];
        mask_password_str(login_password, masked, sizeof(masked));
        draw_text_field(pw_x, pw_y, pw_w, 28, masked, "Enter Password", true, TEXT_BODY);
        if (!masked[0]) ui_text_mid(pw_x + 14, pw_y, 28, "Enter Password", TEXT_BODY, theme->text_muted);

        if (login_status[0]) {
            int lw = text_width(login_status, TEXT_SMALL);
            ui_text(cx + (cw - lw) / 2, cy + 168, login_status, TEXT_SMALL,
                    login_status_ok ? theme->accent : RGB(225, 76, 70));
        }

        draw_push_button(pw_x, cy + 190, pw_w, 28, "Sign In", true);

        int bot_y = cy + ch - 42;
        int half_w = (pw_w - 10) / 2;
        draw_push_button(pw_x, bot_y, half_w, 24, "Switch User", false);
        draw_push_button(pw_x + half_w + 10, bot_y, half_w, 24, "New Account", false);
    }
}

/* Built-in Tahoe icon set (tools/mkicons.py), filtered to any size. */
static void draw_icon_id(IconId id, int x, int y, int size, uint8_t alpha)
{
    if ((unsigned)id < ICON_COUNT)
        gfx_draw_rgba_scaled(surface(), icon_pixels[id], ICON_PX, ICON_PX, x, y, size, size, alpha);
}

static int icon_for_bundle(const char *bundle_id)
{
    static const struct { const char *id; IconId icon; } map[] = {
        { "com.ojjyos.finder", ICON_FINDER }, { "com.ojjyos.terminal", ICON_TERMINAL },
        { "com.ojjyos.textedit", ICON_TEXTEDIT }, { "com.ojjyos.notes", ICON_NOTES },
        { "com.ojjyos.preview", ICON_PREVIEW }, { "com.ojjyos.settings", ICON_SETTINGS },
        { "com.ojjyos.about", ICON_ABOUT }, { "com.ojjyos.calendar", ICON_CALENDAR },
        { "com.ojjyos.calculator", ICON_CALCULATOR }, { "com.ojjyos.monitor", ICON_MONITOR },
        { "com.ojjyos.web", ICON_WEB },
    };
    for (unsigned i = 0; bundle_id && i < ARRAY_SIZE(map); i++)
        if (strcmp(bundle_id, map[i].id) == 0) return (int)map[i].icon;
    return -1;
}

static void draw_app_icon_a(const AppInfo *app, int x, int y, int size, uint8_t alpha)
{
    int id = icon_for_bundle(app->bundle_id);
    if (id >= 0) draw_icon_id((IconId)id, x, y, size, alpha);
    else if (app->icon.valid)
        gfx_draw_rgba_scaled(surface(), app->icon.pixels, BUNDLE_ICON_SIZE, BUNDLE_ICON_SIZE, x, y, size, size, alpha);
}

static void draw_app_icon(const AppInfo *app, int x, int y, int size)
{
    draw_app_icon_a(app, x, y, size, 255);
}

/* Dock layout (macOS-style magnification): icons near the pointer grow
 * and push their neighbours apart, so nothing overlaps; the glass plate
 * widens with them. Shared by drawing and clicks. */


#define DOCK_GAP     10
#define DOCK_PAD     12

/* 0..1000: how far the Dock magnification has eased in (draw_dock). */
static int dock_hover_amt;

static void dock_layout(DockLayout *L)
{
    SettingsState *settings = settings_get();
    int count = MIN(app_registry_count(), APP_REGISTRY_MAX);
    int base = MAX(40, MIN(64, (int)settings->dock_size));
    int max = MAX(base, MIN(96, (int)settings->dock_magnify));
    L->count = count;
    L->base = base;
    L->hover = -1;
    L->bg_h = base + 2 * DOCK_PAD;
    L->bg_y = (int)comp_height - L->bg_h - 10;
    L->bottom = L->bg_y + L->bg_h - DOCK_PAD;

    /* Resting positions decide which icons the pointer is near. */
    int rest_w = count * base + (count - 1) * DOCK_GAP;
    int rest_x = ((int)comp_width - rest_w) / 2;
    bool near = cursor_y >= L->bg_y - (max - base) - 12 && cursor_y <= (int)comp_height &&
                cursor_x >= rest_x - base && cursor_x <= rest_x + rest_w + base;
    int total = 0;
    for (int i = 0; i < count; i++) {
        int size = base;
        if (near && max > base) {
            int c = rest_x + i * (base + DOCK_GAP) + base / 2;
            int d = ABS(cursor_x - c);
            int reach = base * 5 / 2;
            if (d < reach) {
                int t = 1000 - d * 1000 / reach;              /* 1 at the pointer -> 0 */
                t = t * t / 1000;
                size = base + (max - base) * t / 1000 * dock_hover_amt / 1000;
            }
        }
        L->size[i] = size;
        total += size;
    }
    total += (count - 1) * DOCK_GAP;
    /* Keep the hovered region under the pointer: centre on the resting
     * middle, so growth spreads to both sides. */
    int x = ((int)comp_width - total) / 2;
    for (int i = 0; i < count; i++) {
        L->x[i] = x;
        if (cursor_x >= x && cursor_x < x + L->size[i] + DOCK_GAP &&
            cursor_y >= L->bottom - L->size[i] && cursor_y <= L->bg_y + L->bg_h) L->hover = i;
        x += L->size[i] + DOCK_GAP;
    }
    L->bg_w = MAX(240, total + 2 * DOCK_PAD);
    L->bg_x = ((int)comp_width - L->bg_w) / 2;
}

static void draw_dock(uint64_t now_ms)
{
    /* Ease the magnification in/out (~150 ms) instead of snapping. */
    {
        static uint64_t last;
        int dt = last ? (int)MIN(now_ms - last, 100) : 16;
        last = now_ms;
        int base = MAX(40, MIN(64, (int)settings_get()->dock_size));
        int dock_top = (int)comp_height - (base + 2 * DOCK_PAD) - 10;
        bool near = !login_screen_visible() && cursor_y >= dock_top - 30 && overlay == OVERLAY_NONE;
        int target = near ? 1000 : 0;
        if (settings_get()->reduced_motion) dock_hover_amt = target;
        else if (dock_hover_amt < target) dock_hover_amt = MIN(target, dock_hover_amt + dt * 1000 / 150);
        else if (dock_hover_amt > target) dock_hover_amt = MAX(target, dock_hover_amt - dt * 1000 / 200);
        if (dock_hover_amt != target) anim_keepalive_until = now_ms + 50;
    }
    DockLayout L;
    dock_layout(&L);
    if (!L.count) return;

    draw_shadow(L.bg_x, L.bg_y, L.bg_w, L.bg_h, 22);
    ui_glass(L.bg_x, L.bg_y, L.bg_w, L.bg_h, 22, 120);

    for (int i = 0; i < L.count; i++) {
        AppInfo *app = app_registry_get(i);
        if (!app) continue;
        int size = L.size[i];
        int bounce = 0;
        if (app->bounce_until > now_ms) {
            /* Launch bounce: two decaying hops (ease in/out per hop). */
            int left = (int)(app->bounce_until - now_ms);       /* ms remaining of 800 */
            int t = 800 - MIN(800, left);
            int hop = t % 400, amp = t < 400 ? 18 : 9;
            int u = hop * 1000 / 400;                            /* 0..1000 */
            bounce = -(amp * 4 * u * (1000 - u) / 1000) / 1000;
        }
        int icon_y = L.bottom - size + bounce;
        draw_app_icon(app, L.x[i], icon_y, size);
        if (app->running)
            draw_rounded_rect_blend(L.x[i] + size / 2 - 2, L.bg_y + L.bg_h - 6, 4, 4, 2,
                                    settings_get()->dark_mode ? COLOR_WHITE : theme->midnight, 200);
    }

    /* Name tooltip above the hovered icon */
    if (L.hover >= 0 && overlay == OVERLAY_NONE && !dragging) {
        AppInfo *app = app_registry_get(L.hover);
        if (app) {
            int tw = text_width(app->name, TEXT_BODY_MEDIUM) + 22;
            int tx = L.x[L.hover] + L.size[L.hover] / 2 - tw / 2;
            int ty = L.bottom - L.size[L.hover] - 34;
            tx = MAX(6, MIN((int)comp_width - tw - 6, tx));
            draw_shadow(tx, ty, tw, 24, 8);
            ui_glass(tx, ty, tw, 24, 8, 215);
            ui_text_mid(tx + 11, ty, 24, app->name, TEXT_BODY_MEDIUM, theme->text);
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
            draw_rounded_rect_blend(x - 6, y - 6, DESKTOP_ICON_SIZE + 12, DESKTOP_ICON_SIZE + 12, 10,
                                    COLOR_BLACK, 50);
        }

        if (icon->is_disk) {
            draw_icon_id(ICON_DISK, x, y, DESKTOP_ICON_SIZE, 255);
        } else if (icon->type == VFS_TYPE_DIR) {
            if (true) {  /* built-in icon set */
                draw_icon_id(ICON_FOLDER, x, y, DESKTOP_ICON_SIZE, 255);
            } else {
                fb_fill_rect(x, y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, theme->accent_soft);
            }
        } else if (icon->type == VFS_TYPE_BUNDLE) {
            bool drew = false;
            for (int a = 0; a < app_registry_count(); a++) {
                AppInfo *app = app_registry_get(a);
                if (app && strcmp(app->path, icon->path) == 0 && app->icon.valid) {
                    draw_app_icon(app, x, y, DESKTOP_ICON_SIZE);
                    drew = true;
                    break;
                }
            }
            if (!drew) {
                fb_fill_rect(x, y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, theme->accent_soft);
            }
        } else {
            if (true) {  /* built-in icon set */
                draw_icon_id(ICON_FILE, x, y, DESKTOP_ICON_SIZE, 255);
            } else {
                fb_fill_rect(x, y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, theme->accent_soft);
            }
        }

        /* Label: centred under the icon, white with a soft shadow (it sits
         * on the wallpaper); selected = accent pill. */
        {
            int max_w = DESKTOP_CELL_W + 8;
            int tw = MIN(text_width(icon->name, TEXT_SMALL_BOLD), max_w);
            int lx = x + DESKTOP_ICON_SIZE / 2 - tw / 2, ly = y + DESKTOP_ICON_SIZE + 6;
            if (i == desktop_selected)
                draw_rounded_rect_blend(lx - 6, ly - 1, tw + 12, 17, 5, theme->accent, 235);
            else
                text_draw_n(surface(), lx, ly + 1, icon->name, -1, TEXT_SMALL_BOLD, COLOR_BLACK, 110);
            text_draw_fit(surface(), lx, ly, icon->name, TEXT_SMALL_BOLD, COLOR_WHITE, max_w);
        }
    }
}

/* Magnifying glass: AA ring + handle. */
static void draw_search_glyph(int x, int y, int size, Color c, uint8_t a)
{
    int r = size * 5 / 14;
    gfx_arc_up(surface(), x + r + 1, y + r + 1, r - 2, r, 180, c, a);
    for (int k = 0; k < size / 3; k++)
        draw_rounded_rect_blend(x + r * 2 - 2 + k, y + r * 2 - 2 + k, 3, 3, 1, c, a);
}

static void draw_spotlight(int anim)
{
    int x = ((int)comp_width - SPOTLIGHT_WIDTH) / 2;
    int y = 120 - overlay_offset(anim, 24);
    int r = 18;
    uint8_t fade = overlay_alpha(255, anim);
    GfxSurface *gs = surface();

    gfx_shadow(gs, x, y, SPOTLIGHT_WIDTH, SPOTLIGHT_HEIGHT, r, 28, 12, theme->shadow, (uint8_t)(95 * fade / 255));
    ui_glass_fade(x, y, SPOTLIGHT_WIDTH, SPOTLIGHT_HEIGHT, r, 205, fade);
    draw_search_glyph(x + 20, y + (SPOTLIGHT_HEIGHT - 20) / 2, 20, theme->text_muted, fade);
    int qy = text_y_center(y, SPOTLIGHT_HEIGHT, TEXT_HEADLINE);
    if (spotlight_query[0]) {
        int qw = text_draw_n(gs, x + 52, qy, spotlight_query, -1, TEXT_HEADLINE, theme->text, fade);
        draw_rounded_rect_blend(x + 53 + qw, y + (SPOTLIGHT_HEIGHT - 24) / 2, 2, 24, 1, theme->accent, fade);
    } else {
        draw_rounded_rect_blend(x + 52, y + (SPOTLIGHT_HEIGHT - 24) / 2, 2, 24, 1, theme->accent, fade);
        text_draw_n(gs, x + 58, qy, "Spotlight Search", -1, TEXT_HEADLINE, theme->text_muted, (uint8_t)(170 * fade / 255));
    }

    int list_y = y + SPOTLIGHT_HEIGHT + 8;
    /* Rows are 28 px (the click handler uses the same geometry) */
    int list_h = spotlight_count > 0 ? spotlight_count * 28 + 12 : 0;
    if (list_h > 0) {
        gfx_shadow(gs, x, list_y, SPOTLIGHT_WIDTH, list_h, 14, 24, 10, theme->shadow, (uint8_t)(80 * fade / 255));
        ui_glass_fade(x, list_y, SPOTLIGHT_WIDTH, list_h, 14, 215, fade);
        for (int i = 0; i < spotlight_count; i++) {
            int row_y = list_y + 6 + i * 28;
            bool sel = i == spotlight_selected;
            if (sel) draw_rounded_rect_blend(x + 6, row_y, SPOTLIGHT_WIDTH - 12, 28, 8, theme->accent, fade);
            const SearchResult *res = &spotlight_results[i];
            if (res->type == SEARCH_RESULT_APP && res->app_index >= 0) {
                AppInfo *app = app_registry_get(res->app_index);
                if (app) draw_app_icon(app, x + 14, row_y + 3, 22);
            } else if (res->type == SEARCH_RESULT_PICTURE) {
                int c = res->app_index;
                if (!wp_thumbs[c] && !wp_thumb_failed[c]) wallpaper_thumb_load(c);
                if (wp_thumbs[c])
                    gfx_blit_scaled(gs, wp_thumbs[c], wp_thumb_px[c][0], 0, 0, wp_thumb_px[c][0], wp_thumb_px[c][1],
                                    x + 10, row_y + 5, 30, 19, 4, fade);
            } else {
                draw_icon_id(ICON_FILE, x + 14, row_y + 3, 22, fade);
            }
            Color fg = sel ? COLOR_WHITE : theme->text;
            text_draw_n(gs, x + 46, text_y_center(row_y, 28, TEXT_BODY_MEDIUM), res->title, -1, TEXT_BODY_MEDIUM, fg, fade);
            int sw = MIN(text_width(res->subtitle, TEXT_SMALL), 230);
            int sx = x + SPOTLIGHT_WIDTH - 18 - sw;
            text_draw_fit(gs, sx, text_y_center(row_y, 28, TEXT_SMALL), res->subtitle, TEXT_SMALL,
                          sel ? RGB(225, 238, 255) : theme->text_muted, 230);
        }
    } else if (spotlight_query[0]) {
        ui_glass_fade(x, list_y, SPOTLIGHT_WIDTH, 40, 14, 215, fade);
        text_draw_n(gs, x + 20, text_y_center(list_y, 40, TEXT_BODY), "No results", -1, TEXT_BODY, theme->text_muted, fade);
    }
}

static void draw_launchpad(int anim)
{
    if (settings_get()->reduced_transparency) {
        draw_rounded_rect_blend(0, 0, (int)comp_width, (int)comp_height, 0,
                                theme->dock_tint, overlay_alpha(230, anim));
    } else {
        /* Frosted full-screen glass: blurred wallpaper, then a tint. */
        if (wp_cache_valid)
            gfx_round_rect_image(surface(), 0, 0, (int)comp_width, (int)comp_height, 0, wp_blur, overlay_alpha(220, anim));
        draw_rounded_rect_blend(0, 0, (int)comp_width, (int)comp_height, 0, theme->dock_tint, overlay_alpha(70, anim));
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

            /* Staggered entrance: each icon grows from 80% and fades in a
             * little after the previous one. */
            int p = (anim - idx * 30) * 1000 / MAX(1, 1000 - 12 * 30);
            int e = ease_out(p < 0 ? 0 : p > 1000 ? 1000 : p);
            int sz = LAUNCHPAD_ICON_SIZE * (800 + 200 * e / 1000) / 1000;
            int off = (LAUNCHPAD_ICON_SIZE - sz) / 2;
            draw_app_icon_a(app, ix + off, iy + off, sz, (uint8_t)(255 * e / 1000));
            if (e > 300) {
                int lw = MIN(text_width(app->name, TEXT_BODY_MEDIUM), LAUNCHPAD_ICON_SIZE + 36);
                int lx = ix + LAUNCHPAD_ICON_SIZE / 2 - lw / 2, ly = iy + LAUNCHPAD_ICON_SIZE + 10;
                text_draw_n(surface(), lx, ly + 1, app->name, -1, TEXT_BODY_MEDIUM, COLOR_BLACK, 90);
                text_draw_fit(surface(), lx, ly, app->name, TEXT_BODY_MEDIUM, COLOR_WHITE, LAUNCHPAD_ICON_SIZE + 36);
            }
            idx++;
        }
    }
}

/* Control Center: four tiles (Wi-Fi, Bluetooth, Appearance, Transparency)
 * plus Restart / Shut Down. Geometry shared with the click handler. */
#define CC_TILE_W   ((CONTROL_CENTER_WIDTH - 44) / 2)
#define CC_TILE_H   56
static void cc_tile_rect(int panel_x, int panel_y, int i, int *tx, int *ty)
{
    *tx = panel_x + 16 + (i % 2) * (CC_TILE_W + 12);
    *ty = panel_y + 40 + (i / 2) * (CC_TILE_H + 10);
}

static void draw_control_center(int anim)
{
    int x = (int)comp_width - CONTROL_CENTER_WIDTH - 20;
    int y = MENU_BAR_HEIGHT + 10 - overlay_offset(anim, 24);
    int r = 20;
    uint8_t fade = overlay_alpha(255, anim);
    GfxSurface *gs = surface();

    gfx_shadow(gs, x, y, CONTROL_CENTER_WIDTH, CONTROL_CENTER_HEIGHT, r, 26, 10, theme->shadow, (uint8_t)(90 * fade / 255));
    ui_glass_fade(x, y, CONTROL_CENTER_WIDTH, CONTROL_CENTER_HEIGHT, r, 200, fade);

    SettingsState *settings = settings_get();
    const NetInfo *ni = net_info();
    text_draw_n(gs, x + 16, text_y_center(y + 8, 26, TEXT_TITLE), "Control Center", -1, TEXT_TITLE, theme->text, fade);

    const char *titles[4] = { "Wi-Fi", "Bluetooth", "Appearance", "Transparency" };
    const char *status[4] = {
        network_status_text(), "No hardware",
        settings->dark_mode ? "Dark" : "Light",
        settings->reduced_transparency ? "Reduced" : "Glass"
    };
    bool on[4] = { ni->present && ni->enabled, false, settings->dark_mode, !settings->reduced_transparency };
    bool usable[4] = { ni->present, false, true, true };
    for (int i = 0; i < 4; i++) {
        int tx, ty;
        cc_tile_rect(x, y, i, &tx, &ty);
        draw_rounded_rect_blend(tx, ty, CC_TILE_W, CC_TILE_H, 14,
                                settings->dark_mode ? COLOR_WHITE : COLOR_WHITE,
                                (uint8_t)((settings->dark_mode ? 26 : (usable[i] ? 150 : 80)) * fade / 255));
        /* Round toggle: accent when on, frosted grey when off */
        int dx = tx + 10, dy = ty + (CC_TILE_H - 32) / 2;
        Color disc = on[i] ? theme->accent : (settings->dark_mode ? RGB(70, 78, 92) : RGB(196, 204, 214));
        draw_rounded_rect_blend(dx, dy, 32, 32, 16, disc, fade);
        Color g = on[i] ? COLOR_WHITE : theme->text;
        if (i == 0) {
            draw_wifi_glyph(dx + 8, dy + 7, g, !on[i]);
        } else if (i == 1) {   /* Bluetooth rune */
            for (int k = 0; k < 6; k++) {
                draw_rounded_rect_blend(dx + 15, dy + 8 + k * 3, 2, 3, 1, g, fade);
            }
            for (int k = 0; k < 4; k++) {
                draw_rounded_rect_blend(dx + 16 + k, dy + 9 + k, 2, 2, 1, g, fade);
                draw_rounded_rect_blend(dx + 19 - k, dy + 13 + k, 2, 2, 1, g, fade);
                draw_rounded_rect_blend(dx + 16 + k, dy + 23 - k, 2, 2, 1, g, fade);
                draw_rounded_rect_blend(dx + 19 - k, dy + 19 - k, 2, 2, 1, g, fade);
            }
        } else if (i == 2) {   /* half-moon: appearance */
            draw_rounded_rect_blend(dx + 8, dy + 8, 16, 16, 8, g, fade);
            draw_rounded_rect_blend(dx + 13, dy + 6, 14, 14, 7, disc, fade);
        } else {               /* layered squares: transparency */
            draw_rounded_rect_blend(dx + 8, dy + 8, 12, 12, 3, g, (uint8_t)(150 * fade / 255));
            draw_rounded_rect_blend(dx + 13, dy + 13, 12, 12, 3, g, fade);
        }
        int lx = tx + 50;
        if (text_width(titles[i], TEXT_BODY_BOLD) <= CC_TILE_W - 58)
            text_draw_n(gs, lx, ty + 11, titles[i], -1, TEXT_BODY_BOLD, usable[i] ? theme->text : theme->text_muted, fade);
        else
            text_draw_n(gs, lx, ty + 12, titles[i], -1, TEXT_BODY_MEDIUM, usable[i] ? theme->text : theme->text_muted, fade);
        text_draw_n(gs, lx, ty + 29, status[i], -1, TEXT_SMALL, theme->text_muted, fade);
    }

    /* Info line: current address, or a transient message */
    char info[64];
    info[0] = '\0';
    if (cc_message[0] && timer_get_ticks() < cc_message_until) {
        str_append(info, sizeof(info), cc_message);
    } else if (ni->configured && ni->enabled) {
        char ip[16];
        net_format_ip(ni->ip, ip, sizeof(ip));
        str_append(info, sizeof(info), "Network address  ");
        str_append(info, sizeof(info), ip);
    }
    text_draw_n(gs, x + 18, y + 40 + 2 * (CC_TILE_H + 10) + 3, info, -1, TEXT_SMALL, theme->text_muted, fade);

    /* Real power controls: Restart and Shut Down (rects match the click handler) */
    int btn_y = y + CONTROL_CENTER_HEIGHT - 44;
    int btn_w = (CONTROL_CENTER_WIDTH - 40) / 2;
    int btn_h = 28;
    draw_rounded_rect_blend(x + 16, btn_y, btn_w, btn_h, 9, settings->dark_mode ? COLOR_WHITE : COLOR_WHITE,
                            (uint8_t)((settings->dark_mode ? 30 : 160) * fade / 255));
    int rw = text_width("Restart", TEXT_BODY_MEDIUM);
    text_draw_n(gs, x + 16 + (btn_w - rw) / 2, text_y_center(btn_y, btn_h, TEXT_BODY_MEDIUM), "Restart", -1,
                TEXT_BODY_MEDIUM, theme->text, fade);
    gfx_round_rect_vgradient(gs, x + 24 + btn_w, btn_y, btn_w, btn_h, 9, RGB(240, 104, 96), RGB(214, 64, 58), fade);
    int sw = text_width("Shut Down", TEXT_BODY_BOLD);
    text_draw_n(gs, x + 24 + btn_w + (btn_w - sw) / 2, text_y_center(btn_y, btn_h, TEXT_BODY_BOLD), "Shut Down", -1,
                TEXT_BODY_BOLD, COLOR_WHITE, fade);
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
    if (wp_cache_valid)
        gfx_round_rect_image(surface(), 0, 0, (int)comp_width, (int)comp_height, 0, wp_blur, overlay_alpha(255, anim));
    draw_rounded_rect_blend(0, 0, (int)comp_width, (int)comp_height, 0, theme->midnight, overlay_alpha(70, anim));

    const char *header = "Mission Control";
    int hw = text_width(header, TEXT_LARGE);
    text_draw_n(surface(), ((int)comp_width - hw) / 2, 56, header, -1, TEXT_LARGE, COLOR_WHITE, overlay_alpha(255, anim));

    if (window_count == 0) {
        const char *empty = "No open windows \xE2\x80\x94 click anywhere or press Esc to return";
        int ew = text_width(empty, TEXT_BODY);
        text_draw_n(surface(), ((int)comp_width - ew) / 2, (int)comp_height / 2, empty, -1, TEXT_BODY,
                    COLOR_WHITE, overlay_alpha(220, anim));
        return;
    }

    for (int i = 0; i < window_count; i++) {
        int cx, cy, cw, ch;
        mission_control_card_rect(i, window_count, &cx, &cy, &cw, &ch);
        cy -= overlay_offset(anim, 16);

        CompositorWindow *win = &windows[i];
        AppWindowState *state = &app_states[i];

        uint8_t fade = overlay_alpha(255, anim);
        gfx_shadow(surface(), cx, cy, cw, ch, 14, 24, 10, theme->shadow, (uint8_t)(90 * fade / 255));
        ui_glass_fade(cx, cy, cw, ch, 14, 200, fade);
        if (i == active_window_index)
            gfx_round_rect_stroke(surface(), cx - 2, cy - 2, cw + 4, ch + 4, 16, theme->accent, fade);

        /* App icon */
        int aidx = app_registry_find_by_name(win->title);
        AppInfo *app = app_registry_get(aidx);
        if (app && app->icon.valid) {
            draw_app_icon(app, cx + 16, cy + 42, 40);
        } else {
            fb_fill_rect(cx + 16, cy + 42, 40, 40, theme->accent);
        }

        text_draw_fit(surface(), cx + 16, cy + 14, win->title, TEXT_TITLE, theme->text, cw - 32);

        const char *status_label = win->minimized ? "Minimized" :
                                   (i == active_window_index ? "Active Window" : "Open Window");
        ui_text(cx + 68, cy + 50, status_label, TEXT_BODY_MEDIUM, theme->text);

        char dim_buf[32];
        char num[12];
        dim_buf[0] = '\0';
        utoa((uint32_t)win->w, num, 10);
        str_append(dim_buf, sizeof(dim_buf), num);
        str_append(dim_buf, sizeof(dim_buf), "x");
        utoa((uint32_t)win->h, num, 10);
        str_append(dim_buf, sizeof(dim_buf), num);
        ui_text(cx + 68, cy + 70, dim_buf, TEXT_SMALL, theme->text_muted);

        ui_text(cx + 16, cy + ch - 24, "Click to focus", TEXT_SMALL, theme->text_muted);
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
    int cell = 84, size = 64;
    int width = 24 + shown * cell;
    int height = 128;
    int x = ((int)comp_width - width) / 2;
    int y = ((int)comp_height - height) / 2 - 40 - overlay_offset(anim, 18);
    uint8_t fade = overlay_alpha(255, anim);

    gfx_shadow(surface(), x, y, width, height, 22, 30, 12, theme->shadow, (uint8_t)(100 * fade / 255));
    ui_glass_fade(x, y, width, height, 22, 200, fade);

    int sel = app_switcher_index % shown;
    for (int i = 0; i < shown; i++) {
        AppInfo *app = app_registry_get(running[i]);
        int cx = x + 12 + i * cell;
        int icon_x = cx + (cell - size) / 2, icon_y = y + 16;
        if (i == sel)
            draw_rounded_rect_blend(cx + 2, y + 8, cell - 4, height - 16, 16,
                                    settings_get()->dark_mode ? COLOR_WHITE : theme->midnight,
                                    (uint8_t)(34 * fade / 255));
        draw_app_icon(app, icon_x, icon_y, size);
        if (i == sel) {
            int lw = MIN(text_width(app->name, TEXT_SMALL_BOLD), cell - 6);
            text_draw_fit(surface(), cx + (cell - lw) / 2, y + height - 34, app->name, TEXT_SMALL_BOLD, theme->text, cell - 6);
        }
    }
}

static void spotlight_refresh(void)
{
    spotlight_count = search_index_query(spotlight_query, spotlight_results, SEARCH_RESULTS_MAX);
    /* Desktop pictures (the compositor owns the list): choosing one sets it. */
    for (int i = 0; i < wallpaper_choice_count() && spotlight_count < SEARCH_RESULTS_MAX; i++) {
        const char *name = wallpaper_choice_name(i);
        if (!spotlight_query[0] || !str_contains_ci(name, spotlight_query)) continue;
        SearchResult *r = &spotlight_results[spotlight_count++];
        memset(r, 0, sizeof(*r));
        r->type = SEARCH_RESULT_PICTURE;
        strncpy(r->title, name, sizeof(r->title) - 1);
        strncpy(r->subtitle, "Desktop Picture", sizeof(r->subtitle) - 1);
        r->app_index = i;
    }
    if (spotlight_selected >= spotlight_count) {
        spotlight_selected = spotlight_count > 0 ? 0 : 0;
    }
}

static void spotlight_activate(const SearchResult *res)
{
    if (res->type == SEARCH_RESULT_APP) launch_app_index(res->app_index, timer_get_ticks());
    else if (res->type == SEARCH_RESULT_PICTURE) wallpaper_choose(res->app_index);
    else if (res->type == SEARCH_RESULT_FILE) open_path(res->path);
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
    AppType type = app_type_from_bundle(app->bundle.manifest.bundle_id);
    if (type != APP_DEMO) {
        /* Only switch the menu bar once the window really exists. */
        if (app_open_window(type, app->name) < 0) {
            serial_printf("[UI] could not open a window for '%s'\n", app->name);
            return;
        }
        app->running = true;
        strncpy(active_app_name, app->name, sizeof(active_app_name) - 1);
        active_app_name[sizeof(active_app_name) - 1] = '\0';
        app->bounce_until = now_ms + 800;
    } else {
        app->running = true;
        app->bounce_until = now_ms + 800;
        bundle_launch(&app->bundle);
    }
}

/* Ease-out cubic on a 0..1000 progress value: fast start, gentle landing. */
static int ease_out(int t)
{
    if (t <= 0) return 0;
    if (t >= 1000) return 1000;
    int u = 1000 - t;
    return 1000 - (int)(((int64_t)u * u * u) / 1000000);
}

static uint8_t overlay_alpha(uint8_t base, int anim)
{
    if (anim <= 0) return 0;
    if (anim >= 1000) return base;
    return (uint8_t)((base * ease_out(anim)) / 1000);
}

static int overlay_offset(int anim, int max_offset)
{
    if (anim <= 0) return max_offset;
    if (anim >= 1000) return 0;
    return (max_offset * (1000 - ease_out(anim))) / 1000;
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
    /* Files may have changed while it was in the background. */
    if (app_states[idx].type == APP_FINDER) app_states[idx].finder.needs_refresh = true;
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
    if (idx >= 0 && users_is_authenticated()) ghost_capture(idx, GHOST_CLOSE);
    if (idx < 0) return -1;
    if (app_states[idx].type == APP_NOTES) notes_save_open(&app_states[idx].notes);

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
    if (!dir) {
        /* Folder vanished: show it empty instead of retrying every frame. */
        state->selected = -1;
        state->needs_refresh = false;
        return;
    }

    VfsDirEntry entry;
    while (vfs_readdir(dir, &entry) == 0 && state->entry_count < FINDER_MAX_ENTRIES) {
        if (entry.name[0] == '.') continue;            /* hidden (e.g. .Trash) */
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
    /* Never leave the selection pointing at a stale entry (Delete/Enter), and
     * never keep previewing a file that is no longer selected. */
    if (state->selected >= state->entry_count) state->selected = state->entry_count - 1;
    state->preview_ready = false;
    if (state->selected >= 0) finder_update_preview(state);
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

    /* Navigating after Back discards the forward entries (browser rules). */
    if (state->history_count > 0) state->history_count = state->history_pos + 1;
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
    /* Opening dst with TRUNC first would wipe src when they are the same
     * file (Cmd+C, Cmd+V in one folder). Never overwrite anything here. */
    if (!src[0] || !dst[0] || strcmp(src, dst) == 0 || vfs_exists(dst)) return -1;
    VfsFile *in = vfs_open(src, VFS_O_READ);
    if (!in) return -1;
    VfsFile *out = vfs_open(dst, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    if (!out) {
        vfs_close(in);
        return -1;
    }

    char buffer[128];
    ssize_t bytes;
    int result = 0;
    while ((bytes = vfs_read(in, buffer, sizeof(buffer))) > 0) {
        if (vfs_write(out, buffer, bytes) != bytes) { result = -1; break; }
    }
    if (bytes < 0) result = -1;
    vfs_close(in);
    vfs_close(out);
    if (result != 0) vfs_unlink(dst);   /* no silently truncated copies */
    return result;
}

/* First free "name", "name copy", "name copy 2", ... in dir (macOS-style;
 * the copy marker goes before the extension). */
static int unique_copy_path(char *out, size_t size, const char *dir, const char *base)
{
    char name[VFS_NAME_MAX + 1];
    const char *dot = NULL;
    for (const char *c = base; *c; c++) if (*c == '.') dot = c;
    size_t stem = (dot && dot != base) ? (size_t)(dot - base) : strlen(base);
    for (int n = 0; n < 100; n++) {
        if (n == 0) {
            strncpy(name, base, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        } else {
            char suffix[16] = " copy";
            if (n > 1) { suffix[5] = ' '; utoa((uint64_t)n, suffix + 6, 10); }
            size_t keep = MIN(stem, sizeof(name) - 1 - strlen(suffix) - (dot && dot != base ? strlen(dot) : 0));
            if ((int)keep <= 0) return -1;
            memcpy(name, base, keep);
            name[keep] = '\0';
            str_append(name, sizeof(name), suffix);
            if (dot && dot != base) str_append(name, sizeof(name), dot);
        }
        if (vfs_join_path(out, size, dir, name) != 0) return -1;
        if (!vfs_exists(out)) return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Trash: Finder's Delete moves items to ~/.Trash; emptying deletes.   */
/* ------------------------------------------------------------------ */

static void trash_dir(char *out, size_t size)
{
    users_make_home_path(out, size, ".Trash");
}

static bool path_in_trash(const char *path)
{
    char t[160];
    trash_dir(t, sizeof(t));
    size_t n = strlen(t);
    return strncmp(path, t, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

/* Remove a file or a whole folder tree (bounded depth). */
static int delete_tree(const char *path, int depth)
{
    if (depth > 16) return -1;
    if (vfs_isdir(path)) {
        for (int pass = 0; pass < 64; pass++) {
            char names[16][64];
            int n = 0;
            VfsDir *dir = vfs_opendir(path);
            if (!dir) break;
            VfsDirEntry entry;
            while (n < 16 && vfs_readdir(dir, &entry) == 0) {
                strncpy(names[n], entry.name, sizeof(names[n]) - 1);
                names[n][sizeof(names[n]) - 1] = '\0';
                n++;
            }
            vfs_closedir(dir);
            if (!n) break;
            for (int i = 0; i < n; i++) {
                char child[256];
                if (vfs_join_path(child, sizeof(child), path, names[i]) == 0) delete_tree(child, depth + 1);
            }
        }
    }
    return vfs_unlink(path);
}

static int move_to_trash(const char *path)
{
    char trash[160], dst[256];
    trash_dir(trash, sizeof(trash));
    if (!vfs_isdir(trash)) vfs_mkdir(trash);
    if (unique_copy_path(dst, sizeof(dst), trash, vfs_basename(path)) != 0) return -1;
    return vfs_rename(path, dst);
}

static void empty_trash(void)
{
    char trash[160];
    trash_dir(trash, sizeof(trash));
    if (!vfs_isdir(trash)) return;
    delete_tree(trash, 0);
    vfs_mkdir(trash);
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

/* Network command output goes to the (single) Terminal window, wherever
 * it currently sits in the window stack, and keeps the view at the end. */
static void terminal_net_print(const char *line)
{
    int idx = app_window_index[APP_TERMINAL];
    if (idx < 0 || idx >= window_count) return;
    TerminalState *term = &app_states[idx].terminal;
    terminal_append_line(term, line);
    term->scroll = TERMINAL_MAX_LINES;      /* clamped to the end when drawn */
    mark_dirty();
}

static void terminal_exec(TerminalState *term, const char *raw)
{
    if (!term || !raw) return;
    /* Trailing spaces (e.g. left by Tab completion) are not part of a path. */
    char cmd[TERMINAL_LINE_MAX];
    strncpy(cmd, raw, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    for (size_t n = strlen(cmd); n > 0 && cmd[n - 1] == ' '; n--) cmd[n - 1] = '\0';

    if (netcmd_exec(cmd, terminal_net_print)) return;

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
        terminal_append_line(term, "Network: ifconfig, net on|off|renew,");
        terminal_append_line(term, "  ping <host> [n], nslookup <host>, curl [-I] <url>");
        terminal_append_line(term, "  (Ctrl+C stops a running network command)");
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
        int created = users_create(uname, fname, upass, true);
        if (created == 0) {
            terminal_append_line(term, "Account created & PBKDF2 credentials saved.");
        } else if (created == -4) {
            terminal_append_line(term, "adduser: only an administrator can add accounts");
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

        /* cp file dir -> dir/file (Unix). Refuse src == dst: opening dst
         * with TRUNC first used to wipe the file ("cp a a", "cp a ./a"). */
        if (vfs_isdir(dst)) {
            char into[256];
            if (vfs_join_path(into, sizeof(into), dst, vfs_basename(src_resolved)) != 0) {
                terminal_append_line(term, "cp: path too long");
                return;
            }
            strncpy(dst, into, sizeof(dst) - 1);
            dst[sizeof(dst) - 1] = '\0';
        }
        char norm_src[256], norm_dst[256];
        if (vfs_normalize_path(src_resolved, norm_src, sizeof(norm_src)) != 0 ||
            vfs_normalize_path(dst, norm_dst, sizeof(norm_dst)) != 0) {
            terminal_append_line(term, "cp: path too long");
            return;
        }
        if (strcmp(norm_src, norm_dst) == 0) {
            terminal_append_line(term, "cp: source and destination are the same file");
            return;
        }
        if (!vfs_isfile(norm_src)) {
            terminal_append_line(term, "cp: cannot open source");
            return;
        }
        if (vfs_exists(norm_dst)) vfs_unlink(norm_dst);   /* cp overwrites, like Unix */
        if (copy_file_path(norm_src, norm_dst) != 0) {
            terminal_append_line(term, "cp: copy failed (disk full?)");
        }
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
            while (len > 0 && text[len - 1] == ' ') len--;   /* "echo hi > f" writes "hi" */
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

        open_path(path);
        return;
    }

    terminal_append_line(term, "Unknown command");
}

/* Open a path like Finder would: app bundle -> launch, folder -> Finder,
 * file -> TextEdit. Used by Terminal "open" and Spotlight. */
static void open_path(const char *path)
{
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
        /* A note (a .txt file in ~/Notes) opens in Notes, next to the others. */
        char notes_dir[128], parent[128];
        int notes_idx = app_registry_find_by_bundle_id("com.ojjyos.notes");
        if (notes_idx >= 0 && notes_is_note_name(vfs_basename(path)) &&
            users_make_home_path(notes_dir, sizeof(notes_dir), "Notes") == 0 &&
            vfs_dirname(path, parent, sizeof(parent)) == 0 && strcmp(parent, notes_dir) == 0) {
            launch_app_index(notes_idx, timer_get_ticks());
            TextEditState *edit = notes_editor();
            int index = notes_find(&notes_lib, vfs_basename(path));
            if (edit && index >= 0 && index != notes_lib.selected) notes_open_note(edit, index);
            return;
        }
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

    /* A file that does not fit the grid (or holds NUL bytes) is shown cut
     * down and opens read-only: saving that copy would destroy the rest. */
    char buffer[128];
    int line = 0;
    int col = 0;
    bool any = false;
    bool ends_with_newline = false;
    bool full = false;
    ssize_t bytes;

    while (!full && (bytes = vfs_read(file, buffer, sizeof(buffer))) > 0) {
        any = true;
        for (int i = 0; i < bytes; i++) {
            char c = buffer[i];
            if (line >= TEXTEDIT_MAX_LINES) {
                edit->read_only = true;
                full = true;
                break;
            }
            if (c == '\n') {
                line++;
                col = 0;
                ends_with_newline = true;
                continue;
            }
            ends_with_newline = false;
            if (c == '\0' || col >= TEXTEDIT_LINE_MAX - 1) {
                edit->read_only = true;
                continue;
            }
            edit->lines[line][col++] = c;
            edit->lines[line][col] = '\0';
        }
    }
    vfs_close(file);

    if (!any) {
        strcpy(edit->status, "Empty file");
        return;
    }
    /* The newline that ends the last line does not start another one:
     * textedit_save writes one per line, so load + save keeps the bytes. */
    edit->line_count = ends_with_newline ? line : line + 1;
    if (edit->line_count < 1) edit->line_count = 1;
    if (edit->line_count > TEXTEDIT_MAX_LINES) edit->line_count = TEXTEDIT_MAX_LINES;
    if (edit->read_only) strcpy(edit->status, "Read-only: too long to edit here");
}

/* Bytes textedit_save writes: every line plus its newline. */
static int textedit_byte_count(const TextEditState *edit)
{
    int total = 0;
    for (int i = 0; i < edit->line_count; i++) total += (int)strlen(edit->lines[i]) + 1;
    return total;
}

/* Keys that never change a read-only document. */
static bool textedit_key_allowed(const TextEditState *edit, KeyCode key)
{
    if (!edit->read_only) return true;
    return key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN ||
           key == KEY_HOME || key == KEY_END || key == KEY_PAGEUP || key == KEY_PAGEDOWN;
}

/* Writes the document back. `notify` is true for TextEdit's explicit
 * saves (Cmd+S, File > Save): those push a notification and log a
 * [TEXTEDIT] line with the counts. Notes autosaves stay quiet — a toast
 * per second of typing would be noise. */
static void textedit_save(TextEditState *edit, bool notify)
{
    if (!edit) return;
    if (edit->read_only) {
        strcpy(edit->status, "Read-only: not saved");
        if (notify) {
            notifications_push("TextEdit: file is read-only, not saved");
            serial_printf("[TEXTEDIT] Read-only, not saved: '%s'\n", edit->file_path[0] ? vfs_basename(edit->file_path) : "Untitled");
        }
        return;
    }
    if (edit->file_path[0] == '\0') {
        users_make_home_path(edit->file_path, sizeof(edit->file_path), "Documents/Untitled.txt");
    }

    VfsFile *file = vfs_open(edit->file_path, VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    if (!file) {
        strcpy(edit->status, "Save failed");
        if (notify) notifications_push("TextEdit: save failed");
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
    if (notify) {
        int words, chars;
        textedit_counts(edit, &words, &chars);
        char message[48];
        strcpy(message, "Saved ");
        str_append(message, sizeof(message), vfs_basename(edit->file_path));
        notifications_push(message);
        serial_printf("[TEXTEDIT] Saved '%s' %d bytes, %d words, %d chars\n",
                      vfs_basename(edit->file_path), textedit_byte_count(edit), words, chars);
    }
}

/* Tab completion: command names for the first word, file names after. */
static void terminal_complete(TerminalState *term)
{
    static const char *commands[] = {
        "adduser", "cat", "cd", "clear", "cp", "curl", "date", "echo", "heap", "help", "ifconfig", "kill", "lock",
        "logout", "ls", "mem", "mkdir", "mv", "net", "nslookup", "ntp", "open", "passwd", "ping", "ps", "pwd",
        "reboot", "rm", "run", "shutdown", "su", "sync", "top", "touch", "users", "whoami"
    };
    char *input = term->input;
    int start = term->input_len;
    while (start > 0 && input[start - 1] != ' ') start--;
    const char *word = input + start;
    size_t wlen = strlen(word);

    char matches[24][64];
    bool is_dir[24];
    int count = 0;
    if (start == 0) {
        for (unsigned i = 0; i < ARRAY_SIZE(commands) && count < 24; i++) {
            if (strncmp(commands[i], word, wlen) == 0) {
                strncpy(matches[count], commands[i], 63);
                matches[count][63] = '\0';
                is_dir[count++] = false;
            }
        }
    } else {
        /* Split "dir/prefix"; list dir (relative to cwd) for names with prefix. */
        const char *slash = NULL;
        for (const char *c = word; *c; c++) if (*c == '/') slash = c;
        char dir_arg[TERMINAL_LINE_MAX], dir[160];
        const char *prefix = word;
        if (slash) {
            size_t dl = (size_t)(slash - word);
            memcpy(dir_arg, word, dl ? dl : 1);
            dir_arg[dl ? dl : 1] = '\0';
            prefix = slash + 1;
        } else {
            strcpy(dir_arg, ".");
        }
        if (terminal_resolve_path(term, dir_arg, dir, sizeof(dir)) != 0) return;
        VfsDir *d = vfs_opendir(dir);
        if (!d) return;
        VfsDirEntry entry;
        size_t pl = strlen(prefix);
        while (vfs_readdir(d, &entry) == 0 && count < 24) {
            if (entry.name[0] == '.' && prefix[0] != '.') continue;
            if (strncmp(entry.name, prefix, pl) != 0) continue;
            strncpy(matches[count], entry.name, 63);
            matches[count][63] = '\0';
            is_dir[count++] = entry.type == VFS_TYPE_DIR;
        }
        vfs_closedir(d);
        word = prefix;
        wlen = pl;
        start = (int)(word - input);
    }
    if (count == 0) return;

    /* Longest common prefix of all matches */
    size_t common = strlen(matches[0]);
    for (int i = 1; i < count; i++) {
        size_t k = 0;
        while (k < common && matches[i][k] == matches[0][k]) k++;
        common = k;
    }
    char add[66];
    size_t n = 0;
    for (size_t k = wlen; k < common && n < 63; k++) add[n++] = matches[0][k];
    if (count == 1) add[n++] = is_dir[0] ? '/' : ' ';
    add[n] = '\0';
    if (n) {
        if (term->input_len + (int)n < TERMINAL_LINE_MAX - 1) {
            strcpy(term->input + term->input_len, add);
            term->input_len += (int)n;
        }
        return;
    }
    /* Ambiguous: list the choices like a Unix shell */
    char line[TERMINAL_LINE_MAX];
    line[0] = '\0';
    for (int i = 0; i < count; i++) {
        if (strlen(line) + strlen(matches[i]) + 3 >= sizeof(line)) { terminal_append_line(term, line); line[0] = '\0'; }
        str_append(line, sizeof(line), matches[i]);
        if (is_dir[i]) str_append(line, sizeof(line), "/");
        str_append(line, sizeof(line), "  ");
    }
    if (line[0]) terminal_append_line(term, line);
    term->scroll = TERMINAL_MAX_LINES;
    (void)start;
}

static void terminal_handle_key(TerminalState *term, char ascii, KeyCode key)
{
    if (!term) return;

    if (key == KEY_TAB) {
        terminal_complete(term);
        return;
    }

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
        /* Commands may create/move/delete files: re-read open Finder views. */
        for (int w = 0; w < window_count; w++) {
            if (app_states[w].type == APP_FINDER) app_states[w].finder.needs_refresh = true;
        }
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
    bool ctrl = (modifiers & INPUT_MOD_CTRL) != 0;

    /* Word-wise jumps (Ctrl+arrows). */
    if (ctrl && (key == KEY_LEFT || key == KEY_RIGHT)) {
        textedit_move_horiz(edit, key == KEY_RIGHT ? 1 : -1, true, shift);
        return;
    }

    /* Up/Down/Page move over visual rows from the wrap layout, keeping
     * the goal column; the compositor stores the view size while
     * drawing (16 columns until the window has been drawn once). */
    if (key == KEY_UP || key == KEY_DOWN) {
        textedit_move_visual(edit, edit->view_cols, key == KEY_DOWN ? 1 : -1);
        if (!shift) textedit_clear_selection(edit);
        return;
    }
    if (key == KEY_PAGEUP || key == KEY_PAGEDOWN) {
        int page = MAX(1, edit->view_rows);
        textedit_move_visual(edit, edit->view_cols, key == KEY_PAGEDOWN ? page : -page);
        if (!shift) textedit_clear_selection(edit);
        return;
    }

    if (key == KEY_LEFT || key == KEY_RIGHT) {
        textedit_move_horiz(edit, key == KEY_RIGHT ? 1 : -1, false, shift);
        return;
    }
    if (key == KEY_HOME || key == KEY_END) {
        textedit_move_line_edge(edit, key == KEY_END, shift);
        return;
    }
    if (key == KEY_BACKSPACE) {
        textedit_backspace(edit);
        return;
    }
    if (key == KEY_ENTER) {
        textedit_newline(edit);
        textedit_clear_selection(edit);
        return;
    }

    if (ascii >= 32 && ascii <= 126) {
        textedit_insert_char(edit, ascii);
    }
}

/* ---- Notes: the ~/Notes library (ui/notes.c) around the shared editor ---- */

static TextEditState *notes_editor(void)
{
    int idx = app_window_index[APP_NOTES];
    return (idx >= 0 && idx < window_count) ? &app_states[idx].notes : NULL;
}

/* Writes the open note if it has unsaved edits: autosave, switching notes,
 * New Note, closing, locking, Cmd+S and every disk sync go through here. */
static void notes_save_open(TextEditState *edit)
{
    if (!edit || !edit->dirty || !edit->file_path[0] || edit->read_only) return;
    textedit_save(edit, false);
    if (edit->dirty) {
        serial_printf("[NOTES] Save failed for '%s'\n", vfs_basename(edit->file_path));
        return;
    }
    serial_printf("[NOTES] Saved '%s' %d bytes\n", vfs_basename(edit->file_path), textedit_byte_count(edit));
    notes_refresh(&notes_lib, notes_lib.selected);
}

static void notes_open_note(TextEditState *edit, int index)
{
    char path[NOTES_DIR_MAX + NOTES_NAME_MAX];
    if (!edit || notes_path(&notes_lib, index, path, sizeof(path)) != 0) return;
    if (strlen(path) >= sizeof(edit->file_path)) {
        strcpy(edit->status, "Note name too long");
        return;
    }
    notes_save_open(edit);
    notes_lib.selected = index;
    notes_follow_selection = true;
    textedit_load_file(edit, path);
    VfsStat st;
    int size = vfs_stat(path, &st) == 0 ? (int)st.size : -1;
    serial_printf("[NOTES] Opened '%s' %d bytes%s\n", notes_lib.items[index].name, size,
                  edit->read_only ? " read-only" : "");
}

/* Lists the current user's ~/Notes (made on first use) and opens the
 * first note. */
static void notes_reload(TextEditState *edit)
{
    if (!edit) return;
    textedit_clear(edit);
    edit->file_path[0] = '\0';
    int result = notes_open(&notes_lib, users_current_home());
    if (result < 0) {
        strcpy(edit->status, "Can't open the Notes folder");
        serial_printf("[NOTES] No Notes folder\n");
        return;
    }
    if (result == 1) serial_printf("[NOTES] Imported Documents/Notes.txt\n");
    serial_printf("[NOTES] %d notes in %s\n", notes_lib.count, notes_lib.dir);
    if (notes_lib.count > 0) notes_open_note(edit, 0);
    else strcpy(edit->status, "No notes yet");
}

/* Notes came to the front again: pick up notes made or deleted elsewhere
 * (Finder, Terminal) in the meantime. */
static void notes_rescan(TextEditState *edit)
{
    if (!edit) return;
    if (!notes_lib.dir[0]) {
        notes_reload(edit);
        return;
    }
    notes_save_open(edit);
    notes_scan(&notes_lib);
    if (notes_lib.selected >= 0) return;
    if (notes_lib.count > 0) {           /* the open note is gone, or none was open */
        edit->dirty = false;
        notes_open_note(edit, 0);
    } else {
        textedit_clear(edit);
        edit->file_path[0] = '\0';
        strcpy(edit->status, "No notes yet");
    }
}

static void notes_new(TextEditState *edit)
{
    if (!edit) return;
    notes_save_open(edit);
    if (!notes_lib.dir[0]) {
        strcpy(edit->status, "Can't open the Notes folder");
        return;
    }
    int index = notes_create(&notes_lib);
    if (index < 0) {
        strcpy(edit->status, notes_lib.count >= NOTES_MAX ? "Notes is full" : "Couldn't make a note");
        serial_printf("[NOTES] New note failed\n");
        return;
    }
    serial_printf("[NOTES] New note '%s'\n", notes_lib.items[index].name);
    notes_open_note(edit, index);
    strcpy(edit->status, "New note");
}

static void notes_handle_key(TextEditState *edit, char ascii, KeyCode keycode, uint8_t modifiers)
{
    if (!edit->file_path[0]) {
        /* Typing with no note open starts one. */
        if ((modifiers & (INPUT_MOD_SUPER | INPUT_MOD_CTRL)) || ascii < 32 || ascii > 126) return;
        notes_new(edit);
        if (!edit->file_path[0]) return;
    }
    if (!textedit_key_allowed(edit, keycode)) return;
    textedit_handle_key(edit, ascii, keycode, modifiers);
    if (edit->dirty) notes_lib.last_edit_ms = timer_get_ticks();
}

/* Autosave: one write once typing has paused for NOTES_AUTOSAVE_MS (the
 * old Notes rewrote the file on every key), and at once when the screen
 * locks - the next user may not be allowed to write this home folder. */
static void notes_autosave_tick(uint64_t now_ms)
{
    TextEditState *edit = notes_editor();
    if (!edit || !edit->dirty || edit->read_only) return;
    if (!login_screen_visible() && !notes_autosave_due(true, notes_lib.last_edit_ms, now_ms)) return;
    notes_save_open(edit);
    if (edit->dirty) notes_lib.last_edit_ms = now_ms;      /* failed: try again in a second */
    mark_dirty();
}

void compositor_save_documents(void)
{
    notes_save_open(notes_editor());
}

static void notes_click(TextEditState *edit, int content_x, int content_y, int content_w, int content_h, int x, int y)
{
    int sw = notes_sidebar_w(content_w);
    if (x < content_x || x >= content_x + sw || y < content_y) return;
    if (y >= content_y + NOTES_BUTTON_Y && y < content_y + NOTES_BUTTON_Y + NOTES_BUTTON_H) {
        notes_new(edit);
        return;
    }
    int top = content_y + NOTES_LIST_Y;
    if (y < top) return;
    int row = (y - top) / NOTES_ROW_H;
    if (row >= notes_visible_rows(content_h)) return;
    int index = notes_lib.scroll + row;
    if (index < notes_lib.count && index != notes_lib.selected) notes_open_note(edit, index);
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
        notes_reload(&state->notes);
    } else if (type == APP_PREVIEW) {
        state->preview.index = wp_choice;
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
        browser_open();
    } else if (type == APP_CALCULATOR) {
        calculator_reset(&state->calc);
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
        if (windows[idx].minimized) {
            windows[idx].anim_open = 0; windows[idx].animating = true;
            windows[idx].anim_start_ms = 0; windows[idx].anim_frames = 0;
        }
        windows[idx].minimized = false;
        raise_window_to_top(idx);
        if (type == APP_NOTES) notes_rescan(&app_states[window_count - 1].notes);
        CompositorWindow *top = &windows[window_count - 1];
        serial_printf("[UI] window '%s' at %d,%d %dx%d\n", top->title, top->x, top->y, top->w, top->h);
        return top->id;
    }

    int w = 560;
    int h = 380;
    int x = 140 + (window_count * 18) % 90;
    int y = 90 + (window_count * 18) % 70;
    if (type == APP_FINDER) { w = 640; h = 420; }
    if (type == APP_SETTINGS) { w = 700; h = 460; }
    if (type == APP_TERMINAL) { w = 600; h = 360; }
    if (type == APP_TEXTEDIT) { w = 560; h = 360; }
    if (type == APP_NOTES) { w = 640; h = 400; }
    if (type == APP_PREVIEW) { w = 480; h = 320; }
    if (type == APP_CALENDAR) { w = 860; h = 480; }
    if (type == APP_ABOUT) { w = 500; h = 350; }
    if (type == APP_WEB) {
        w = MIN(1240, (int)comp_width - 60);
        h = MIN(820, (int)comp_height - MENU_BAR_HEIGHT - DOCK_HEIGHT - 24);
    }
    if (type == APP_CALCULATOR) { w = 320; h = 400; }
    if (type == APP_MONITOR) { w = 540; h = 380; }

    /* Keep new windows fully on screen (cascade offsets + large apps such as
     * Web could otherwise run past the right edge or under the Dock). */
    w = MIN(w, (int)comp_width - 16);
    h = MIN(h, (int)comp_height - MENU_BAR_HEIGHT - DOCK_HEIGHT - 24);
    if (x + w > (int)comp_width - 8) x = MAX(8, (int)comp_width - 8 - w);
    if (y + h > (int)comp_height - DOCK_HEIGHT - 16) y = MAX(MENU_BAR_HEIGHT + 8, (int)comp_height - DOCK_HEIGHT - 16 - h);

    int id = create_window_internal(title, x, y, w, h, type);
    if (id >= 0) {
        serial_printf("[UI] window '%s' at %d,%d %dx%d\n", title, x, y, w, h);   /* test/diagnostic receipt */
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
            notes_reload(&st->notes);
        } else if (st->type == APP_CALENDAR) {
            calendar_load(&st->calendar);
        }
    }
}

void compositor_init(uint32_t width, uint32_t height)
{
    comp_width = width;
    comp_height = height;
    text_set_density(fb_ui_scale());

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
    net_set_enabled(settings_get()->wifi_enabled);     /* Wi-Fi tile state survives reboots */
    compositor_set_dark_mode(settings_get()->dark_mode);
    /* The saved picture, else Tahoe (when the pack is installed), else the
     * built-in waves - each following the appearance. */
    int pick = wallpaper_find(settings_get()->wallpaper);
    if (pick < 0) pick = wallpaper_find(dark_mode ? "Tahoe Dark" : "Tahoe Light");
    if (pick < 0) pick = dark_mode ? 1 : 0;
    wallpaper_show(pick);

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
    theme_set_accent(settings_get()->accent);   /* accent survives theme flips */
    settings_get()->dark_mode = enabled;
    wallpaper_retint();              /* window glass tint follows the theme */
}

void compositor_toggle_appearance(void)
{
    toggle_appearance();
}

/* Auto appearance: dark between 19:00 and 07:00. Called once a minute from
 * the main loop and immediately when Auto is selected. */
void compositor_appearance_auto_tick(void)
{
    SettingsState *s = settings_get();
    if (s->appearance != 2) return;
    RtcTime now;
    rtc_read_time(&now);
    bool dark = now.hour >= 19 || now.hour < 7;
    if (dark != dark_mode) {
        toggle_appearance();
        serial_printf("[UI] Appearance auto -> %s\n", dark ? "dark" : "light");
    }
}

/* Show a desktop picture by name (see wallpaper_choice_name). */
void compositor_set_wallpaper(const char *name)
{
    int i = wallpaper_find(name);
    if (i >= 0) wallpaper_choose(i);
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
static bool handle_app_shortcut(AppWindowState *state, KeyCode keycode, uint8_t modifiers)
{
    if (state->type == APP_WEB) return browser_shortcut(keycode, modifiers);
    if (state->type != APP_TEXTEDIT && state->type != APP_NOTES) {
        return false;
    }
    bool notes = state->type == APP_NOTES;
    TextEditState *edit = notes ? &state->notes : &state->textedit;
    /* Cut/paste change the text: not in a read-only document, and Notes
     * needs a note to change. */
    if ((keycode == KEY_X || keycode == KEY_V) && (edit->read_only || (notes && !edit->file_path[0]))) {
        return true;
    }

    switch (keycode) {
        case KEY_S:
            if (notes) notes_save_open(edit);
            else textedit_save(edit, true);
            return true;
        case KEY_N:
            if (!notes) return false;
            notes_new(edit);
            return true;
        case KEY_O:
            /* Notes edits only its own notes; the sidebar opens them. */
            if (!notes && last_opened_path[0]) {
                textedit_load_file(edit, last_opened_path);
            }
            return true;
        case KEY_C:
            if (edit->sel_active) {
                textedit_copy_selection(edit, text_clipboard, sizeof(text_clipboard));
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
                textedit_copy_selection(edit, text_clipboard, sizeof(text_clipboard));
                textedit_delete_selection(edit);
                strcpy(edit->status, "Cut");
            } else {
                strncpy(text_clipboard, edit->lines[edit->cursor_line],
                        sizeof(text_clipboard) - 1);
                text_clipboard[sizeof(text_clipboard) - 1] = '\0';
                textedit_delete_line(edit, edit->cursor_line);
                strcpy(edit->status, "Cut line");
            }
            if (notes) notes_lib.last_edit_ms = timer_get_ticks();
            return true;
        case KEY_V:
            if (text_clipboard[0]) {
                textedit_insert_line(edit, edit->cursor_line + 1, text_clipboard);
                strcpy(edit->status, "Pasted line");
            }
            if (notes) notes_lib.last_edit_ms = timer_get_ticks();
            return true;
        default:
            return false;
    }
}

void compositor_handle_key(KeyCode keycode, char ascii, uint8_t modifiers)
{
    last_input_ms = timer_get_ticks();
    last_modifiers = modifiers;
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
                int created = users_create(login_username, fn, login_password, true);
                if (created == 0) {
                    users_login(login_username, login_password);
                    login_password[0] = '\0';
                    login_confirm[0] = '\0';
                    login_status[0] = '\0';
                    login_create_mode = false;
                    login_selected_user = users_current_index();
                    compositor_on_user_switched();
                } else if (created == -4) {
                    strcpy(login_status, "Only an administrator can add accounts. Log in first.");
                    login_status_ok = false;
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
        if (handle_app_shortcut(&app_states[active_window_index], keycode, modifiers)) {
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

    /* Finder owns Cmd+C (copy file) when focused, like the editors above. */
    bool finder_focused = overlay == OVERLAY_NONE && active_window_index >= 0 &&
                          active_window_index < window_count &&
                          app_states[active_window_index].type == APP_FINDER;
    if (shortcuts && (modifiers & INPUT_MOD_SUPER) && keycode == KEY_C && !finder_focused) {
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
                SearchResult res = spotlight_results[spotlight_selected];
                overlay_set(OVERLAY_NONE);
                spotlight_activate(&res);
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
            if ((modifiers & INPUT_MOD_CTRL) && keycode == KEY_C) {
                netcmd_cancel(terminal_net_print);
                return;
            }
            terminal_handle_key(&state->terminal, ascii, keycode);
        } else if (state->type == APP_TEXTEDIT) {
            /* Cmd shortcuts were already handled by handle_app_shortcut */
            if (textedit_key_allowed(&state->textedit, keycode))
                textedit_handle_key(&state->textedit, ascii, keycode, modifiers);
        } else if (state->type == APP_NOTES) {
            notes_handle_key(&state->notes, ascii, keycode, modifiers);
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
                if (vfs_join_path(dst, sizeof(dst), finder->path, base) != 0) return;
                if (!finder->clip_cut && unique_copy_path(dst, sizeof(dst), finder->path, base) != 0) return;
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
                /* Like macOS: Delete moves to the Trash; inside the Trash it
                 * removes the item for good (folders included). */
                FinderEntry *entry = &finder->entries[finder->selected];
                char path[256];
                if (vfs_join_path(path, sizeof(path), finder->path, entry->name) == 0 &&
                    (path_in_trash(finder->path) ? delete_tree(path, 0) : move_to_trash(path)) == 0) {
                    finder->needs_refresh = true;
                }
                return;
            }
            if ((modifiers & INPUT_MOD_SUPER) && (modifiers & INPUT_MOD_SHIFT) && keycode == KEY_N) {
                char dst[256];
                if (unique_copy_path(dst, sizeof(dst), finder->path, "untitled folder") == 0 && vfs_mkdir(dst) == 0) {
                    finder_refresh(finder);
                    for (int i = 0; i < finder->entry_count; i++)
                        if (strcmp(finder->entries[i].name, vfs_basename(dst)) == 0) finder->selected = i;
                    strncpy(finder->rename_buffer, vfs_basename(dst), sizeof(finder->rename_buffer) - 1);
                    finder->rename_buffer[sizeof(finder->rename_buffer) - 1] = '\0';
                    finder->rename_mode = true;
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
            browser_key(keycode, ascii, modifiers);
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
            if (keycode == KEY_ENTER) ascii = '\n';
            else if (keycode == KEY_ESCAPE) ascii = 'c';
            else if (keycode == KEY_BACKSPACE) ascii = '\b';
            calculator_input(&state->calc, calculator_key_for_char(ascii));
        }
    }
}

bool compositor_overlay_active(void)
{
    return overlay != OVERLAY_NONE;
}

void compositor_handle_mouse(int x, int y, bool down, bool up)
{
    last_input_ms = timer_get_ticks();
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
                            if (vfs_join_path(dst, sizeof(dst), dst_dir, base) == 0 &&
                                vfs_rename(finder->drag_path, dst) == 0) {
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
                            if (vfs_join_path(dst, sizeof(dst), dst_dir, base) == 0 &&
                                vfs_rename(finder->drag_path, dst) == 0) {
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
            int count = CTX_ITEMS;
            int mx, my, w, h;
            ctx_menu_rect(&mx, &my, &w, &h);
            ctx_menu_open = false;
            if (x >= mx && x <= mx + w && y >= my + 6 && y < my + 6 + count * MENU_ROW_H) {
                int item = (y - (my + 6)) / MENU_ROW_H;
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
                    app_open_window(APP_SETTINGS, "Settings");
                    if (app_window_index[APP_SETTINGS] >= 0)
                        app_states[app_window_index[APP_SETTINGS]].settings.page = SETTINGS_PAGE_WALLPAPER;
                } else if (item == 6) {
                    toggle_appearance();
                } else if (item == 7) {
                    users_lock_session();
                    login_password[0] = '\0';
                    login_status[0] = '\0';
                }
                return;
            }
        }

        if (active_menu > 0) {
            const char *mitems[8];
            int mx;
            int mcount = menu_items(active_menu, mitems, &mx);
            int mw = menu_panel_width(mitems, mcount);
            int my = MENU_BAR_HEIGHT + 4;
            if (x >= mx && x <= mx + mw && y >= my + 6 && y < my + 6 + mcount * MENU_ROW_H) {
                int item = (y - (my + 6)) / MENU_ROW_H;
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
                            if (st->type == APP_TEXTEDIT) textedit_save(&st->textedit, true);
                            else if (st->type == APP_NOTES) notes_save_open(&st->notes);
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
                        toggle_appearance();
                    } else if (item == 1) {
                        settings_toggle_reduced_transparency();
                    } else if (item == 2) {
                        overlay_set(OVERLAY_MISSION_CONTROL);
                    } else if (item == 3) {
                        overlay_set(OVERLAY_LAUNCHPAD);
                    }
                } else if (menu == 5) {
                    if (item == 0 && active_window_index >= 0 && active_window_index < window_count) {
                        ghost_capture(active_window_index, GHOST_MINIMIZE);
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
            MenuBarLayout L;
            menu_bar_layout(&L);
            if (x >= L.logo_x - 8 && x < L.logo_x + L.logo_w + 8) {
                active_menu = (active_menu == 1) ? 0 : 1;
                overlay_set(OVERLAY_NONE);
                return;
            }
            for (int i = 0; i < 4; i++) {
                if (x >= L.title_x[i] - 10 && x < L.title_x[i] + L.title_w[i] + 10) {
                    active_menu = (active_menu == i + 2) ? 0 : i + 2;
                    overlay_set(OVERLAY_NONE);
                    return;
                }
            }
            /* Wi-Fi glyph and the Control Center glyph/clock open Control Center */
            if ((net_info()->present && x >= L.wifi_x - 6 && x < L.wifi_x + 22) || x >= L.cc_x - 8) {
                active_menu = 0;
                overlay_set(overlay == OVERLAY_CONTROL_CENTER ? OVERLAY_NONE : OVERLAY_CONTROL_CENTER);
                return;
            }
            if (x >= L.user_x - 8 && x < L.user_x + L.user_w + 8) {
                active_menu = 0;
                overlay_set(OVERLAY_NONE);
                app_open_window(APP_SETTINGS, "Settings");
                if (app_window_index[APP_SETTINGS] >= 0) {
                    app_states[app_window_index[APP_SETTINGS]].settings.page = SETTINGS_PAGE_USERS;
                }
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
                    SearchResult res = spotlight_results[idx];
                    overlay_set(OVERLAY_NONE);
                    spotlight_activate(&res);
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
                int btn_y = panel_y + CONTROL_CENTER_HEIGHT - 44;
                int btn_w = (CONTROL_CENTER_WIDTH - 40) / 2;
                int btn_h = 28;
                int tile = -1;
                for (int i = 0; i < 4; i++) {
                    int tx, ty;
                    cc_tile_rect(panel_x, panel_y, i, &tx, &ty);
                    if (x >= tx && x < tx + CC_TILE_W && y >= ty && y < ty + CC_TILE_H) tile = i;
                }
                mark_dirty();
                if (tile == 0) {
                    if (net_info()->present) set_wifi(!net_info()->enabled);
                    else { strcpy(cc_message, "No network adapter in this VM"); cc_message_until = timer_get_ticks() + 3000; }
                } else if (tile == 1) {
                    strcpy(cc_message, "No Bluetooth hardware in this VM");
                    cc_message_until = timer_get_ticks() + 3000;
                } else if (tile == 2) {
                    toggle_appearance();
                } else if (tile == 3) {
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
                    if (windows[i].minimized) {
                        windows[i].anim_open = 0; windows[i].animating = true;
                        windows[i].anim_start_ms = 0; windows[i].anim_frames = 0;
                    }
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

        {
            DockLayout L;
            dock_layout(&L);
            if (L.count && x >= L.bg_x && x <= L.bg_x + L.bg_w && y >= L.bg_y - 40 && y <= L.bg_y + L.bg_h) {
                for (int i = 0; i < L.count; i++) {
                    if (x >= L.x[i] && x < L.x[i] + L.size[i] &&
                        y >= L.bottom - L.size[i] && y <= L.bg_y + L.bg_h) {
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
                    if (hit_traffic_light(win, 0, x, y)) {
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
                    } else if (hit_traffic_light(win, 1, x, y)) {
                        ghost_capture((int)(win - windows), GHOST_MINIMIZE);
                        win->minimized = true;
                        return;
                    } else if (hit_traffic_light(win, 2, x, y)) {
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

                    if (state->type == APP_WEB && browser_titlebar_click(x, y)) {
                        if (i != window_count - 1) raise_window_to_top(i);
                        return;
                    }

                    /* Finder's unified titlebar toolbar (was unreachable). */
                    if (state->type == APP_FINDER && y >= win->y + 8 && y <= win->y + 28) {
                        int tb_x = finder_toolbar_x(win, win->x);
                        if (x >= tb_x && x < tb_x + 22) { finder_back(&state->finder); return; }
                        if (x >= tb_x + 24 && x < tb_x + 46) { finder_forward(&state->finder); return; }
                        if (x >= tb_x + 52 && x < tb_x + 104) {
                            state->finder.view_mode = (state->finder.view_mode == FINDER_VIEW_LIST)
                                ? FINDER_VIEW_ICON : FINDER_VIEW_LIST;
                            return;
                        }
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

                    {
                        /* Empty Trash button (preview pane, Trash folder only) */
                        int content_w = win->w - 24, content_h = win->h - 52, preview_w = 180;
                        int px = content_x + content_w - preview_w;
                        char trash[160];
                        trash_dir(trash, sizeof(trash));
                        if (strcmp(state->finder.path, trash) == 0 && x >= px + 12 && x <= px + preview_w - 12 &&
                            y >= content_y + content_h - 36 && y <= content_y + content_h - 12) {
                            empty_trash();
                            state->finder.needs_refresh = true;
                            state->finder.selected = -1;
                            return;
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
                        } else if (rel_y >= 132 && rel_y < 152) {
                            char tpath[160];
                            trash_dir(tpath, sizeof(tpath));
                            if (!vfs_isdir(tpath)) vfs_mkdir(tpath);
                            finder_set_path(&state->finder, tpath);
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
                        int main_w = (win->w - 24) - sidebar_w - 10;
                        int row = -1;
                        for (int r = 0; r < APPEARANCE_ROWS; r++) {
                            int ry = content_y + APPEARANCE_ROW_Y(r);
                            if (y >= ry && y < ry + 26 && x >= main_x + 8 && x <= main_x + main_w - 8) row = r;
                        }
                        if (state->settings.page == SETTINGS_PAGE_APPEARANCE) {
                            appearance_page_click(main_x, main_w, content_y, row, x, y);
                        }
                        if (state->settings.page == SETTINGS_PAGE_NETWORK && net_info()->present) {
                            if (row == 0) set_wifi(!net_info()->enabled);
                            /* "Renew Lease" button sits below the detail lines */
                            int lines = net_info()->configured ? 4 : 2;
                            int by = content_y + APPEARANCE_ROW_Y(0) + 34 + lines * 18 + 8;
                            if (x >= main_x + 16 && x <= main_x + 136 && y >= by && y < by + 22 && net_info()->enabled) {
                                net_renew();
                                mark_dirty();
                            }
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
                        if (state->settings.page == SETTINGS_PAGE_WALLPAPER)
                            wallpaper_page_click(&state->settings, main_x, main_w, content_y, content_h, x, y);
                        if (state->settings.page == SETTINGS_PAGE_DOCK) {
                            int sx = seg_x(main_x, main_w);
                            int si = segmented_hit(sx, content_y + APPEARANCE_ROW_Y(0) + 1, 3, x, y);
                            int mi = segmented_hit(sx, content_y + APPEARANCE_ROW_Y(1) + 1, 3, x, y);
                            if (si >= 0) dock_apply(si, dock_magnify_level());
                            if (mi >= 0) dock_apply(dock_size_index(), mi);
                        }
                        if (state->settings.page == SETTINGS_PAGE_KEYBOARD && row == 0) {
                            settings_get()->shortcuts_enabled = !settings_get()->shortcuts_enabled;
                            settings_save();
                        }
                        if (state->settings.page == SETTINGS_PAGE_MOUSE) {
                            int mi = segmented_hit(seg_x(main_x, main_w), content_y + APPEARANCE_ROW_Y(0) + 1, 4, x, y);
                            if (mi >= 0) { settings_get()->mouse_speed = (uint8_t)(mi + 1); settings_save(); }
                        }
                    }
                } else if (state->type == APP_CALCULATOR) {
                    int content_x = win->x + 12;
                    int content_y = win->y + 40;
                    CalculatorLayout layout;
                    if (calculator_layout(win->w - 24, win->h - 52, &layout))
                        calculator_input(&state->calc, calculator_hit(&layout, x - content_x, y - content_y));
                } else if (state->type == APP_CALENDAR) {
                    CalendarState *cal = &state->calendar;
                    int content_x = win->x + 12;
                    int content_y = win->y + 40;
                    int content_w = win->w - 24;
                    int content_h = win->h - 52;
                    int sidebar_w = 160;
                    int agenda_w = 200;
                    int header_h = CALENDAR_HEADER_H;

                    int header_x = content_x + sidebar_w + 10;
                    int header_w = content_w - sidebar_w - agenda_w - 20;

                    if (y >= content_y + 10 && y <= content_y + 30) {
                        int nav_left = header_x + header_w - 98;
                        int nav_right = header_x + header_w - 74;
                        int today_x = header_x + header_w - 50;
                        if (x >= nav_left && x <= nav_left + 20) {
                            calendar_shift_month(cal, -1);
                            return;
                        }
                        if (x >= nav_right && x <= nav_right + 20) {
                            calendar_shift_month(cal, 1);
                            return;
                        }
                        if (x >= today_x && x <= today_x + 46) {
                            RtcTime now;
                            rtc_read_time(&now);
                            cal->year = now.year;
                            cal->month = now.month;
                            cal->day = now.day;
                            cal->selected_day = now.day;
                            return;
                        }
                    }

                    int view_x = header_x + 6;
                    if (y >= content_y + header_h - 18 && y <= content_y + header_h - 2) {
                        for (int i = 0; i < 4; i++) {
                            int vx = view_x + i * 50;
                            if (x >= vx && x <= vx + 48) {
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
                        int row = (y - (content_y + 30)) / 38;      /* two-line agenda rows */
                        /* Only the 6 rows that are actually drawn are
                         * clickable (the old row < count bound allowed
                         * selecting an event from uninitialized memory) */
                        int shown = MIN(count, 6);
                        if (row >= 0 && row < shown) {
                            cal->selected_event = indices[row];
                        }
                    }
                } else if (state->type == APP_NOTES) {
                    notes_click(&state->notes, win->x + 12, win->y + 40, win->w - 24, win->h - 52, x, y);
                } else if (state->type == APP_PREVIEW) {
                    preview_click(&state->preview, win->x + 12, win->y + 40, win->w - 24, win->h - 52, x, y);
                } else if (state->type == APP_WEB) {
                    browser_click(x, y);
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

void compositor_handle_mouse_absolute(int32_t x, int32_t y)
{
    pointer_absolute = true;
    last_input_ms = timer_get_ticks();
    int old_y = cursor_y;
    int nx = MAX(0, MIN(x, (int)comp_width - 1));
    int ny = MAX(0, MIN(y, (int)comp_height - 1));
    if (nx == cursor_x && ny == cursor_y) return;
    cursor_x = nx; cursor_y = ny;
    /* A cursor-only move never invalidates glass/windows. Hover visuals,
     * dragging and resizing still need a scene frame. */
    if (!login_screen_visible() && (dragging || resizing || active_menu || ctx_menu_open ||
        old_y >= (int)comp_height - DOCK_HEIGHT - 20 - DOCK_HOVER_RADIUS ||
        cursor_y >= (int)comp_height - DOCK_HEIGHT - 20 - DOCK_HOVER_RADIUS)) mark_dirty();
    for (int i = 0; i < window_count; i++) {
        if (app_states[i].type == APP_FINDER && app_states[i].finder.drag_active) mark_dirty();
    }
    {
        /* Traffic-light hover shows the window-button symbols. */
        static int last_tl = -1;
        int tl = -1;
        for (int i = window_count - 1; i >= 0 && tl < 0 && !login_screen_visible(); i--) {
            CompositorWindow *w = &windows[i];
            if (w->minimized) continue;
            if (cursor_x >= w->x + TL_X(0) - 4 && cursor_x < w->x + TL_X(2) + TL_SIZE + 4 &&
                cursor_y >= w->y + TL_TOP - 4 && cursor_y < w->y + TL_TOP + TL_SIZE + 4) tl = i;
            if (cursor_x >= w->x && cursor_x < w->x + w->w && cursor_y >= w->y && cursor_y < w->y + w->h) break;
        }
        if (tl != last_tl) { last_tl = tl; mark_dirty(); }
    }
    /* Browser: show the hovered link's address in its status bar. */
    if (active_window_index >= 0 && active_window_index < window_count &&
        app_states[active_window_index].type == APP_WEB && !windows[active_window_index].minimized &&
        browser_hover(cursor_x, cursor_y)) mark_dirty();

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

void compositor_handle_mouse_move(int32_t dx, int32_t dy)
{
    int speed = MAX(1, MIN(settings_get()->mouse_speed, 4));
    int64_t x = (int64_t)cursor_x + (int64_t)dx * speed;
    int64_t y = (int64_t)cursor_y + (int64_t)dy * speed;
    compositor_handle_mouse_absolute((int32_t)MAX(0, MIN(x, (int)comp_width - 1)),
                                     (int32_t)MAX(0, MIN(y, (int)comp_height - 1)));
    pointer_absolute = false;            /* relative mice keep the speed setting */
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
    last_input_ms = timer_get_ticks();
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
            if (cursor_x >= win->x + 12 && cursor_x < win->x + 12 + notes_sidebar_w(win->w - 24)) {
                notes_lib.scroll = MAX(0, notes_lib.scroll + (delta > 0 ? 1 : -1));   /* clamped when drawn */
                break;
            }
            state->notes.scroll += delta;
            if (state->notes.scroll < 0) state->notes.scroll = 0;
            break;
        case APP_CALENDAR:
            /* No scrollable region in the calendar yet */
            break;
        case APP_SETTINGS:
            if (state->settings.page == SETTINGS_PAGE_WALLPAPER)
                state->settings.wp_scroll = MAX(0, MIN(state->settings.wp_scroll + delta * 16,
                                                       state->settings.wp_scroll_max));
            break;
        case APP_WEB:
            browser_scroll(delta * 40);
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
    last_modifiers = modifiers;
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

/* Pointer pump: drawing a frame takes tens of ms (every window is redrawn),
 * and the main loop only reads input between frames, so the cursor used to
 * move once per frame. pointer_pump() runs between frame stages: it polls the
 * pointer devices and moves the cursor overlay to the newest absolute
 * position right away. Events stay queued, in order, for the main loop. */
static void (*pointer_poll_fn)(void);
static uint64_t ptr_last_pump_ms;
static uint32_t ptr_gap_max;             /* longest time the pointer went unread (ms) */

void compositor_set_pointer_poll(void (*poll)(void))
{
    pointer_poll_fn = poll;
}

/* Sleeping (wfi/hlt) between loop turns adds wake-up latency - under a
 * hypervisor on a busy host one "1 ms" sleep measured 6-12 ms, sometimes
 * 47 ms - so while the user is interacting or something is moving, the
 * main loop should wait without sleeping. Idle desktops still sleep. */
bool compositor_wants_fast_loop(uint64_t now_ms)
{
    return now_ms - last_input_ms < 250 || need_redraw || last_tick_animating;
}

static void pointer_pump_at(int where)
{
    uint64_t now = timer_get_ticks();
    if (ptr_last_pump_ms && now - ptr_last_pump_ms > ptr_gap_max) ptr_gap_max = (uint32_t)(now - ptr_last_pump_ms);
    if (ptr_last_pump_ms && now - ptr_last_pump_ms >= 100)
        serial_printf("[PERF] pointer stall %d ms before line %d\n", (int)(now - ptr_last_pump_ms), where);
    ptr_last_pump_ms = now;
    if (pointer_poll_fn) pointer_poll_fn();
    if (pointer_absolute) {
        int32_t mx, my;
        input_get_mouse_position(&mx, &my);
        fb_cursor_move(MAX(0, MIN(mx, (int)comp_width - 1)), MAX(0, MIN(my, (int)comp_height - 1)));
    } else {
        fb_cursor_move(cursor_x, cursor_y);
    }
}

#define pointer_pump() pointer_pump_at(__LINE__)

/* Frame-time receipts: "[PERF]" serial lines (tests/qemu_perf.py). */
static uint32_t perf_frames, perf_max_ms;
static uint64_t perf_total_ms, perf_window_start;

static void perf_record_frame(uint64_t start_ms, uint64_t end_ms)
{
    uint32_t ms = (uint32_t)(end_ms - start_ms);
    perf_frames++;
    perf_total_ms += ms;
    if (ms > perf_max_ms) perf_max_ms = ms;
    if (!perf_window_start) perf_window_start = start_ms;
    if (end_ms - perf_window_start >= 5000) {
        serial_printf("[PERF] frames=%d avg_ms=%d max_ms=%d ptr_gap_max_ms=%d\n", (int)perf_frames,
                      (int)(perf_total_ms / perf_frames), (int)perf_max_ms, (int)ptr_gap_max);
        ptr_gap_max = 0;
        perf_frames = perf_max_ms = 0;
        perf_total_ms = 0;
        perf_window_start = 0;
    }
}

static bool update_animations(uint64_t now_ms)
{
    bool changed = false;
    bool no_motion = settings_get()->reduced_motion;
    /* Progress advances by elapsed time, so a slow frame skips ahead
     * instead of stretching the animation (window open 240 ms, overlays 200 ms). */
    static uint64_t last_ms;
    int dt = last_ms ? (int)MIN(now_ms - last_ms, 100) : 16;
    last_ms = now_ms;
    int win_step = MAX(1, dt * 1000 / 240);

    for (int i = 0; i < window_count; i++) {
        CompositorWindow *win = &windows[i];
        if (win->animating) {
            changed = true;
            if (!win->anim_start_ms) win->anim_start_ms = now_ms;
            win->anim_frames++;
            if (no_motion) {
                win->anim_open = 1000;
                win->animating = false;
            } else {
                win->anim_open += win_step;
                if (win->anim_open >= 1000) {
                    win->anim_open = 1000;
                    win->animating = false;
                    serial_printf("[PERF] open '%s' frames=%d ms=%d\n", win->title,
                                  (int)win->anim_frames, (int)(now_ms - win->anim_start_ms));
                }
            }
        }
    }

    int step = no_motion ? 1000 : MAX(1, dt * 1000 / 200);
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
    if (!changed) last_ms = 0;      /* next animation starts from a normal first step */
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
    /* the topmost window under the pointer gets the right click first */
    for (int i = window_count - 1; i >= 0; i--) {
        CompositorWindow *win = &windows[i];
        if (win->minimized || x < win->x || x >= win->x + win->w || y < win->y || y >= win->y + win->h) continue;
        if (app_states[i].type == APP_WEB && browser_right_click(x, y)) {
            active_window_index = i;
            if (i != window_count - 1) raise_window_to_top(i);
            return;
        }
        break;
    }
    ctx_menu_open = true;
    ctx_menu_x = x;
    ctx_menu_y = y;
}

void compositor_tick(uint64_t now_ms)
{
    static bool login_frame_visible;
    /* Pointer overlay is independent of the 30fps scene/animation budget. */
    pointer_pump();
    if (netcmd_busy()) netcmd_poll(terminal_net_print);
    browser_poll();
    notes_autosave_tick(now_ms);
    for (int i = 0; i < window_count; i++)
        if (app_states[i].type == APP_CALCULATOR && calculator_expire(&app_states[i].calc, now_ms)) mark_dirty();
    pointer_pump();
    {
        /* Network status shows in the menu bar, Control Center and Settings:
         * repaint when it changes (DHCP finishes in the background). */
        static uint32_t last_sig;
        static uint64_t last_counters_ms;
        const NetInfo *ni = net_info();
        uint32_t sig = ((uint32_t)ni->present | ((uint32_t)ni->enabled << 1) | ((uint32_t)ni->configured << 2)) ^ ni->ip;
        if (sig != last_sig) { last_sig = sig; mark_dirty(); }
        bool showing_counters = overlay == OVERLAY_CONTROL_CENTER ||
            (app_window_index[APP_SETTINGS] >= 0 &&
             app_states[app_window_index[APP_SETTINGS]].settings.page == SETTINGS_PAGE_NETWORK);
        if (showing_counters && now_ms - last_counters_ms > 1000) { last_counters_ms = now_ms; mark_dirty(); }
        if (cc_message[0] && now_ms >= cc_message_until && overlay == OVERLAY_CONTROL_CENTER) { cc_message[0] = '\0'; mark_dirty(); }
    }
    uint64_t delta = now_ms - last_frame_ms;
    if (delta < 16) {               /* up to ~60 fps when frames are cheap */
        return;
    }

    last_frame_ms = now_ms;
    int focused_type = (active_window_index >= 0 && active_window_index < window_count)
                       ? (int)app_states[active_window_index].type : -1;
    proc_tick(delta > 100 ? 33 : delta, focused_type);
    bool anim_changed = update_animations(now_ms);

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
    bool animating = anim_changed || any_dock_bounce(now_ms) || ghost.kind != GHOST_NONE ||
                     now_ms < anim_keepalive_until;
    last_tick_animating = animating;
    if (!need_redraw && !animating) {
        return;
    }
    need_redraw = false;
    uint64_t frame_start = timer_get_ticks();

    /* Login -> desktop: the back buffer still holds the last login frame;
     * keep it as a full-screen ghost that fades out over the desktop. */
    if (login_frame_visible && !login_screen_visible()) ghost_capture_screen();

    draw_wallpaper();
    pointer_pump();

    if (login_screen_visible()) {
        draw_login_screen();
        pointer_pump();
        fb_present();
        pointer_pump();
        login_frame_visible = true;
        return;
    }

    draw_desktop_icons();  /* Desktop sits under windows */

    if (anim_launchpad > 0) {
        draw_launchpad(anim_launchpad);
    } else if (mission_control_active || anim_mission_control > 0) {
        for (int i = 0; i < window_count; i++) {
            if (!windows[i].minimized) {
                draw_window_animated(i);
                pointer_pump();
            }
        }
        draw_mission_control(anim_mission_control);
    } else {
        for (int i = 0; i < window_count; i++) {
            if (!windows[i].minimized) {
                draw_window_animated(i);
                pointer_pump();
            }
        }
    }
    draw_ghost(GHOST_CLOSE, now_ms);

    draw_menu_bar();
    pointer_pump();

    if (anim_launchpad == 0) {
        draw_dock(now_ms);
        pointer_pump();
    }
    draw_ghost(GHOST_MINIMIZE, now_ms);

    if (anim_spotlight > 0) {
        draw_spotlight(anim_spotlight);
    }
    if (anim_control_center > 0) {
        draw_control_center(anim_control_center);
    }
    if (anim_app_switcher > 0) {
        draw_app_switcher(anim_app_switcher);
    }

    if (active_menu <= 0 && !ctx_menu_open) menu_open_key = 0;
    draw_menu_dropdown();
    draw_context_menu();
    draw_ghost(GHOST_SCREEN, now_ms);
    pointer_pump();

    fb_present();
    pointer_pump();
    perf_record_frame(frame_start, timer_get_ticks());
    if (login_frame_visible) {
        serial_printf("[UI] Desktop presented\n");
        login_frame_visible = false;
    }
}
