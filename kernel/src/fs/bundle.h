/*
 * ojjyOS v3 Kernel - App Bundle System
 *
 * App bundles are directories with a .app extension that contain:
 * - manifest.json: Application metadata
 * - executable: The main binary (future: ELF loader)
 * - resources/: Icons, images, and other assets
 * - locale/: Localization files (optional)
 *
 * Bundle Structure:
 * MyApp.app/
 * ├── manifest.json       (required)
 * ├── icon.raw            (32x32 RGBA icon)
 * ├── executable          (future: ELF binary)
 * └── resources/
 *     ├── icon@2x.raw     (64x64 for HiDPI)
 *     └── ...
 *
 * Manifest Format (JSON-like, simplified):
 * {
 *   "name": "About ojjyOS",
 *   "bundle_id": "com.ojjyos.about",
 *   "version": "1.0.0",
 *   "executable": "executable",
 *   "icon": "icon.raw",
 *   "category": "system"
 * }
 */

#ifndef _OJJY_BUNDLE_H
#define _OJJY_BUNDLE_H

#include "../types.h"
#include "vfs.h"

/*
 * Maximum lengths
 */
#define BUNDLE_NAME_MAX         64
#define BUNDLE_ID_MAX           128
#define BUNDLE_VERSION_MAX      32
#define BUNDLE_PATH_MAX         256

/*
 * Bundle categories
 */
typedef enum {
    BUNDLE_CAT_UNKNOWN = 0,
    BUNDLE_CAT_SYSTEM,          /* System utilities */
    BUNDLE_CAT_PRODUCTIVITY,    /* Office, notes, etc. */
    BUNDLE_CAT_UTILITY,         /* Tools and utilities */
    BUNDLE_CAT_GAME,            /* Games */
    BUNDLE_CAT_MEDIA,           /* Photos, music, video */
    BUNDLE_CAT_DEVELOPMENT,     /* Dev tools */
} BundleCategory;

/*
 * Bundle manifest (parsed from manifest.json)
 */
typedef struct {
    char            name[BUNDLE_NAME_MAX];
    char            bundle_id[BUNDLE_ID_MAX];
    char            version[BUNDLE_VERSION_MAX];
    char            executable[BUNDLE_PATH_MAX];
    char            icon[BUNDLE_PATH_MAX];
    BundleCategory  category;
    bool            valid;
} BundleManifest;

/*
 * Loaded bundle
 */
typedef struct {
    char            path[BUNDLE_PATH_MAX];      /* Full path to .app */
    BundleManifest  manifest;
    bool            loaded;
} Bundle;

/*
 * Bundle icon (32x32 RGBA)
 */
#define BUNDLE_ICON_SIZE    32
#define BUNDLE_ICON_BYTES   (BUNDLE_ICON_SIZE * BUNDLE_ICON_SIZE * 4)

typedef struct {
    uint8_t pixels[BUNDLE_ICON_BYTES];
    bool    valid;
} BundleIcon;

/*
 * Initialize bundle system
 */
void bundle_init(void);

/*
 * Load a bundle from path
 * Path should be the .app directory (e.g., "/Applications/About.app")
 */
int bundle_load(const char *path, Bundle *bundle);

/*
 * Parse manifest file
 */
int bundle_parse_manifest(const char *manifest_data, size_t size, BundleManifest *manifest);

/*
 * Load bundle icon
 */
int bundle_load_icon(Bundle *bundle, BundleIcon *icon);

/*
 * Launch a bundle (future: spawn process)
 * For now, calls the bundle's "run" handler if registered
 */
typedef void (*BundleRunHandler)(Bundle *bundle);
int bundle_launch(Bundle *bundle);

/*
 * Register a built-in bundle handler
 * Used for system apps that are compiled into the kernel
 */
void bundle_register_builtin(const char *bundle_id, BundleRunHandler handler);

/*
 * Get category from string
 */
BundleCategory bundle_category_from_string(const char *str);

/*
 * Get category name
 */
const char *bundle_category_name(BundleCategory cat);

/*
 * List all bundles in a directory (e.g., /Applications)
 * Returns number of bundles found
 */
int bundle_list_directory(const char *dir_path, Bundle *bundles, int max_bundles);

/*
 * Draw bundle icon at position
 */
void bundle_draw_icon(BundleIcon *icon, int x, int y);

/*
 * Draw default app icon (when no icon available)
 */
void bundle_draw_default_icon(int x, int y);

#endif /* _OJJY_BUNDLE_H */
