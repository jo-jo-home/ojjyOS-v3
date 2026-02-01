/*
 * ojjyOS v3 Kernel - App Bundle Implementation
 */

#include "bundle.h"
#include "vfs.h"
#include "../serial.h"
#include "../string.h"
#include "../console.h"
#include "../framebuffer.h"

/*
 * Built-in bundle handlers (for system apps)
 */
#define MAX_BUILTIN_HANDLERS    16

typedef struct {
    char            bundle_id[BUNDLE_ID_MAX];
    BundleRunHandler handler;
} BuiltinHandler;

static BuiltinHandler builtin_handlers[MAX_BUILTIN_HANDLERS];
static int handler_count = 0;

/*
 * Initialize bundle system
 */
void bundle_init(void)
{
    serial_printf("[BUNDLE] Initializing bundle system...\n");
    handler_count = 0;
    memset(builtin_handlers, 0, sizeof(builtin_handlers));
    serial_printf("[BUNDLE] Bundle system ready\n");
}

/*
 * Skip whitespace in string
 */
static const char *skip_whitespace(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }
    return s;
}

/*
 * Parse a JSON string value (simple parser)
 * Expects: "value" and returns pointer after closing quote
 */
static const char *parse_json_string(const char *s, char *out, size_t max_len)
{
    s = skip_whitespace(s);
    if (*s != '"') return NULL;
    s++;

    size_t i = 0;
    while (*s && *s != '"' && i < max_len - 1) {
        if (*s == '\\' && s[1]) {
            s++;  /* Skip escape */
        }
        out[i++] = *s++;
    }
    out[i] = '\0';

    if (*s != '"') return NULL;
    return s + 1;
}

/*
 * Find key in JSON object (simple parser)
 * Returns pointer to value after the colon
 */
static const char *find_json_key(const char *json, const char *key)
{
    char search[128];
    size_t key_len = strlen(key);
    if (key_len > 120) return NULL;

    search[0] = '"';
    strcpy(search + 1, key);
    search[key_len + 1] = '"';
    search[key_len + 2] = '\0';

    const char *p = json;
    while (*p) {
        /* Look for the key */
        const char *found = NULL;
        for (const char *s = p; *s; s++) {
            bool match = true;
            for (size_t i = 0; i < key_len + 2; i++) {
                if (s[i] != search[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                found = s + key_len + 2;
                break;
            }
        }

        if (!found) return NULL;

        /* Skip to colon */
        found = skip_whitespace(found);
        if (*found == ':') {
            return skip_whitespace(found + 1);
        }

        p = found;
    }

    return NULL;
}

/*
 * Parse manifest file
 */
int bundle_parse_manifest(const char *data, size_t size, BundleManifest *manifest)
{
    if (!data || !manifest || size == 0) return -1;

    memset(manifest, 0, sizeof(*manifest));

    /* Find and parse each field */
    const char *val;

    /* name */
    val = find_json_key(data, "name");
    if (val) {
        parse_json_string(val, manifest->name, BUNDLE_NAME_MAX);
    }

    /* bundle_id */
    val = find_json_key(data, "bundle_id");
    if (val) {
        parse_json_string(val, manifest->bundle_id, BUNDLE_ID_MAX);
    }

    /* version */
    val = find_json_key(data, "version");
    if (val) {
        parse_json_string(val, manifest->version, BUNDLE_VERSION_MAX);
    }

    /* executable */
    val = find_json_key(data, "executable");
    if (val) {
        parse_json_string(val, manifest->executable, BUNDLE_PATH_MAX);
    }

    /* icon */
    val = find_json_key(data, "icon");
    if (val) {
        parse_json_string(val, manifest->icon, BUNDLE_PATH_MAX);
    }

    /* category */
    val = find_json_key(data, "category");
    if (val) {
        char cat_str[32];
        parse_json_string(val, cat_str, sizeof(cat_str));
        manifest->category = bundle_category_from_string(cat_str);
    }

    /* Validate required fields */
    if (manifest->name[0] == '\0' || manifest->bundle_id[0] == '\0') {
        serial_printf("[BUNDLE] Manifest missing required fields\n");
        return -1;
    }

    manifest->valid = true;
    return 0;
}

/*
 * Load a bundle from path
 */
int bundle_load(const char *path, Bundle *bundle)
{
    if (!path || !bundle) return -1;

    memset(bundle, 0, sizeof(*bundle));
    strncpy(bundle->path, path, BUNDLE_PATH_MAX - 1);

    /* Check if path exists and is a bundle */
    if (!vfs_exists(path)) {
        serial_printf("[BUNDLE] Path not found: %s\n", path);
        return -1;
    }

    if (!vfs_is_bundle(path)) {
        serial_printf("[BUNDLE] Not a bundle: %s\n", path);
        return -1;
    }

    /* Build manifest path */
    char manifest_path[BUNDLE_PATH_MAX + 32];
    vfs_join_path(manifest_path, sizeof(manifest_path), path, "manifest.json");

    /* Open and read manifest */
    VfsFile *mf = vfs_open(manifest_path, VFS_O_READ);
    if (!mf) {
        serial_printf("[BUNDLE] No manifest found: %s\n", manifest_path);
        return -1;
    }

    /* Read manifest content */
    char manifest_data[2048];
    ssize_t read_size = vfs_read(mf, manifest_data, sizeof(manifest_data) - 1);
    vfs_close(mf);

    if (read_size <= 0) {
        serial_printf("[BUNDLE] Failed to read manifest\n");
        return -1;
    }
    manifest_data[read_size] = '\0';

    /* Parse manifest */
    if (bundle_parse_manifest(manifest_data, read_size, &bundle->manifest) != 0) {
        return -1;
    }

    bundle->loaded = true;

    serial_printf("[BUNDLE] Loaded: %s (id=%s, v%s)\n",
        bundle->manifest.name,
        bundle->manifest.bundle_id,
        bundle->manifest.version);

    return 0;
}

/*
 * Load bundle icon
 */
int bundle_load_icon(Bundle *bundle, BundleIcon *icon)
{
    if (!bundle || !icon || !bundle->loaded) return -1;

    memset(icon, 0, sizeof(*icon));

    /* Build icon path */
    char icon_path[BUNDLE_PATH_MAX + 64];
    if (bundle->manifest.icon[0]) {
        vfs_join_path(icon_path, sizeof(icon_path), bundle->path, bundle->manifest.icon);
    } else {
        vfs_join_path(icon_path, sizeof(icon_path), bundle->path, "icon.raw");
    }

    /* Open icon file */
    VfsFile *f = vfs_open(icon_path, VFS_O_READ);
    if (!f) {
        serial_printf("[BUNDLE] No icon found: %s\n", icon_path);
        return -1;
    }

    /* Read icon data (32x32 RGBA = 4096 bytes) */
    ssize_t read_size = vfs_read(f, icon->pixels, BUNDLE_ICON_BYTES);
    vfs_close(f);

    if (read_size != BUNDLE_ICON_BYTES) {
        serial_printf("[BUNDLE] Invalid icon size: %d (expected %d)\n",
            (int)read_size, BUNDLE_ICON_BYTES);
        return -1;
    }

    icon->valid = true;
    return 0;
}

/*
 * Register builtin handler
 */
void bundle_register_builtin(const char *bundle_id, BundleRunHandler handler)
{
    if (handler_count >= MAX_BUILTIN_HANDLERS) {
        serial_printf("[BUNDLE] Too many builtin handlers\n");
        return;
    }

    BuiltinHandler *h = &builtin_handlers[handler_count++];
    strncpy(h->bundle_id, bundle_id, BUNDLE_ID_MAX - 1);
    h->handler = handler;

    serial_printf("[BUNDLE] Registered builtin: %s\n", bundle_id);
}

/*
 * Launch a bundle
 */
int bundle_launch(Bundle *bundle)
{
    if (!bundle || !bundle->loaded) return -1;

    serial_printf("[BUNDLE] Launching: %s\n", bundle->manifest.name);

    /* Look for builtin handler */
    for (int i = 0; i < handler_count; i++) {
        if (strcmp(builtin_handlers[i].bundle_id, bundle->manifest.bundle_id) == 0) {
            if (builtin_handlers[i].handler) {
                builtin_handlers[i].handler(bundle);
                return 0;
            }
        }
    }

    /* TODO: Load and execute ELF binary */
    serial_printf("[BUNDLE] No handler for: %s\n", bundle->manifest.bundle_id);
    console_printf("Cannot launch %s: No executable loader yet\n", bundle->manifest.name);

    return -1;
}

/*
 * Category from string
 */
BundleCategory bundle_category_from_string(const char *str)
{
    if (!str) return BUNDLE_CAT_UNKNOWN;

    if (strcmp(str, "system") == 0) return BUNDLE_CAT_SYSTEM;
    if (strcmp(str, "productivity") == 0) return BUNDLE_CAT_PRODUCTIVITY;
    if (strcmp(str, "utility") == 0) return BUNDLE_CAT_UTILITY;
    if (strcmp(str, "game") == 0) return BUNDLE_CAT_GAME;
    if (strcmp(str, "media") == 0) return BUNDLE_CAT_MEDIA;
    if (strcmp(str, "development") == 0) return BUNDLE_CAT_DEVELOPMENT;

    return BUNDLE_CAT_UNKNOWN;
}

/*
 * Category name
 */
const char *bundle_category_name(BundleCategory cat)
{
    switch (cat) {
        case BUNDLE_CAT_SYSTEM:       return "System";
        case BUNDLE_CAT_PRODUCTIVITY: return "Productivity";
        case BUNDLE_CAT_UTILITY:      return "Utility";
        case BUNDLE_CAT_GAME:         return "Game";
        case BUNDLE_CAT_MEDIA:        return "Media";
        case BUNDLE_CAT_DEVELOPMENT:  return "Development";
        default:                      return "Unknown";
    }
}

/*
 * List bundles in directory
 */
int bundle_list_directory(const char *dir_path, Bundle *bundles, int max_bundles)
{
    if (!dir_path || !bundles || max_bundles <= 0) return 0;

    VfsDir *dir = vfs_opendir(dir_path);
    if (!dir) {
        serial_printf("[BUNDLE] Cannot open directory: %s\n", dir_path);
        return 0;
    }

    int count = 0;
    VfsDirEntry entry;

    while (vfs_readdir(dir, &entry) == 0 && count < max_bundles) {
        /* Check if it's a bundle */
        if (entry.type == VFS_TYPE_BUNDLE || vfs_is_bundle(entry.name)) {
            char full_path[BUNDLE_PATH_MAX];
            vfs_join_path(full_path, sizeof(full_path), dir_path, entry.name);

            if (bundle_load(full_path, &bundles[count]) == 0) {
                count++;
            }
        }
    }

    vfs_closedir(dir);
    return count;
}

/*
 * Draw bundle icon
 */
void bundle_draw_icon(BundleIcon *icon, int x, int y)
{
    if (!icon || !icon->valid) {
        bundle_draw_default_icon(x, y);
        return;
    }

    /* Draw 32x32 RGBA icon */
    for (int row = 0; row < BUNDLE_ICON_SIZE; row++) {
        for (int col = 0; col < BUNDLE_ICON_SIZE; col++) {
            int idx = (row * BUNDLE_ICON_SIZE + col) * 4;
            uint8_t r = icon->pixels[idx + 0];
            uint8_t g = icon->pixels[idx + 1];
            uint8_t b = icon->pixels[idx + 2];
            uint8_t a = icon->pixels[idx + 3];

            if (a > 128) {  /* Simple alpha threshold */
                fb_put_pixel(x + col, y + row, RGB(r, g, b));
            }
        }
    }
}

/*
 * Draw default app icon
 */
void bundle_draw_default_icon(int x, int y)
{
    /* Simple 32x32 default icon - rounded rectangle with "A" */
    Color bg = COLOR_SLATE;
    Color fg = COLOR_WHITE;

    /* Background with rounded corners */
    fb_fill_rect(x + 2, y, 28, 32, bg);
    fb_fill_rect(x, y + 2, 32, 28, bg);
    fb_fill_rect(x + 1, y + 1, 30, 30, bg);

    /* Draw "A" in center */
    /* Simple bitmap A (8x10) centered */
    static const uint8_t letter_a[] = {
        0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00, 0x00
    };

    int ax = x + 12;
    int ay = y + 11;
    for (int row = 0; row < 10; row++) {
        for (int col = 0; col < 8; col++) {
            if (letter_a[row] & (0x80 >> col)) {
                fb_put_pixel(ax + col, ay + row, fg);
            }
        }
    }
}
