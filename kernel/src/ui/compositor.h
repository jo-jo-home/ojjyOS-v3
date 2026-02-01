/*
 * ojjyOS v3 Kernel - Tahoe Compositor
 *
 * Minimal compositor for translucent, blurred glass windows.
 */

#ifndef _OJJY_UI_COMPOSITOR_H
#define _OJJY_UI_COMPOSITOR_H

#include "../types.h"
#include "../drivers/input.h"

typedef struct {
    int id;
    int app_type;
    int x;
    int y;
    int w;
    int h;
    int base_w;
    int base_h;
    uint8_t glass_level;
    uint8_t blur_level;
    uint8_t corner_level;
    bool active;
    bool demo;
    char title[32];

    /* Animation state (0-1000) */
    int anim_open;
    bool animating;
} CompositorWindow;

typedef struct {
    void (*on_create)(int id, int app_type, int x, int y, int w, int h);
    void (*on_destroy)(int id);
    void (*on_move)(int id, int x, int y);
    void (*on_resize)(int id, int w, int h);
    void (*on_focus)(int id);
} CompositorWmHooks;

void compositor_init(uint32_t width, uint32_t height);
void compositor_set_dark_mode(bool enabled);
void compositor_set_wallpaper(const char *path);
int compositor_create_window(const char *title, int x, int y, int w, int h);
void compositor_move_window(int id, int x, int y);
void compositor_resize_window(int id, int w, int h);
void compositor_set_demo(int id, bool demo);
void compositor_set_active_app(const char *name);
void compositor_open_default_apps(void);
void compositor_set_wm_hooks(const CompositorWmHooks *hooks);
int compositor_destroy_window(int id);
void compositor_handle_key(KeyCode keycode, char ascii, uint8_t modifiers);
void compositor_handle_mouse_move(int32_t dx, int32_t dy);
void compositor_handle_mouse(int x, int y, bool down, bool up);
bool compositor_overlay_active(void);
void compositor_tick(uint64_t now_ms);

#endif /* _OJJY_UI_COMPOSITOR_H */
