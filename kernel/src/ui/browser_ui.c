/*
 * ojjyOS v3 - ojjyOS Web drawing: tab strip in the title bar, toolbar with
 * the omnibox, bookmarks bar, the page, menus, find bar and bubbles. Every
 * hit test here uses the same geometry as the drawing code.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "ui/browser_int.h"
#include "ui/draw.h"
#include "uifont.h"
#include "webfont.h"
#include "web/paint.h"
#include "framebuffer.h"
#include "string.h"
#include "net/net.h"

static GfxSurface *surf(void)
{
    static GfxSurface s;
    fb_get_surface(&s);
    return &s;
}

static bool dark(void) { return ((ui_theme()->text >> 16) & 0xFF) > 128; }
static Color toolbar_bg(void) { return dark() ? RGB(41, 44, 52) : RGB(255, 255, 255); }
static Color omni_bg(void) { return dark() ? RGB(28, 31, 38) : RGB(238, 241, 245); }
static Color fg(void) { return ui_theme()->text; }
static Color muted(void) { return ui_theme()->text_muted; }
static Color accent(void) { return ui_theme()->accent; }

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

/* Thick-ish line made of small round dots (icons only). */
static void line(int x0, int y0, int x1, int y1, Color c, uint8_t a)
{
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (steps == 0) steps = 1;
    for (int i = 0; i <= steps; i++) {
        int x = x0 + dx * i / steps, y = y0 + dy * i / steps;
        ui_rounded_rect(x - 1, y - 1, 2, 2, 1, c, a);
    }
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

static int tab_order[MAX_TABS];
static int tab_n;

static void tab_layout(int *x0, int *tw)
{
    tab_n = 0;
    for (int i = 0; i < MAX_TABS; i++) if (BR->tabs[i].used) tab_order[tab_n++] = i;
    int avail = BR->w - TAB_X0 - 44;
    int w = tab_n ? avail / tab_n : avail;
    if (w > 236) w = 236;
    if (w < 44) w = 44;
    *x0 = BR->x + TAB_X0;
    *tw = w;
}

int ui_tab_hit(int x, int y, bool *close)
{
    *close = false;
    if (!BR || !in_rect(y, 0, BR->y + TAB_TOP, 0, TAB_H, 1)) return -1;
    if (y < BR->y + TAB_TOP || y >= BR->y + TAB_TOP + TAB_H) return -1;
    int x0, tw;
    tab_layout(&x0, &tw);
    for (int k = 0; k < tab_n; k++) {
        int tx = x0 + k * tw;
        if (x >= tx && x < tx + tw) {
            int cxp = tx + tw - 22;
            bool show_close = tw > 70 || tab_order[k] == BR->active;
            *close = show_close && x >= cxp - 2 && x < cxp + 16;
            return tab_order[k];
        }
    }
    return -1;
}

static void omni_rect(int *x, int *y, int *w, int *h)
{
    *x = BR->x + 140;
    *y = BR->y + TOOLBAR_Y + 6;
    *w = BR->w - 140 - 52;
    *h = TOOLBAR_H - 12;
}

int ui_toolbar_hit(int x, int y)
{
    if (!BR) return -1;
    int x0, tw;
    tab_layout(&x0, &tw);
    int nx = x0 + tab_n * tw + 6;
    if (in_rect(x, y, nx, BR->y + TAB_TOP + 3, 26, 26)) return TB_NEWTAB;
    int ty = BR->y + TOOLBAR_Y;
    if (y < ty || y >= ty + TOOLBAR_H) return -1;
    if (in_rect(x, y, BR->x + 8, ty + 6, 30, 28)) return TB_BACK;
    if (in_rect(x, y, BR->x + 40, ty + 6, 30, 28)) return TB_FWD;
    if (in_rect(x, y, BR->x + 72, ty + 6, 30, 28)) return TB_RELOAD;
    if (in_rect(x, y, BR->x + 104, ty + 6, 30, 28)) return TB_HOME;
    int ox, oy, ow, oh;
    omni_rect(&ox, &oy, &ow, &oh);
    if (in_rect(x, y, ox + ow - 30, oy, 30, oh)) return TB_STAR;
    if (in_rect(x, y, ox + 4, oy, 26, oh) && !BR->omni_focus) return TB_LOCK;
    if (in_rect(x, y, ox, oy, ow, oh)) return TB_OMNI;
    if (in_rect(x, y, BR->x + BR->w - 44, ty + 6, 34, 28)) return TB_MENU;
    return -1;
}

static int bm_x[64], bm_w[64], bm_n;

static void bm_layout(void)
{
    bm_n = 0;
    int x = BR->x + 12;
    int limit = BR->x + BR->w - 12;
    for (int i = 0; i < bookmark_count() && bm_n < 64; i++) {
        const Bookmark *b = bookmark_get(i);
        int tw = text_width(b->title, TEXT_SMALL);
        if (tw > 140) tw = 140;
        int w = tw + 36;
        if (x + w > limit) break;
        bm_x[bm_n] = x;
        bm_w[bm_n] = w;
        bm_n++;
        x += w + 4;
    }
}

int ui_bookmark_hit(int x, int y)
{
    if (!BR || !BR->show_bmbar) return -1;
    int by = BR->y + TOOLBAR_Y + TOOLBAR_H;
    if (y < by || y >= by + BMBAR_H) return -1;
    bm_layout();
    for (int i = 0; i < bm_n; i++) if (x >= bm_x[i] && x < bm_x[i] + bm_w[i]) return i;
    return -1;
}

int ui_sugg_hit(int x, int y)
{
    int ox, oy, ow, oh;
    omni_rect(&ox, &oy, &ow, &oh);
    int top = oy + oh + 4;
    for (int i = 0; i < BR->sugg_count; i++)
        if (in_rect(x, y, ox, top + 8 + i * 36, ow, 36)) return i;
    return -1;
}

static void find_rect(int *x, int *y, int *w, int *h)
{
    *w = 360;
    *h = 40;
    *x = BR->x + BR->w - *w - 12;
    *y = BR->cy + 6;
}

bool ui_find_hit(int x, int y, int *button)
{
    int fx, fy, fw, fh;
    find_rect(&fx, &fy, &fw, &fh);
    *button = 0;
    if (!in_rect(x, y, fx, fy, fw, fh)) return false;
    int bx = fx + fw - 96;
    if (x >= bx && x < bx + 30) *button = 1;
    else if (x >= bx + 30 && x < bx + 60) *button = 2;
    else if (x >= bx + 62) *button = 3;
    return true;
}

/* ---- Menus ---- */

int ui_menu_items(int menu, const char **labels, int max)
{
    int n = 0;
#define ADD(s) do { if (n < max) labels[n++] = (s); } while (0)
    Tab *t = active_tab();
    if (menu == MENU_MAIN) {
        ADD("New Tab");
        ADD("Reopen Closed Tab");
        ADD("-");
        ADD("History");
        ADD("Bookmarks");
        ADD("Downloads");
        ADD("-");
        ADD("Zoom In");
        ADD("Zoom Out");
        ADD("Actual Size");
        ADD("Find...");
        ADD("View Page Source");
        ADD("Bookmark This Page");
        ADD(setting_bmbar() ? "Hide Bookmarks Bar" : "Show Bookmarks Bar");
        ADD("-");
        ADD("Settings");
        ADD("About ojjyOS Web");
    } else if (menu == MENU_CONTEXT) {
        if (BR->menu_link >= 0) {
            ADD("Open Link in New Tab");
            ADD("Copy Link Address");
            ADD("-");
        }
        ADD("Back");
        ADD("Forward");
        ADD("Reload");
        ADD("-");
        ADD("Bookmark This Page");
        ADD("View Page Source");
    } else if (menu == MENU_PAGE_INFO) {
        (void)t;
    }
#undef ADD
    return n;
}

#define MENU_ROW 28
#define MENU_SEP 9

void ui_menu_rect(int *x, int *y, int *w, int *h)
{
    const char *labels[20];
    if (BR->menu == MENU_SELECT) {
        WebField *f = &BR->doc->fields[BR->menu_field];
        int widest = 120;
        for (int i = 0; i < f->option_count; i++) { int tw = text_width(f->options[i], TEXT_BODY) + 40; if (tw > widest) widest = tw; }
        *w = widest > 420 ? 420 : widest;
        int rows = f->option_count > 14 ? 14 : f->option_count;
        *h = rows * MENU_ROW + 12;
        /* below the field */
        *x = BR->x + 40;
        *y = BR->cy + 40;
        Tab *t = active_tab();
        for (int k = 0; t && k < BR->doc->item_count; k++) {
            const WebItem *it = &BR->doc->items[k];
            if (it->kind == WI_FIELD && it->ref == BR->menu_field) {
                *x = BR->cx + it->x;
                *y = BR->cy + it->y - t->scroll + it->h + 4;
                break;
            }
        }
        if (*y + *h > BR->y + BR->h - 8) *y = BR->y + BR->h - 8 - *h;
        if (*x + *w > BR->x + BR->w - 8) *x = BR->x + BR->w - 8 - *w;
        return;
    }
    if (BR->menu == MENU_PAGE_INFO) {
        int ox, oy, ow, oh;
        omni_rect(&ox, &oy, &ow, &oh);
        *x = ox;
        *y = oy + oh + 6;
        *w = 380;
        *h = 132;
        return;
    }
    int n = ui_menu_items(BR->menu, labels, 20);
    int height = 12, widest = 180;
    for (int i = 0; i < n; i++) {
        height += labels[i][0] == '-' ? MENU_SEP : MENU_ROW;
        int tw = text_width(labels[i], TEXT_BODY) + 72;
        if (tw > widest) widest = tw;
    }
    *w = widest;
    *h = height;
    if (BR->menu == MENU_MAIN) {
        *x = BR->x + BR->w - widest - 10;
        *y = BR->y + TOOLBAR_Y + TOOLBAR_H - 4;
    } else {
        *x = BR->menu_x;
        *y = BR->menu_y;
        if (*x + *w > BR->x + BR->w - 6) *x = BR->x + BR->w - 6 - *w;
        if (*y + *h > BR->y + BR->h - 6) *y = BR->y + BR->h - 6 - *h;
    }
}

int ui_menu_hit(int x, int y)
{
    int mx, my, mw, mh;
    ui_menu_rect(&mx, &my, &mw, &mh);
    if (!in_rect(x, y, mx, my, mw, mh)) return -1;
    if (BR->menu == MENU_SELECT) {
        int i = (y - my - 6) / MENU_ROW;
        WebField *f = &BR->doc->fields[BR->menu_field];
        int first = f->selected > 13 ? f->selected - 13 : 0;
        i += first;
        return i >= 0 && i < f->option_count ? i : -1;
    }
    if (BR->menu == MENU_PAGE_INFO) return -1;
    const char *labels[20];
    int n = ui_menu_items(BR->menu, labels, 20);
    int cy = my + 6;
    for (int i = 0; i < n; i++) {
        int h = labels[i][0] == '-' ? MENU_SEP : MENU_ROW;
        if (y >= cy && y < cy + h) return labels[i][0] == '-' ? -1 : i;
        cy += h;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Pieces                                                              */
/* ------------------------------------------------------------------ */

static Color site_color(const char *s)
{
    static const Color colors[] = {RGB(26, 115, 232), RGB(232, 113, 10), RGB(24, 128, 56), RGB(217, 48, 37),
                                   RGB(147, 52, 230), RGB(0, 123, 131), RGB(197, 34, 31), RGB(25, 103, 210)};
    unsigned h = 0;
    for (; *s; s++) h = h * 31 + (unsigned char)*s;
    return colors[h % 8];
}

/* The site's main name: "en.wikipedia.org" -> "wikipedia", "bbc.co.uk" -> "bbc". */
static void site_name(const char *host, char *out, size_t size)
{
    const char *labels[16];
    size_t lens[16];
    int n = 0;
    const char *p = host;
    while (*p && n < 16) {
        labels[n] = p;
        while (*p && *p != '.') p++;
        lens[n] = (size_t)(p - labels[n]);
        n++;
        if (*p) p++;
    }
    int pick = n >= 2 ? n - 2 : 0;
    if (n >= 3 && lens[n - 1] == 2 && lens[n - 2] <= 3) pick = n - 3;       /* co.uk, com.au */
    size_t k = lens[pick] < size - 1 ? lens[pick] : size - 1;
    memcpy(out, labels[pick], k);
    out[k] = '\0';
}

/* A site badge: coloured circle with the site's first letter. */
static void favicon(int x, int y, int size, const char *host, const char *title)
{
    char name[64];
    if (host && host[0] && strcmp(host, "newtab") != 0) site_name(host, name, sizeof(name));
    else {
        size_t k = 0;
        const char *src = host && strcmp(host, "newtab") == 0 ? "ojjyOS" : title ? title : "?";
        while (src[k] && k < sizeof(name) - 1) { name[k] = src[k]; k++; }
        name[k] = '\0';
    }
    char letter[2] = {name[0] ? name[0] : '?', 0};
    if (letter[0] >= 'a' && letter[0] <= 'z') letter[0] = (char)(letter[0] - 32);
    ui_rounded_rect(x, y, size, size, size / 2, site_color(name), 255);
    int tw = text_width(letter, TEXT_SMALL_BOLD);
    text_draw(surf(), x + (size - tw) / 2, text_y_center(y, size, TEXT_SMALL_BOLD), letter, TEXT_SMALL_BOLD, COLOR_WHITE);
}

static void spinner(int cx, int cy, int r)
{
    int phase = (int)((net_now() / 60) % 12);
    for (int i = 0; i < 12; i++) {
        static const int dx[12] = {0, 50, 87, 100, 87, 50, 0, -50, -87, -100, -87, -50};
        static const int dy[12] = {-100, -87, -50, 0, 50, 87, 100, 87, 50, 0, -50, -87};
        int a = ((i - phase + 12) % 12) * 18 + 40;
        ui_rounded_rect(cx + dx[i] * r / 100 - 1, cy + dy[i] * r / 100 - 1, 3, 3, 1, accent(), (uint8_t)(a > 255 ? 255 : a));
    }
}

static void draw_tabs(void)
{
    int x0, tw;
    tab_layout(&x0, &tw);
    int ty = BR->y + TAB_TOP;
    for (int k = 0; k < tab_n; k++) {
        int i = tab_order[k];
        Tab *t = &BR->tabs[i];
        int tx = x0 + k * tw;
        bool active = i == BR->active;
        bool hover = i == BR->hover_tab;
        if (active) {
            /* the active tab merges into the toolbar below */
            ui_rounded_rect(tx, ty, tw, TAB_H + 8, 10, toolbar_bg(), 255);
            /* little concave feet like Chrome */
            ui_rounded_rect(tx - 8, ty + TAB_H - 8, 8, 8, 0, toolbar_bg(), 255);
            ui_rounded_rect(tx + tw, ty + TAB_H - 8, 8, 8, 0, toolbar_bg(), 255);
        } else if (hover) {
            ui_rounded_rect(tx + 2, ty + 3, tw - 4, TAB_H - 6, 8, dark() ? COLOR_WHITE : RGB(255, 255, 255), dark() ? 30 : 110);
        } else if (k + 1 < tab_n && tab_order[k + 1] != BR->active) {
            ui_rounded_rect(tx + tw - 1, ty + 9, 1, TAB_H - 18, 0, muted(), 90);
        }
        int ix = tx + 12, iy = ty + (TAB_H - 16) / 2;
        if (t->loading) spinner(ix + 8, iy + 8, 6);
        else favicon(ix, iy, 16, t->url.host, t->title);
        int title_x = ix + 24;
        bool show_close = tw > 70 || active;
        int title_w = tw - (title_x - tx) - (show_close ? 30 : 8);
        if (title_w > 8)
            text_draw_fit(surf(), title_x, text_y_center(ty, TAB_H, TEXT_BODY), t->title[0] ? t->title : "New Tab",
                          TEXT_BODY, active ? fg() : muted(), title_w);
        if (show_close) {
            int cxp = tx + tw - 22, cyp = ty + TAB_H / 2;
            if (BR->hover_close == i) ui_rounded_rect(cxp - 2, cyp - 9, 18, 18, 9, muted(), 60);
            line(cxp + 3, cyp - 4, cxp + 11, cyp + 4, muted(), 230);
            line(cxp + 3, cyp + 4, cxp + 11, cyp - 4, muted(), 230);
        }
    }
    /* new tab button */
    int nx = x0 + tab_n * tw + 6, ny = ty + 3;
    if (BR->hover_button == TB_NEWTAB) ui_rounded_rect(nx, ny, 26, 26, 13, muted(), 50);
    ui_rounded_rect(nx + 7, ny + 12, 12, 2, 1, fg(), 230);
    ui_rounded_rect(nx + 12, ny + 7, 2, 12, 1, fg(), 230);
}

static void icon_button_bg(int x, int y, int id)
{
    if (BR->hover_button == id) ui_rounded_rect(x, y, 30, 28, 14, muted(), 45);
}

static void draw_toolbar(void)
{
    Tab *t = active_tab();
    int ty = BR->y + TOOLBAR_Y;
    int bar_h = TOOLBAR_H + (BR->show_bmbar ? BMBAR_H : 0);
    ui_rounded_rect(BR->x + 1, ty, BR->w - 2, bar_h, 0, toolbar_bg(), 255);
    ui_rounded_rect(BR->x + 1, ty + bar_h - 1, BR->w - 2, 1, 0, muted(), 60);
    /* back / forward / reload / home */
    bool can_back = t && t->hist_pos > 0, can_fwd = t && t->hist_pos + 1 < t->hist_count;
    int by = ty + 6;
    int cy = by + 14;
    icon_button_bg(BR->x + 8, by, TB_BACK);
    int bx = BR->x + 23;
    Color c = can_back ? fg() : muted();
    uint8_t a = can_back ? 255 : 110;
    line(bx - 6, cy, bx + 6, cy, c, a);
    line(bx - 6, cy, bx - 1, cy - 5, c, a);
    line(bx - 6, cy, bx - 1, cy + 5, c, a);
    icon_button_bg(BR->x + 40, by, TB_FWD);
    bx = BR->x + 55;
    c = can_fwd ? fg() : muted();
    a = can_fwd ? 255 : 110;
    line(bx - 6, cy, bx + 6, cy, c, a);
    line(bx + 6, cy, bx + 1, cy - 5, c, a);
    line(bx + 6, cy, bx + 1, cy + 5, c, a);
    icon_button_bg(BR->x + 72, by, TB_RELOAD);
    bx = BR->x + 87;
    if (t && t->loading) {
        line(bx - 5, cy - 5, bx + 5, cy + 5, fg(), 255);
        line(bx - 5, cy + 5, bx + 5, cy - 5, fg(), 255);
    } else {
        gfx_arc_up(surf(), bx, cy, 5, 7, 140, fg(), 255);
        ui_rounded_rect(bx + 3, cy - 8, 6, 2, 1, fg(), 255);
        ui_rounded_rect(bx + 7, cy - 8, 2, 6, 1, fg(), 255);
    }
    icon_button_bg(BR->x + 104, by, TB_HOME);
    bx = BR->x + 119;
    for (int k = 0; k < 6; k++) ui_rounded_rect(bx - k - 1, cy - 7 + k, 2 + 2 * k, 2, 1, fg(), 255);
    ui_rounded_rect(bx - 5, cy - 1, 10, 8, 1, fg(), 255);
    ui_rounded_rect(bx - 1, cy + 3, 3, 4, 1, toolbar_bg(), 255);
    /* omnibox */
    int ox, oy, ow, oh;
    omni_rect(&ox, &oy, &ow, &oh);
    GfxSurface *s = surf();
    if (BR->omni_focus) {
        ui_rounded_rect(ox, oy, ow, oh, oh / 2, dark() ? RGB(24, 26, 32) : COLOR_WHITE, 255);
        gfx_round_rect_stroke(s, ox, oy, ow, oh, oh / 2, accent(), 255);
        gfx_round_rect_stroke(s, ox - 1, oy - 1, ow + 2, oh + 2, oh / 2 + 1, accent(), 120);
        const char *txt = BR->omni;
        int room = ow - 60;
        int tx = ox + 16, tyy = text_y_center(oy, oh, TEXT_BODY);
        /* keep the caret visible */
        int caret_w = text_width_n(txt, BR->omni_caret, TEXT_BODY);
        int shift = caret_w > room ? caret_w - room : 0;
        fb_set_clip(ox + 10, oy, ow - 46, oh);
        if (BR->omni_all && txt[0]) ui_rounded_rect(tx - 2 - shift, oy + 6, text_width(txt, TEXT_BODY) + 4, oh - 12, 3, accent(), 70);
        text_draw(s, tx - shift, tyy, txt[0] ? txt : "", TEXT_BODY, fg());
        if (!txt[0]) text_draw(s, tx, tyy, "Search or type a URL", TEXT_BODY, muted());
        if (!BR->omni_all && ((net_now() / 530) % 2 == 0)) ui_rounded_rect(tx - shift + caret_w, oy + 7, 2, oh - 14, 1, accent(), 255);
        fb_reset_clip();
    } else {
        ui_rounded_rect(ox, oy, ow, oh, oh / 2, omni_bg(), 255);
        if (BR->hover_button == TB_OMNI) ui_rounded_rect(ox, oy, ow, oh, oh / 2, muted(), 25);
        int tx = ox + 34;
        /* security chip */
        int lx = ox + 10, ly = oy + oh / 2;
        bool https = t && strcmp(t->url.scheme, "https") == 0;
        bool http = t && strcmp(t->url.scheme, "http") == 0;
        if (https) {
            ui_rounded_rect(lx + 2, ly - 1, 10, 8, 2, muted(), 255);
            gfx_arc_up(s, lx + 7, ly - 1, 2, 4, 90, muted(), 255);
        } else if (http) {
            text_draw(s, lx, text_y_center(oy, oh, TEXT_SMALL_BOLD), "Not secure", TEXT_SMALL_BOLD, muted());
            tx = ox + 22 + text_width("Not secure", TEXT_SMALL_BOLD);
            ui_rounded_rect(tx - 9, oy + 8, 1, oh - 16, 0, muted(), 120);
        } else {
            gfx_round_rect_stroke(s, lx + 1, ly - 7, 14, 14, 7, muted(), 255);
            ui_rounded_rect(lx + 7, ly - 1, 2, 5, 1, muted(), 255);
            ui_rounded_rect(lx + 7, ly - 4, 2, 2, 1, muted(), 255);
        }
        bool newtab = !t || !t->url.scheme[0] || (strcmp(t->url.scheme, "ojjy") == 0 && strcmp(t->url.host, "newtab") == 0);
        if (newtab) {
            text_draw(s, tx, text_y_center(oy, oh, TEXT_BODY), "Search or type a URL", TEXT_BODY, muted());
        } else if (strcmp(t->url.scheme, "ojjy") == 0) {
            char shown[URL_TEXT_MAX] = "ojjy://";
            size_t k = strlen(shown);
            for (size_t j = 0; t->url.host[j] && k + 1 < sizeof(shown); j++) shown[k++] = t->url.host[j];
            shown[k] = '\0';
            text_draw_fit(s, tx, text_y_center(oy, oh, TEXT_BODY), shown, TEXT_BODY, fg(), ow - (tx - ox) - 40);
        } else {
            /* host in full colour, the rest muted (Chrome style) */
            const char *host = t->url.host;
            int hw = text_width(host, TEXT_BODY);
            text_draw_fit(s, tx, text_y_center(oy, oh, TEXT_BODY), host, TEXT_BODY, fg(), ow - (tx - ox) - 40);
            int room = ow - (tx - ox) - 40 - hw;
            if (room > 20 && strcmp(t->url.path, "/") != 0)
                text_draw_fit(s, tx + hw, text_y_center(oy, oh, TEXT_BODY), t->url.path, TEXT_BODY, muted(), room);
        }
    }
    /* star */
    {
        char u[URL_TEXT_MAX];
        u[0] = '\0';
        if (t && t->url.scheme[0]) url_format(&t->url, u, sizeof(u), false);
        bool marked = u[0] && bookmark_find(u) >= 0;
        int sx = ox + ow - 22, sy = oy + oh / 2;
        static const int px[10] = {0, 23, 95, 38, 59, 0, -59, -38, -95, -23};
        static const int py[10] = {-100, -32, -31, 12, 81, 40, 81, 12, -31, -32};
        int r = 8;
        Color sc = marked ? accent() : muted();
        if (marked)
            for (int k = 0; k < 7; k++) ui_rounded_rect(sx - (k < 4 ? k : 7 - k) - 2, sy - 3 + k, 4 + 2 * (k < 4 ? k : 7 - k), 1, 0, sc, 255);
        for (int k = 0; k < 10; k++) {
            int k2 = (k + 1) % 10;
            line(sx + px[k] * r / 100, sy + py[k] * r / 100, sx + px[k2] * r / 100, sy + py[k2] * r / 100, sc, 255);
        }
    }
    /* menu (three dots) */
    int mx = BR->x + BR->w - 44;
    if (BR->hover_button == TB_MENU || BR->menu == MENU_MAIN) ui_rounded_rect(mx + 2, by, 30, 28, 14, muted(), 45);
    for (int k = 0; k < 3; k++) ui_rounded_rect(mx + 15, by + 7 + k * 6, 4, 4, 2, fg(), 255);
    /* bookmarks bar */
    if (BR->show_bmbar) {
        bm_layout();
        int y = ty + TOOLBAR_H + 3;
        for (int i = 0; i < bm_n; i++) {
            const Bookmark *b = bookmark_get(i);
            Url u;
            const char *host = url_parse(b->url, &u) ? u.host : b->title;
            favicon(bm_x[i] + 8, y + 5, 14, host, b->title);
            text_draw_fit(surf(), bm_x[i] + 28, text_y_center(y, BMBAR_H - 6, TEXT_SMALL), b->title, TEXT_SMALL, fg(), bm_w[i] - 34);
        }
        if (!bookmark_count()) text_draw(surf(), BR->x + 14, text_y_center(y, BMBAR_H - 6, TEXT_SMALL),
                                         "Bookmark pages with \xE2\x8C\x98" "D to see them here", TEXT_SMALL, muted());
    }
}

/* ---- Form controls on the page ---- */

static void draw_field(void *ctx, GfxSurface *s, const WebItem *it, int x, int y)
{
    (void)ctx;
    if (it->ref < 0 || it->ref >= BR->doc->field_count) return;
    WebField *f = &BR->doc->fields[it->ref];
    bool focused = BR->field == it->ref;
    uint32_t color = it->color & 0xFFFFFF;
    switch (f->kind) {
    case FIELD_CHECKBOX:
        gfx_round_rect(s, x, y, it->w, it->h, 3, f->checked ? 0x1A73E8 : 0xFFFFFF, 255);
        if (!f->checked) gfx_round_rect_stroke(s, x, y, it->w, it->h, 3, 0x767676, 255);
        else {
            line(x + 3, y + it->h / 2, x + it->w / 2 - 1, y + it->h - 4, COLOR_WHITE, 255);
            line(x + it->w / 2 - 1, y + it->h - 4, x + it->w - 3, y + 3, COLOR_WHITE, 255);
        }
        return;
    case FIELD_RADIO:
        gfx_round_rect(s, x, y, it->w, it->h, it->w / 2, 0xFFFFFF, 255);
        gfx_round_rect_stroke(s, x, y, it->w, it->h, it->w / 2, f->checked ? 0x1A73E8 : 0x767676, 255);
        if (f->checked) gfx_round_rect(s, x + 3, y + 3, it->w - 6, it->h - 6, (it->w - 6) / 2, 0x1A73E8, 255);
        return;
    case FIELD_SUBMIT: case FIELD_BUTTON: case FIELD_RESET:
        if (f->node && f->node->tag == TAG_BUTTON) return;       /* its content is the page's own */
        {
            int tw = webfont_width(f->label, (int)strlen(f->label), it->font);
            webfont_draw(s, x + (it->w - tw) / 2, y + (it->h - webfont_height(it->font)) / 2, f->label, (int)strlen(f->label),
                         it->font, color, f->disabled ? 120 : 255);
        }
        return;
    case FIELD_SELECT: {
        const char *t = f->selected >= 0 && f->selected < f->option_count ? f->options[f->selected] : "";
        int saved[4] = {s->clip_x1, s->clip_y1, s->clip_x2, s->clip_y2};
        int k = s->scale > 1 ? s->scale : 1;
        if (s->clip_x2 > (x + it->w - 16) * k) s->clip_x2 = (x + it->w - 16) * k;
        webfont_draw(s, x + 2, y + (it->h - webfont_height(it->font)) / 2, t, (int)strlen(t), it->font, color, 255);
        s->clip_x1 = saved[0]; s->clip_y1 = saved[1]; s->clip_x2 = saved[2]; s->clip_y2 = saved[3];
        int ax = x + it->w - 11, ay = y + it->h / 2;
        line(ax - 4, ay - 2, ax, ay + 2, color | 0xFF000000u, 255);
        line(ax, ay + 2, ax + 4, ay - 2, color | 0xFF000000u, 255);
        return;
    }
    default:
        break;
    }
    /* text fields */
    char shown[512];
    const char *v = f->value;
    bool placeholder = !v[0];
    if (placeholder) v = f->placeholder;
    size_t n = 0;
    for (size_t i = 0; v[i] && n + 1 < sizeof(shown); i++) {
        char c = v[i];
        if (f->kind == FIELD_PASSWORD && !placeholder) c = '*';
        if (c == '\n') c = ' ';
        shown[n++] = c;
    }
    shown[n] = '\0';
    int saved[4] = {s->clip_x1, s->clip_y1, s->clip_x2, s->clip_y2};
    int k = s->scale > 1 ? s->scale : 1;
    int cx1 = x * k, cx2 = (x + it->w) * k;
    if (s->clip_x1 < cx1) s->clip_x1 = cx1;
    if (s->clip_x2 > cx2) s->clip_x2 = cx2;
    int caret_px = focused ? webfont_width(shown, BR->field_caret < (int)n ? BR->field_caret : (int)n, it->font) : 0;
    int shift = caret_px > it->w - 4 ? caret_px - it->w + 4 : 0;
    int ty = f->kind == FIELD_TEXTAREA ? y + 2 : y + (it->h - webfont_height(it->font)) / 2;
    webfont_draw(s, x + 1 - shift, ty, shown, (int)n, it->font, placeholder ? 0x757575 : color, 255);
    if (focused && (net_now() / 530) % 2 == 0)
        gfx_round_rect(s, x + 1 - shift + caret_px, ty + 1, 1, webfont_height(it->font) - 2, 0, color, 255);
    s->clip_x1 = saved[0]; s->clip_y1 = saved[1]; s->clip_x2 = saved[2]; s->clip_y2 = saved[3];
    if (focused) gfx_round_rect_stroke(s, x - 3, y - 2, it->w + 6, it->h + 4, 4, 0x1A73E8, 200);
}

static void draw_page(void)
{
    Tab *t = active_tab();
    GfxSurface *s = surf();
    int x = BR->cx, y = BR->cy, w = BR->cw, h = BR->ch;
    if (!t || !BR->doc_laid_out || BR->doc_tab != BR->active) {
        ui_rounded_rect(x, y, w, h, 0, dark() ? RGB(29, 33, 41) : RGB(246, 248, 251), 255);
        if (t && t->loading) {
            const char *m = "Loading\xE2\x80\xA6";
            text_draw(s, x + (w - text_width(m, TEXT_BODY)) / 2, y + h / 3, m, TEXT_BODY, muted());
        }
    } else {
        WebPaintOpts o = {draw_field, NULL, BR->hover_link, BR->find_open ? BR->marks : NULL,
                          BR->find_open ? BR->mark_count : 0, BR->mark_cur};
        web_paint(s, BR->doc, x + 1, y, t->scroll, x, y, w, h, &o);
        /* scrollbar */
        if (BR->doc->height > h) {
            int track = h - 8;
            int thumb = (int)((int64_t)track * h / BR->doc->height);
            if (thumb < 28) thumb = 28;
            int max = BR->doc->height - h + 24;
            int sy = y + 4 + (max > 0 ? (int)((int64_t)(track - thumb) * t->scroll / max) : 0);
            ui_rounded_rect(x + w - 9, sy, 6, thumb, 3, RGB(128, 128, 128), 150);
        }
        if (BR->doc->needs_js) {
            int bh = 34;
            ui_rounded_rect(x, y, w, bh, 0, dark() ? RGB(60, 52, 28) : RGB(254, 247, 224), 245);
            ui_rounded_rect(x, y + bh - 1, w, 1, 0, RGB(220, 190, 110), 255);
            text_draw_fit(s, x + 14, text_y_center(y, bh, TEXT_BODY),
                          "This page builds itself with JavaScript, which ojjyOS Web doesn't run yet, so parts may be missing.",
                          TEXT_BODY, dark() ? RGB(250, 230, 180) : RGB(90, 70, 20), w - 28);
        }
    }
    /* progress bar */
    if (t && t->loading) {
        uint64_t el = net_now() - t->started;
        int pw = (int)(w * (el > 8000 ? 90 : 20 + (int)(el * 70 / 8000)) / 100);
        ui_rounded_rect(x, y, pw, 3, 1, accent(), 255);
    }
    /* hovered link: status bubble bottom-left */
    if (BR->hover_url[0]) {
        int bw = text_width(BR->hover_url, TEXT_SMALL) + 18;
        if (bw > w / 2) bw = w / 2;
        int by = y + h - 24;
        ui_rounded_rect(x + 2, by, bw, 22, 6, dark() ? RGB(50, 54, 62) : RGB(241, 243, 244), 245);
        gfx_round_rect_stroke(s, x + 2, by, bw, 22, 6, muted(), 60);
        text_draw_fit(s, x + 11, text_y_center(by, 22, TEXT_SMALL), BR->hover_url, TEXT_SMALL, fg(), bw - 18);
    } else if (t && t->loading) {
        char st[160] = "Waiting for ";
        size_t k = strlen(st);
        for (size_t j = 0; t->fetch && t->fetch->url.host[j] && k + 4 < sizeof(st); j++) st[k++] = t->fetch->url.host[j];
        st[k] = '\0';
        int bw = text_width(st, TEXT_SMALL) + 18;
        int by = y + h - 24;
        ui_rounded_rect(x + 2, by, bw, 22, 6, dark() ? RGB(50, 54, 62) : RGB(241, 243, 244), 245);
        text_draw(s, x + 11, text_y_center(by, 22, TEXT_SMALL), st, TEXT_SMALL, fg());
    }
}

static void glass_panel(int x, int y, int w, int h, int r)
{
    gfx_shadow(surf(), x, y, w, h, r, 14, 6, 0x000000, 60);
    ui_rounded_rect(x, y, w, h, r, dark() ? RGB(46, 50, 58) : RGB(252, 252, 253), 250);
    gfx_round_rect_stroke(surf(), x, y, w, h, r, muted(), 70);
}

static void draw_suggestions(void)
{
    if (!BR->omni_focus || !BR->sugg_count) return;
    int ox, oy, ow, oh;
    omni_rect(&ox, &oy, &ow, &oh);
    int top = oy + oh + 4;
    int h = BR->sugg_count * 36 + 16;
    glass_panel(ox, top, ow, h, 14);
    for (int i = 0; i < BR->sugg_count; i++) {
        Suggestion *s = &BR->sugg[i];
        int ry = top + 8 + i * 36;
        if (i == BR->sugg_sel) ui_rounded_rect(ox + 6, ry, ow - 12, 36, 10, accent(), 40);
        int ix = ox + 18, iy = ry + 10;
        if (s->kind == 0) {
            /* magnifier */
            gfx_round_rect_stroke(surf(), ix, iy, 12, 12, 6, muted(), 255);
            line(ix + 10, iy + 10, ix + 15, iy + 15, muted(), 255);
        } else if (s->kind == 1) {
            gfx_round_rect_stroke(surf(), ix, iy, 15, 15, 7, muted(), 255);
            line(ix + 7, iy + 3, ix + 7, iy + 8, muted(), 255);
            line(ix + 7, iy + 8, ix + 11, iy + 10, muted(), 255);
        } else if (s->kind == 2) {
            favicon(ix - 1, iy - 1, 16, NULL, s->title);
        } else {
            gfx_round_rect_stroke(surf(), ix, iy, 15, 15, 7, muted(), 255);
            ui_rounded_rect(ix + 1, iy + 7, 13, 1, 0, muted(), 255);
        }
        int tx = ox + 46;
        int tw = text_width(s->title, TEXT_BODY);
        int room = ow - 70;
        text_draw_fit(surf(), tx, text_y_center(ry, 36, TEXT_BODY), s->title, TEXT_BODY, fg(), room);
        if ((s->kind == 1 || s->kind == 2) && tw + 40 < room) {
            text_draw_fit(surf(), tx + tw + 12, text_y_center(ry, 36, TEXT_SMALL), s->url, TEXT_SMALL, accent(), room - tw - 12);
        }
    }
}

static void draw_menu(void)
{
    if (BR->menu == MENU_NONE) return;
    int mx, my, mw, mh;
    ui_menu_rect(&mx, &my, &mw, &mh);
    glass_panel(mx, my, mw, mh, 12);
    if (BR->menu == MENU_PAGE_INFO) {
        Tab *t = active_tab();
        bool https = t && strcmp(t->url.scheme, "https") == 0;
        const char *head = https ? "Connection is encrypted" : (t && strcmp(t->url.scheme, "http") == 0) ? "Your connection to this site is not secure" : "Built-in ojjyOS page";
        text_draw(surf(), mx + 18, my + 16, head, TEXT_BODY_BOLD, fg());
        if (https) {
            char line1[96] = "TLS 1.3 \xC2\xB7 ";
            size_t k = strlen(line1);
            for (size_t j = 0; t->suite[j] && k + 1 < sizeof(line1); j++) line1[k++] = t->suite[j];
            line1[k] = '\0';
            text_draw(surf(), mx + 18, my + 44, line1, TEXT_SMALL, muted());
            text_draw_fit(surf(), mx + 18, my + 66, "The certificate is not checked yet, so the site's", TEXT_SMALL, fg(), mw - 30);
            text_draw_fit(surf(), mx + 18, my + 84, "identity is not proven. Avoid entering passwords.", TEXT_SMALL, fg(), mw - 30);
        } else if (t && strcmp(t->url.scheme, "http") == 0) {
            text_draw_fit(surf(), mx + 18, my + 44, "Information you send or receive could be read or", TEXT_SMALL, fg(), mw - 30);
            text_draw_fit(surf(), mx + 18, my + 62, "changed by others on the network.", TEXT_SMALL, fg(), mw - 30);
        } else {
            text_draw_fit(surf(), mx + 18, my + 44, "This page is part of ojjyOS Web.", TEXT_SMALL, fg(), mw - 30);
        }
        char ck[64] = "Cookies stored: ";
        char n[12];
        utoa((uint64_t)cookies_count(), n, 10);
        size_t k = strlen(ck);
        for (size_t j = 0; n[j]; j++) ck[k++] = n[j];
        ck[k] = '\0';
        text_draw(surf(), mx + 18, my + 108, ck, TEXT_SMALL, muted());
        return;
    }
    if (BR->menu == MENU_SELECT) {
        WebField *f = &BR->doc->fields[BR->menu_field];
        int first = f->selected > 13 ? f->selected - 13 : 0;
        for (int i = first; i < f->option_count && i < first + 14; i++) {
            int ry = my + 6 + (i - first) * MENU_ROW;
            if (i == BR->menu_hover) ui_rounded_rect(mx + 5, ry, mw - 10, MENU_ROW, 7, accent(), 255);
            if (i == f->selected) text_draw(surf(), mx + 12, text_y_center(ry, MENU_ROW, TEXT_BODY), "\xE2\x9C\x93", TEXT_BODY,
                                            i == BR->menu_hover ? COLOR_WHITE : fg());
            text_draw_fit(surf(), mx + 30, text_y_center(ry, MENU_ROW, TEXT_BODY), f->options[i], TEXT_BODY,
                          i == BR->menu_hover ? COLOR_WHITE : fg(), mw - 40);
        }
        return;
    }
    const char *labels[20];
    int n = ui_menu_items(BR->menu, labels, 20);
    int cy = my + 6;
    for (int i = 0; i < n; i++) {
        if (labels[i][0] == '-') {
            ui_rounded_rect(mx + 12, cy + 4, mw - 24, 1, 0, muted(), 70);
            cy += MENU_SEP;
            continue;
        }
        bool hover = i == BR->menu_hover;
        if (hover) ui_rounded_rect(mx + 5, cy, mw - 10, MENU_ROW, 7, accent(), 255);
        text_draw(surf(), mx + 16, text_y_center(cy, MENU_ROW, TEXT_BODY), labels[i], TEXT_BODY, hover ? COLOR_WHITE : fg());
        const char *key = NULL;
        if (strcmp(labels[i], "New Tab") == 0) key = "\xE2\x8C\x98T";
        else if (strcmp(labels[i], "Reopen Closed Tab") == 0) key = "\xE2\x87\xA7\xE2\x8C\x98T";
        else if (strcmp(labels[i], "History") == 0) key = "\xE2\x8C\x98Y";
        else if (strcmp(labels[i], "Downloads") == 0) key = "\xE2\x87\xA7\xE2\x8C\x98J";
        else if (strcmp(labels[i], "Zoom In") == 0) key = "\xE2\x8C\x98+";
        else if (strcmp(labels[i], "Zoom Out") == 0) key = "\xE2\x8C\x98-";
        else if (strcmp(labels[i], "Actual Size") == 0) key = "\xE2\x8C\x98" "0";
        else if (strcmp(labels[i], "Find...") == 0) key = "\xE2\x8C\x98" "F";
        else if (strcmp(labels[i], "View Page Source") == 0) key = "\xE2\x8C\x98U";
        else if (strcmp(labels[i], "Bookmark This Page") == 0) key = "\xE2\x8C\x98" "D";
        else if (strcmp(labels[i], "Settings") == 0) key = "\xE2\x8C\x98,";
        else if (strcmp(labels[i], "Back") == 0) key = "\xE2\x8C\x98[";
        else if (strcmp(labels[i], "Forward") == 0) key = "\xE2\x8C\x98]";
        else if (strcmp(labels[i], "Reload") == 0) key = "\xE2\x8C\x98R";
        if (key) {
            int kw = text_width(key, TEXT_BODY);
            text_draw(surf(), mx + mw - kw - 16, text_y_center(cy, MENU_ROW, TEXT_BODY), key, TEXT_BODY, hover ? COLOR_WHITE : muted());
        }
        cy += MENU_ROW;
    }
}

static void draw_find(void)
{
    if (!BR->find_open) return;
    int fx, fy, fw, fh;
    find_rect(&fx, &fy, &fw, &fh);
    glass_panel(fx, fy, fw, fh, 12);
    GfxSurface *s = surf();
    int tyy = text_y_center(fy, fh, TEXT_BODY);
    fb_set_clip(fx + 10, fy, fw - 160, fh);
    if (BR->find[0]) text_draw(s, fx + 14, tyy, BR->find, TEXT_BODY, fg());
    else text_draw(s, fx + 14, tyy, "Find in page", TEXT_BODY, muted());
    int cw = text_width(BR->find, TEXT_BODY);
    if ((net_now() / 530) % 2 == 0) ui_rounded_rect(fx + 15 + cw, fy + 11, 2, fh - 22, 1, accent(), 255);
    fb_reset_clip();
    char count[24];
    if (BR->mark_count) {
        char a[8], b[8];
        utoa((uint64_t)(BR->mark_cur + 1), a, 10);
        utoa((uint64_t)BR->mark_count, b, 10);
        strcpy(count, a);
        size_t k = strlen(count);
        count[k++] = '/';
        strcpy(count + k, b);
    } else strcpy(count, BR->find[0] ? "0/0" : "");
    text_draw(s, fx + fw - 140, text_y_center(fy, fh, TEXT_SMALL), count, TEXT_SMALL, muted());
    int bx = fx + fw - 96, cy = fy + fh / 2;
    line(bx + 10, cy + 3, bx + 15, cy - 2, fg(), 255);
    line(bx + 15, cy - 2, bx + 20, cy + 3, fg(), 255);
    line(bx + 40, cy - 2, bx + 45, cy + 3, fg(), 255);
    line(bx + 45, cy + 3, bx + 50, cy - 2, fg(), 255);
    line(bx + 70, cy - 5, bx + 80, cy + 5, fg(), 255);
    line(bx + 70, cy + 5, bx + 80, cy - 5, fg(), 255);
}

static void draw_zoom_bubble(void)
{
    Tab *t = active_tab();
    if (!t || net_now() > BR->zoom_bubble_until) return;
    char z[16];
    utoa((uint64_t)t->zoom, z, 10);
    size_t k = strlen(z);
    z[k] = '%';
    z[k + 1] = '\0';
    int ox, oy, ow, oh;
    omni_rect(&ox, &oy, &ow, &oh);
    int w = 120, h = 40;
    int x = ox + ow - w, y = oy + oh + 6;
    glass_panel(x, y, w, h, 12);
    text_draw(surf(), x + (w - text_width(z, TEXT_TITLE)) / 2, text_y_center(y, h, TEXT_TITLE), z, TEXT_TITLE, fg());
    ui_mark_dirty();
}

void ui_draw_browser(int x, int y, int w, int h)
{
    (void)x; (void)y; (void)w; (void)h;
    draw_tabs();
    draw_toolbar();
    draw_page();
    draw_find();
    draw_suggestions();
    draw_menu();
    draw_zoom_bubble();
}
