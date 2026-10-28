/*
 * ojjyOS v3 - Web browser internals shared by browser.c (tabs, loading,
 * input), browser_ui.c (drawing), browser_profile.c (bookmarks, history,
 * cookies, settings) and browser_pages.c (built-in pages).
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_BROWSER_INT_H
#define _OJJY_BROWSER_INT_H

#include "types.h"
#include "web/url.h"
#include "web/http.h"
#include "web/engine.h"
#include "../drivers/input.h"

#define MAX_TABS      12
#define TAB_HIST      40
#define PAGE_CAP      (4u << 20)
#define SHEET_CAP     (6u << 20)
#define RES_SLOTS     4
#define RES_CAP       (3u << 20)
#define IMG_POOL      (96u << 20)
#define MAX_SUGG      8
#define MAX_MARKS     1000

/* Geometry inside the window (logical px). */
#define TAB_X0        86
#define TAB_TOP       7
#define TAB_H         31
#define TOOLBAR_Y     40
#define TOOLBAR_H     40
#define BMBAR_H       30

enum { SHEET_NONE = 0, SHEET_LOADING, SHEET_DONE, SHEET_FAILED };
enum { MENU_NONE = 0, MENU_MAIN, MENU_CONTEXT, MENU_SELECT, MENU_PAGE_INFO };
enum { RES_SHEET = 1, RES_IMAGE };

typedef struct {
    bool used;
    Url url;
    char title[128];
    Url hist[TAB_HIST];
    int hist_scroll[TAB_HIST];
    int hist_count, hist_pos;
    /* page source (raw response body) */
    uint8_t *buf;
    size_t src_len;
    bool plain, latin1, have_page;
    int http_status;
    bool secure;
    char suite[40];
    bool verified;              /* https certificate checked (NET-02) */
    bool cert_warning;          /* showing "This connection is not private" */
    char cert_error[96];        /* problem accepted with "Visit anyway", or the warning's reason */
    char cert_subject[64], cert_issuer[64], cert_root[64];
    char ctype[64];
    /* stylesheet texts, by the document's sheet_urls index */
    uint8_t *sbuf;
    size_t sused;
    uint32_t sheet_off[WEB_MAX_SHEETS], sheet_len[WEB_MAX_SHEETS];
    uint8_t sheet_state[WEB_MAX_SHEETS];
    char sheet_base[WEB_MAX_SHEETS][256];       /* directory URL of each sheet */
    /* loading */
    bool loading;
    HttpFetch *fetch;
    uint64_t started, arrived_ms;
    bool replay;
    bool want_source;           /* view-source: */
    int restore_scroll;
    char post_body[2048];
    /* view */
    int scroll;
    int zoom;
    char status[160];
    char error[96];
} Tab;

typedef struct {
    int kind;                   /* 0 = search, 1 = history, 2 = bookmark, 3 = url */
    char title[128];
    char url[URL_TEXT_MAX];
} Suggestion;

typedef struct {
    bool opened;
    Tab tabs[MAX_TABS];
    int count, active;
    Url closed[10];
    int closed_count;
    /* the active tab's document */
    WebDoc *doc;
    int doc_tab;
    bool doc_parsed, doc_laid_out, layout_dirty;
    uint64_t layout_ms;
    int layout_w;
    /* window rect from the last draw */
    int x, y, w, h;
    int cx, cy, cw, ch;         /* page area */
    bool focused_window;
    /* omnibox */
    bool omni_focus;
    char omni[URL_TEXT_MAX];
    int omni_caret;
    bool omni_all;
    Suggestion sugg[MAX_SUGG];
    int sugg_count, sugg_sel;
    /* page focus */
    int field;                  /* focused form field or -1 */
    int field_caret;
    /* find in page */
    bool find_open;
    char find[128];
    int32_t marks[MAX_MARKS * 3];
    int mark_count, mark_cur;
    /* menus */
    int menu;
    int menu_x, menu_y, menu_hover;
    int menu_link;              /* context menu target link (-1) */
    int menu_field;             /* select popup field */
    /* hover */
    int hover_link;
    char hover_url[URL_TEXT_MAX];
    int hover_tab, hover_close, hover_button;
    uint64_t zoom_bubble_until;
    bool show_bmbar;
} Browser;

extern Browser *BR;

/* browser.c */
Tab *active_tab(void);
void br_navigate(Tab *t, const Url *u, bool replay);
void br_go_text(Tab *t, const char *text);
void br_new_tab(const char *url, bool activate);
void br_close_tab(int i);
void br_activate(int i);
void br_reload(Tab *t);
void br_stop(Tab *t);
void br_history_go(Tab *t, int delta);
void br_set_zoom(int zoom);
void br_find_update(void);
void br_view_source(void);
int br_content_top(void);
void br_follow_link(int link, bool new_tab);
void br_omnibox_suggest(void);
const char *br_search_url(void);
void br_menu_action(int menu, int item);

/* browser_ui.c */
void ui_draw_browser(int x, int y, int w, int h);
int ui_menu_items(int menu, const char **labels, int max);
int ui_menu_hit(int x, int y);
void ui_menu_rect(int *x, int *y, int *w, int *h);
int ui_tab_hit(int x, int y, bool *close);
int ui_toolbar_hit(int x, int y);           /* button id or -1 */
int ui_bookmark_hit(int x, int y);
int ui_sugg_hit(int x, int y);
bool ui_find_hit(int x, int y, int *button);
enum { TB_BACK = 1, TB_FWD, TB_RELOAD, TB_HOME, TB_OMNI, TB_STAR, TB_MENU, TB_NEWTAB, TB_LOCK };

/* browser_profile.c */
typedef struct { char title[96]; char url[URL_TEXT_MAX]; } Bookmark;
typedef struct { char title[96]; char url[URL_TEXT_MAX]; uint32_t when; uint16_t visits; } HistEntry;
typedef struct { char name[96]; char path[160]; uint32_t size; uint32_t when; } Download;
void profile_load(void);
void profile_save_soon(void);
void profile_poll(void);
int bookmark_count(void);
const Bookmark *bookmark_get(int i);
int bookmark_find(const char *url);
void bookmark_add(const char *title, const char *url);
void bookmark_remove(int i);
void history_add(const char *title, const char *url);
int history_count(void);
const HistEntry *history_get(int i);         /* 0 = newest */
void history_clear(void);
int download_count(void);
const Download *download_get(int i);
void download_add(const char *name, const char *path, uint32_t size);
int setting_search(void);
void setting_set_search(int i);
bool setting_bmbar(void);
void setting_set_bmbar(bool on);
void cookies_clear(void);
int cookies_count(void);
uint32_t profile_now(void);                 /* seconds since 1970 (RTC) */
const char *profile_dir(void);
void profile_hooks_install(void);

/* browser_pages.c */
size_t page_newtab(char *out, size_t cap);
size_t page_history(char *out, size_t cap);
size_t page_bookmarks(char *out, size_t cap);
size_t page_downloads(char *out, size_t cap);
size_t page_settings(char *out, size_t cap);
size_t page_error(char *out, size_t cap, const char *url, const char *reason, bool https);
/* "This connection is not private": reason, Go Back, Visit anyway (NET-02) */
size_t page_cert_warning(char *out, size_t cap, const char *url, const char *host, const char *reason);
size_t page_needs_js(char *out, size_t cap, const char *url);
size_t page_image(char *out, size_t cap, const char *url);
extern const char *const search_names[];
extern const char *const search_urls[];
#define SEARCH_ENGINES 4

#endif /* _OJJY_BROWSER_INT_H */
