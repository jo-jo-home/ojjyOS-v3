/*
 * ojjyOS v3 Kernel - Desktop Services Implementation
 */

#include "services.h"
#include "../string.h"
#include "../fs/vfs.h"
#include "../serial.h"

static AppInfo app_registry[APP_REGISTRY_MAX];
static int app_count = 0;

static char file_index[16][256];
static int file_count = 0;

static SettingsState settings_state = {
    .dark_mode = false,
    .wifi_enabled = true,
    .bluetooth_enabled = false,
    .volume = 70,
    .brightness = 80,
    .dock_size = 36,
    .dock_magnify = 56,
    .mouse_speed = 2,
    .shortcuts_enabled = true,
    .time_24h = false,
};

static char latest_notification[128] = "";

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

static int parse_value(const char *buffer, const char *key, int fallback)
{
    char pattern[32];
    pattern[0] = '\0';
    str_append(pattern, sizeof(pattern), key);
    str_append(pattern, sizeof(pattern), "=");
    const char *pos = str_find(buffer, pattern);
    if (!pos) return fallback;
    pos += strlen(pattern);
    int value = 0;
    while (*pos >= '0' && *pos <= '9') {
        value = value * 10 + (*pos - '0');
        pos++;
    }
    return value;
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
            char hc = ascii_lower(haystack[i + j]);
            char nc = ascii_lower(needle[j]);
            if (hc != nc) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }

    return false;
}

static bool str_prefix_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || needle[0] == '\0') return false;

    while (*needle && *haystack) {
        if (ascii_lower(*needle) != ascii_lower(*haystack)) {
            return false;
        }
        needle++;
        haystack++;
    }
    return *needle == '\0';
}

void app_registry_init(void)
{
    app_count = 0;
    memset(app_registry, 0, sizeof(app_registry));

    Bundle bundles[APP_REGISTRY_MAX];
    int count = bundle_list_directory("/Applications", bundles, APP_REGISTRY_MAX);
    for (int i = 0; i < count; i++) {
        AppInfo *app = &app_registry[app_count++];
        app->bundle = bundles[i];
        app->loaded = true;
        app->running = false;
        app->bounce_until = 0;

        strncpy(app->name, bundles[i].manifest.name, BUNDLE_NAME_MAX - 1);
        strncpy(app->bundle_id, bundles[i].manifest.bundle_id, BUNDLE_ID_MAX - 1);
        strncpy(app->path, bundles[i].path, BUNDLE_PATH_MAX - 1);

        if (bundle_load_icon(&app->bundle, &app->icon) != 0) {
            app->icon.valid = false;
        }

        if (strcmp(app->bundle_id, "com.ojjyos.finder") == 0) {
            app->running = true;
        }
    }

    serial_printf("[UI] App registry initialized: %d app(s)\n", app_count);
}

int app_registry_count(void)
{
    return app_count;
}

AppInfo *app_registry_get(int index)
{
    if (index < 0 || index >= app_count) return NULL;
    return &app_registry[index];
}

int app_registry_find_by_name(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < app_count; i++) {
        if (strcmp(app_registry[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int app_registry_find_by_bundle_id(const char *bundle_id)
{
    if (!bundle_id) return -1;
    for (int i = 0; i < app_count; i++) {
        if (strcmp(app_registry[i].bundle_id, bundle_id) == 0) {
            return i;
        }
    }
    return -1;
}

int app_registry_launch(int index)
{
    if (index < 0 || index >= app_count) return -1;
    AppInfo *app = &app_registry[index];

    if (!app->loaded) {
        if (bundle_load(app->path, &app->bundle) != 0) {
            return -1;
        }
        app->loaded = true;
    }

    if (bundle_launch(&app->bundle) == 0) {
        app->running = true;
        return 0;
    }
    return -1;
}

void search_index_init(void)
{
    file_count = 0;
    memset(file_index, 0, sizeof(file_index));

    VfsDir *dir = vfs_opendir("/System/Wallpapers");
    if (!dir) return;

    VfsDirEntry entry;
    while (vfs_readdir(dir, &entry) == 0 && file_count < (int)ARRAY_SIZE(file_index)) {
        if (entry.type == VFS_TYPE_FILE) {
            char full[256];
            vfs_join_path(full, sizeof(full), "/System/Wallpapers", entry.name);
            strncpy(file_index[file_count++], full, sizeof(file_index[0]) - 1);
        }
    }
    vfs_closedir(dir);

    VfsDir *docs = vfs_opendir("/Users/guest/Documents");
    if (!docs) return;
    while (vfs_readdir(docs, &entry) == 0 && file_count < (int)ARRAY_SIZE(file_index)) {
        if (entry.type == VFS_TYPE_FILE) {
            char full[256];
            vfs_join_path(full, sizeof(full), "/Users/guest/Documents", entry.name);
            strncpy(file_index[file_count++], full, sizeof(file_index[0]) - 1);
        }
    }
    vfs_closedir(docs);
}

static void add_result(SearchResult *results, int *count, int max_results,
                       SearchResultType type, const char *title,
                       const char *subtitle, const char *path, int app_index, int score)
{
    if (*count >= max_results) return;
    SearchResult *res = &results[(*count)++];
    memset(res, 0, sizeof(*res));
    res->type = type;
    res->score = score;
    res->app_index = app_index;
    if (title) strncpy(res->title, title, sizeof(res->title) - 1);
    if (subtitle) strncpy(res->subtitle, subtitle, sizeof(res->subtitle) - 1);
    if (path) strncpy(res->path, path, sizeof(res->path) - 1);
}

static void sort_results(SearchResult *results, int count)
{
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (results[j].score < results[i].score) {
                SearchResult tmp = results[i];
                results[i] = results[j];
                results[j] = tmp;
            }
        }
    }
}

int search_index_query(const char *query, SearchResult *results, int max_results)
{
    if (!query || query[0] == '\0' || !results || max_results <= 0) {
        return 0;
    }

    int count = 0;

    for (int i = 0; i < app_count; i++) {
        AppInfo *app = &app_registry[i];
        if (str_prefix_ci(app->name, query)) {
            add_result(results, &count, max_results, SEARCH_RESULT_APP,
                       app->name, "Application", app->path, i, 0);
        } else if (str_contains_ci(app->name, query)) {
            add_result(results, &count, max_results, SEARCH_RESULT_APP,
                       app->name, "Application", app->path, i, 2);
        }
    }

    for (int i = 0; i < file_count && count < max_results; i++) {
        const char *path = file_index[i];
        const char *name = vfs_basename(path);
        if (str_prefix_ci(name, query)) {
            add_result(results, &count, max_results, SEARCH_RESULT_FILE,
                       name, "System file", path, -1, 1);
        } else if (str_contains_ci(name, query)) {
            add_result(results, &count, max_results, SEARCH_RESULT_FILE,
                       name, "System file", path, -1, 3);
        }
    }

    sort_results(results, count);
    return count;
}

SettingsState *settings_get(void)
{
    return &settings_state;
}

void settings_load(void)
{
    VfsFile *file = vfs_open("/Library/Preferences/system.conf", VFS_O_READ);
    if (!file) return;

    char buffer[128];
    ssize_t bytes = vfs_read(file, buffer, sizeof(buffer) - 1);
    vfs_close(file);
    if (bytes <= 0) return;
    buffer[bytes] = '\0';

    settings_state.dark_mode = (str_find(buffer, "dark=1") != NULL);
    settings_state.wifi_enabled = (str_find(buffer, "wifi=1") != NULL);
    settings_state.bluetooth_enabled = (str_find(buffer, "bt=1") != NULL);
    settings_state.volume = (uint8_t)parse_value(buffer, "volume", settings_state.volume);
    settings_state.brightness = (uint8_t)parse_value(buffer, "brightness", settings_state.brightness);
    settings_state.dock_size = (uint8_t)parse_value(buffer, "dock_size", settings_state.dock_size);
    settings_state.dock_magnify = (uint8_t)parse_value(buffer, "dock_mag", settings_state.dock_magnify);
    settings_state.mouse_speed = (uint8_t)parse_value(buffer, "mouse", settings_state.mouse_speed);
    settings_state.shortcuts_enabled = (str_find(buffer, "shortcuts=1") != NULL);
    settings_state.time_24h = (str_find(buffer, "time24=1") != NULL);
}

void settings_save(void)
{
    VfsFile *file = vfs_open("/Library/Preferences/system.conf",
                             VFS_O_CREATE | VFS_O_WRITE | VFS_O_TRUNC);
    if (!file) return;

    char buffer[128];
    buffer[0] = '\0';
    if (settings_state.dark_mode) str_append(buffer, sizeof(buffer), "dark=1\n");
    else str_append(buffer, sizeof(buffer), "dark=0\n");
    if (settings_state.wifi_enabled) str_append(buffer, sizeof(buffer), "wifi=1\n");
    else str_append(buffer, sizeof(buffer), "wifi=0\n");
    if (settings_state.bluetooth_enabled) str_append(buffer, sizeof(buffer), "bt=1\n");
    else str_append(buffer, sizeof(buffer), "bt=0\n");
    {
        char tmp[32];
        tmp[0] = '\0';
        str_append(tmp, sizeof(tmp), "volume=");
        char num[16];
        utoa(settings_state.volume, num, 10);
        str_append(tmp, sizeof(tmp), num);
        str_append(tmp, sizeof(tmp), "\n");
        str_append(buffer, sizeof(buffer), tmp);

        tmp[0] = '\0';
        str_append(tmp, sizeof(tmp), "brightness=");
        utoa(settings_state.brightness, num, 10);
        str_append(tmp, sizeof(tmp), num);
        str_append(tmp, sizeof(tmp), "\n");
        str_append(buffer, sizeof(buffer), tmp);

        tmp[0] = '\0';
        str_append(tmp, sizeof(tmp), "dock_size=");
        utoa(settings_state.dock_size, num, 10);
        str_append(tmp, sizeof(tmp), num);
        str_append(tmp, sizeof(tmp), "\n");
        str_append(buffer, sizeof(buffer), tmp);

        tmp[0] = '\0';
        str_append(tmp, sizeof(tmp), "dock_mag=");
        utoa(settings_state.dock_magnify, num, 10);
        str_append(tmp, sizeof(tmp), num);
        str_append(tmp, sizeof(tmp), "\n");
        str_append(buffer, sizeof(buffer), tmp);

        tmp[0] = '\0';
        str_append(tmp, sizeof(tmp), "mouse=");
        utoa(settings_state.mouse_speed, num, 10);
        str_append(tmp, sizeof(tmp), num);
        str_append(tmp, sizeof(tmp), "\n");
        str_append(buffer, sizeof(buffer), tmp);

        tmp[0] = '\0';
        str_append(tmp, sizeof(tmp), "shortcuts=");
        utoa(settings_state.shortcuts_enabled ? 1 : 0, num, 10);
        str_append(tmp, sizeof(tmp), num);
        str_append(tmp, sizeof(tmp), "\n");
        str_append(buffer, sizeof(buffer), tmp);

        tmp[0] = '\0';
        str_append(tmp, sizeof(tmp), "time24=");
        utoa(settings_state.time_24h ? 1 : 0, num, 10);
        str_append(tmp, sizeof(tmp), num);
        str_append(tmp, sizeof(tmp), "\n");
        str_append(buffer, sizeof(buffer), tmp);
    }

    vfs_write(file, buffer, strlen(buffer));
    vfs_close(file);
}

void settings_toggle_wifi(void)
{
    settings_state.wifi_enabled = !settings_state.wifi_enabled;
    settings_save();
}

void settings_toggle_bluetooth(void)
{
    settings_state.bluetooth_enabled = !settings_state.bluetooth_enabled;
    settings_save();
}

void settings_toggle_dark_mode(void)
{
    settings_state.dark_mode = !settings_state.dark_mode;
    settings_save();
}

void settings_toggle_time_format(void)
{
    settings_state.time_24h = !settings_state.time_24h;
    settings_save();
}

void settings_set_volume(uint8_t value)
{
    settings_state.volume = value;
    settings_save();
}

void settings_set_brightness(uint8_t value)
{
    settings_state.brightness = value;
    settings_save();
}

void notifications_init(void)
{
    latest_notification[0] = '\0';
}

void notifications_push(const char *message)
{
    if (!message) return;
    strncpy(latest_notification, message, sizeof(latest_notification) - 1);
}

const char *notifications_latest(void)
{
    return latest_notification[0] ? latest_notification : "No notifications";
}
