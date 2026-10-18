# ojjyOS v3 Filesystem Strategy

This document defines the filesystem layout, bundle format, metadata model, and MVP implementation plan for a macOS-like experience while remaining original to ojjyOS.

## 1) System Directory Layout (Finder-style)

### Root layout

```
/
├── System
├── Applications
├── Users
├── Library
└── Volumes
```

### /System

Purpose: system-owned assets required for the OS to boot and render the UI.

Contents:
- `/System/Library` - OS-level frameworks, shared system components, and service definitions.
- `/System/Fonts` - system fonts used by the shell and default apps.
- `/System/Wallpapers` - old 640x480 `.raw` wallpapers (unused by the desktop now; only Spotlight lists them). Desktop pictures come from the disk pack, see the data partition map below.
- `/System/version.txt` - build/version metadata.

Why: keeps immutable, system-critical files separated from user and application content.

### /Applications

Purpose: user-facing apps presented by Finder.

Contents:
- App bundles as directories with a `.app` suffix, for example `About.app`.

Why: Finder-friendly, bundle-based organization with metadata and resources contained in a single package.

### /Users/<name>

Purpose: per-user data and desktops.

Contents:
- `/Users/<name>/Desktop` - user desktop.
- `/Users/<name>/Documents` - user documents.
- Optional `Downloads`, `Pictures`, `Music`, `Movies` later.

Why: clear ownership boundaries and a familiar macOS-inspired experience.

### /Library

Purpose: shared, non-system, writable state.

Contents:
- `/Library/Preferences` - system-wide preference data.
- `/Library/Fonts` (optional) - shared fonts that are not system-critical.
- `/Library/Metadata` (Phase 2) - extended attributes store.

Why: decouples mutable, shared data from `/System` to keep the system image stable.

### /Volumes (optional)

Purpose: mount point for external and secondary volumes.

Contents:
- `/Volumes/<disk name>` - mounted block devices, removable media, or test images.

Why: isolates external storage while keeping a consistent Finder path.

## 2) Filesystem Plan

### Current writable storage (reliability pass)

`/Users` and `/Library` are independent RAMFS instances checkpointed to disk.
The data partition contains two alternating 8 MiB slots with CRC-32 protected
payloads and commit headers. Boot restores the newest structurally valid,
checksummed save, falling back to the older slot on corruption. Legacy v1
snapshots have no checksum; structural validation permits migration without
overwriting the old record first. Autosaves write in bounded batches; shutdown
and explicit sync wait for a durable commit. CRC-32 does not provide encryption
or protection against intentional disk edits.

Data partition map (LBA = 512-byte sector):

| LBA | Use |
|---|---|
| 67584..83967 | persistence slot 0 (8 MiB) |
| 83968..100351 | persistence slot 1 (8 MiB) |
| 100352..102399 | spare (user-data region) |
| 102400..262110 | read-only desktop picture pack (`kernel/src/image/wallpack.h`), written by `scripts/mkimg.sh` from git-ignored `wallpapers/local/` |

Rebuilding `build/<arch>/ojjyos3.img` preserves the user-data region
(LBA 67584..102399), installs the new build's picture pack, and keeps a
full backup. Do not run old image-building scripts against saved disks. Never
share one writable disk between VMs. User home folders are separate directories,
not yet enforced authorization boundaries.

The following phase descriptions are historical strategy notes.

### MVP (Phase 1) - Read-only packed filesystem

Goal: get a reliable read-only system image working in VM.

Plan:
- OJFS (packed filesystem) embedded into the kernel image.
- VFS layer provides `open/read/close/opendir/readdir`.
- All system data (System/Applications/Users) exists under a single OJFS image.

### MVP Overlay (RAMFS)

- RAMFS is mounted at `/Users` and `/Library` to provide writable user data and preferences.
- Seed files are created at boot (Welcome, Test, Notes).

Rationale:
- Simple to build and deterministic in a VM.
- Minimal dependencies (no block device writes).

### Phase 2 - Writable FS + metadata

Goal: add writes, metadata, and permissions.

Plan:
- Add a writable overlay (RAM or disk-backed) on top of OJFS.
- New native FS (OJFS2 or a block-backed FS) with:
  - inodes + directories
  - permissions + ownership
  - extended attributes (xattrs)
- Keep OJFS as the immutable system image, overlay for user/app data.

### Permissions + ownership model

MVP capabilities (already in VFS):
- `READ`, `WRITE`, `EXECUTE`, `SYSTEM` (system-owned, immutable by user).

Phase 2 ownership model:
- UID 0: system
- UID 1000+: users
- Files inherit owner from parent directory unless explicitly set.
- Capabilities can override in-kernel access for system services.

Extended attributes (Phase 2):
- Stored in `/Library/Metadata` keyed by `inode` or path hash.
- Example keys: `com.ojjyos.bundle.icon`, `com.ojjyos.finder.tags`.

## 3) App Bundles

### Bundle structure

```
MyApp.app/
├── manifest.json
├── executable
├── icon.raw
└── resources/
    ├── icon@2x.raw
    └── about.txt
```

Required:
- `manifest.json`
- `executable` (future ELF loader target)

Optional:
- `icon.raw` (32x32 RGBA)
- `resources/` for images, docs, localization
- `locale/` (future localization support)

### Launching a bundle

1. Finder or shell calls `bundle_load(path)`.
2. Manifest is parsed and validated.
3. `bundle_launch()` either:
   - calls a built-in handler for system apps, or
   - loads the executable (future ELF loader).

### Finder display rules

- Directories ending with `.app` are treated as packages.
- Finder shows bundle icon and name without the `.app` suffix.
- Default behavior: open/launch, not enter the folder.
- Package contents are shown only when explicitly requested.

## 4) Code Deliverables (MVP)

Already implemented in kernel:
- VFS read API: `vfs_open`, `vfs_read`, `vfs_close`, `vfs_opendir`, `vfs_readdir`.
- OJFS read-only filesystem.
- Bundle manifest parser in `bundle_parse_manifest()`.

Sample bundle content:
- `/Applications/About.app/manifest.json`
- `/Applications/About.app/icon.raw` (generated by build script)
- `/Applications/About.app/resources/about.txt`

## 5) Acceptance Tests

From the ojjyOS shell:

1. List apps
```
ls /Applications
```
Expected: `About.app` appears (type `APP`).

2. Launch app
```
launch About.app
```
Expected: About screen printed to console.

3. Read wallpaper
```
cat /System/Wallpapers/Tahoe\ Light.raw
```
Expected: raw data prints (binary noise is normal).

## Build/Run Updates

1. Build filesystem image and embedded header:
```
./scripts/mkfs.sh
```

2. Build the OS:
```
./scripts/build.sh
```

3. Run in VM:
```
./scripts/run.sh
```

The build script now runs `mkfs.sh` automatically to ensure `system_fs.h` exists.
