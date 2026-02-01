/*
 * ojjyOS v3 Kernel - Desktop Services
 *
 * App registry, search indexing, settings, notifications.
 */

#ifndef _OJJY_UI_SERVICES_H
#define _OJJY_UI_SERVICES_H

#include "../types.h"
#include "../fs/bundle.h"

#define APP_REGISTRY_MAX 24
#define SEARCH_RESULTS_MAX 8

typedef struct {
    char name[BUNDLE_NAME_MAX];
    char bundle_id[BUNDLE_ID_MAX];
    char path[BUNDLE_PATH_MAX];
    Bundle bundle;
    BundleIcon icon;
    bool loaded;
    bool running;
    uint64_t bounce_until;
} AppInfo;

typedef enum {
    SEARCH_RESULT_APP = 0,
    SEARCH_RESULT_FILE,
} SearchResultType;

typedef struct {
    SearchResultType type;
    char title[64];
    char subtitle[128];
    char path[256];
    int app_index;
    int score;
} SearchResult;

typedef struct {
    bool dark_mode;
    bool wifi_enabled;
    bool bluetooth_enabled;
    uint8_t volume;
    uint8_t brightness;
    uint8_t dock_size;
    uint8_t dock_magnify;
    uint8_t mouse_speed;
    bool shortcuts_enabled;
    bool time_24h;
} SettingsState;

void app_registry_init(void);
int app_registry_count(void);
AppInfo *app_registry_get(int index);
int app_registry_launch(int index);
int app_registry_find_by_name(const char *name);
int app_registry_find_by_bundle_id(const char *bundle_id);

void search_index_init(void);
int search_index_query(const char *query, SearchResult *results, int max_results);

SettingsState *settings_get(void);
void settings_load(void);
void settings_save(void);
void settings_toggle_wifi(void);
void settings_toggle_bluetooth(void);
void settings_toggle_dark_mode(void);
void settings_toggle_time_format(void);
void settings_set_volume(uint8_t value);
void settings_set_brightness(uint8_t value);

void notifications_init(void);
void notifications_push(const char *message);
const char *notifications_latest(void);

#endif /* _OJJY_UI_SERVICES_H */
