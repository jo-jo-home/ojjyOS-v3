# 12 — Storage: block layer, filesystems, volumes, backups

Today: read-only OJFS system image + two in-RAM RAMFS trees (256 nodes,
1 MiB each) saved as whole snapshots to the data partition
(`02-current-state.md` §4). Target: VFS v2 + ojjyFS (D-009) on every
volume, FAT/exFAT/ext4/NTFS/ISO for other disks, snapshots and backups.

Rules for every storage card:
- **User data is sacred.** Every migration keeps a verified backup and is
  reversible; tests always use private disk copies.
- Every on-disk structure parser is fuzzed on the host (truncated and
  corrupted images) and never trusts sizes/offsets/counts.
- Crash consistency is tested by killing the VM at random points during
  writes (`tests/qemu_crash_fs.py`, created by FS-13) and checking the
  filesystem afterwards.

---

## Block layer

### BLK-01 — Block layer with asynchronous requests
- Phase: P3 · Size: M · Status: todo
- Depends: SCH-04, MEM-18
- Files: kernel/src/drivers/block_cache.c → kernel/block/

**Context.** Drivers expose `block_read_sectors/block_write_sectors`
synchronously; the whole system waits during disk I/O.
**Goal.** Block device objects with request queues: requests (read,
write, flush, discard) over scatter-gather page lists, completion
callbacks, per-device queue depth, merging of adjacent requests,
statistics (`iostat`), synchronous wrappers for old callers.
**Done when.** virtio-blk, NVMe, AHCI, USB storage plug in; persistence
writes no longer block the UI thread.
**Tests.** Host tests with a fake device (reordering, errors, flush
ordering); live concurrency test.
**Pitfalls.** FLUSH/FUA semantics: a flush must wait for prior writes.
**Refs.** —

### BLK-02 — Partition tables (GPT, MBR)
- Phase: P3 · Size: S · Status: todo
- Depends: BLK-01
- Files: kernel/block/part.c

**Goal.** Parse GPT (with CRC and backup header) and MBR (incl. extended
partitions), create partition block devices with type GUIDs and names
(`disk0s2`), protective MBR handling.
**Done when.** USB sticks and the system disk show partitions by name in
Disk Utility.
**Tests.** Host tests with images built by `tools/finalize_image.py` and
corrupted variants.
**Pitfalls.** 4K-sector disks; GPT backup at the disk end.
**Refs.** UEFI spec ch. 5 (GPT).

### BLK-03 — Buffer cache for metadata
- Phase: P3 · Size: S · Status: todo
- Depends: BLK-01
- Files: kernel/block/bcache.c

**Goal.** Cached blocks with reference counts, dirty tracking and
write-back for filesystem metadata (ojjyFS B-tree nodes, FAT tables).
**Done when.** FAT and ojjyFS use it; hit rate visible in stats.
**Tests.** Host tests.
**Pitfalls.** Merge with page cache later (BLK-04) to avoid double caching.
**Refs.** —

### BLK-04 — Page cache with write-back
- Phase: P3 · Size: M · Status: todo
- Depends: BLK-03, MEM-02
- Files: kernel/block/pagecache.c

**Goal.** File data cached in pages per (file, offset), read-ahead,
write-back threads with dirty limits, `fsync`, eviction under pressure
(MEM-14), integration with `mmap` (MEM-12).
**Done when.** Reading a 500 MB file twice: second read ≥ 5 GB/s.
**Tests.** Live benchmark program; host tests of the cache tree.
**Pitfalls.** Dirty pages during snapshot creation (ojjyFS CoW).
**Refs.** —

### BLK-05 — I/O priorities
- Phase: P3 · Size: S · Status: todo
- Depends: BLK-01, SCH-03
- Files: kernel/block/sched.c

**Goal.** Interactive I/O (app launch, file open) before background I/O
(backups, indexing, package installs).
**Done when.** App launch time during a backup is within 20% of idle.
**Tests.** Live benchmark.
**Pitfalls.** Starvation of background I/O.
**Refs.** —

### BLK-06 — Disk images (loop devices) and `.ojdmg`
- Phase: P5 · Size: S · Status: todo
- Depends: BLK-01, FS-01
- Files: kernel/block/loop.c

**Goal.** Mount a file as a block device; `.ojdmg` app distribution format
(ojjyFS image, optionally compressed/signed) that opens in Finder like a
`.dmg`.
**Done when.** Double-clicking a `.ojdmg` mounts it under `/Volumes`.
**Tests.** Live test.
**Pitfalls.** Nested loop mounts.
**Refs.** —

---

## VFS and special filesystems

### FS-01 — VFS v2 (vnodes, name cache, mounts)
- Phase: P2 · Size: L · Status: todo
- Depends: SYS-02, THR-03, PROC-04
- Files: kernel/src/fs/vfs.c → kernel/fs/vfs/

**Context.** `vfs.c` has mount instances and path normalisation with
per-path access checks; no vnode objects, no reference-counted open files,
no locking for concurrency.
**Goal.** Vnodes with reference counts and per-vnode locks, a name
(dentry) cache, mount points anywhere (incl. over directories), path walk
with `..`, symlinks (loop limit 40), permission checks per component,
open-file objects with offsets and flags, filesystem operation table
(lookup, create, read, write, readdir, getattr, setattr, rename, link,
unlink, symlink, readlink, fsync, statfs, xattr).
**Steps.**
1. Define the ops table; adapt RAMFS and OJFS to it.
2. Implement path walk + name cache; replace callers.
3. Move S4/S5 hardening into permission checks (FS-09).
**Done when.** All existing VFS tests and live suites pass on VFS v2; two
threads reading/writing different files do not serialize.
**Tests.** `test_fs.c`, `test_ramfs.c` ported; new concurrency tests.
**Pitfalls.** Rename across directories and lock ordering; mount points
being busy.
**Refs.** Kleiman, "Vnodes" (1986).

### FS-02 — OJFS system image on VFS v2 (and initrd)
- Phase: P2 · Size: S · Status: todo
- Depends: FS-01
- Files: kernel/src/fs/ojfs.c

**Goal.** OJFS as a VFS v2 filesystem mountable from a partition or a
memory buffer (initrd), with directory hash lookups for speed.
**Done when.** `/System` and the initrd mount through VFS v2.
**Tests.** `test_fs.c` + fuzzing of OJFS headers.
**Pitfalls.** Keep `tools/mkojfs` in sync.
**Refs.** —

### FS-03 — tmpfs
- Phase: P2 · Size: S · Status: todo
- Depends: FS-01
- Files: kernel/fs/tmpfs.c

**Goal.** In-memory filesystem with page-backed files (no 1 MiB cap) for
`/tmp`, `/var/run`, early boot.
**Done when.** `/tmp` is tmpfs; a 100 MB file can be written there.
**Tests.** Host + live tests.
**Pitfalls.** Size limit (default 25% RAM).
**Refs.** —

### FS-04 — devfs (`/dev`)
- Phase: P2 · Size: S · Status: todo
- Depends: FS-01, DRV-CORE-01
- Files: kernel/fs/devfs.c

**Goal.** Device nodes created by drivers (`/dev/null`, `zero`, `random`,
`urandom`, `tty*`, `ptmx`, `disk*`), permissions per node.
**Done when.** Shell redirection to `/dev/null` works; disks visible.
**Tests.** Live tests.
**Pitfalls.** Raw disk nodes admin-only.
**Refs.** —

### FS-05 — POSIX filesystem conformance suite
- Phase: P2 · Size: M · Status: todo
- Depends: FS-01, SYS-07
- Files: tests/posixfs/ (new)

**Goal.** An original test program covering open/creat flags, O_APPEND,
truncate, rename semantics, hard links, symlinks, permissions, sticky
bits, readdir stability, large files (> 4 GiB), sparse files, timestamps.
Runs on every filesystem with a capability list.
**Done when.** Runs in the live suite for tmpfs, ojjyFS, FAT.
**Tests.** The suite itself.
**Pitfalls.** —
**Refs.** POSIX.1-2017.

### FS-06 — File locking
- Phase: P3 · Size: S · Status: todo
- Depends: FS-01
- Files: kernel/fs/vfs/lock.c

**Goal.** `flock` and `fcntl` byte-range locks (advisory), Windows-style
mandatory share modes for ojjyWin (`WIN-14`).
**Done when.** Two processes coordinate with locks in a test.
**Tests.** Live tests.
**Pitfalls.** Lock release on process exit.
**Refs.** POSIX `fcntl`.

### FS-07 — Extended attributes and Finder tags
- Phase: P3 · Size: S · Status: todo
- Depends: FS-13
- Files: kernel/fs/vfs/xattr.c

**Goal.** `getxattr/setxattr/listxattr` on ojjyFS (and emulated on FAT via
side files), used for tags (colours), quarantine flags, "where from" URLs
for downloads.
**Done when.** Finder tags persist across reboot.
**Tests.** Live test.
**Pitfalls.** Size limits.
**Refs.** —

### FS-08 — File change notifications
- Phase: P2 · Size: M · Status: todo
- Depends: FS-01, IPC-06
- Files: kernel/fs/vfs/notify.c

**Goal.** Watch directories/files (create, delete, modify, rename, attrib)
with event delivery to ports (FSEvents/inotify-like); Finder windows
update live; search indexer and backups use it.
**Done when.** Saving a file in TextEdit updates an open Finder window
within 100 ms.
**Tests.** Live test.
**Pitfalls.** Event queue overflow → "rescan" event.
**Refs.** —

### FS-09 — Permissions and ACLs in the VFS
- Phase: P3 · Size: M · Status: todo
- Depends: FS-01, PROC-04
- Files: kernel/fs/vfs/perm.c, kernel/src/fs/vfs.c (remove path rules)

**Context.** S4/S5 hardening is path-based ("/Users/<other>").
**Goal.** POSIX modes + owner/group + ACL entries (allow/deny, inheritance)
checked on every operation; admin override via explicit privilege, not
by path.
**Done when.** Path-based rules deleted; the S4/S5 host tests pass using
real permissions.
**Tests.** Existing security tests + ACL tests.
**Pitfalls.** Migration must set correct owners on existing homes (FS-17).
**Refs.** POSIX.1e draft ACL concepts.

### FS-12 — Process and system information filesystem (`/proc`)
- Phase: P2 · Size: S · Status: todo
- Depends: FS-01, PROC-09
- Files: kernel/fs/procfs.c

**Goal.** `/proc/<pid>/{status,cmdline,maps,fd/}`, `/proc/meminfo`,
`/proc/cpuinfo`, `/proc/uptime` — enough for tools and the Linux layer.
**Done when.** `cat /proc/meminfo` works; Linux `ps` (later) can read it.
**Tests.** Live tests.
**Pitfalls.** Permissions for other users' processes.
**Refs.** —

### FS-30 — User-space filesystems (FUSE-like)
- Phase: P6 · Size: M · Status: todo
- Depends: FS-01, IPC-07
- Files: kernel/fs/userfs.c, lib/libuserfs

**Goal.** Filesystems implemented by user processes over a port protocol
(network shares, archives mounted as folders, cloud drives later).
**Done when.** A sample "zip as folder" filesystem works in Finder.
**Tests.** Live test.
**Pitfalls.** Hung servers must not hang the kernel (timeouts).
**Refs.** —

---

## ojjyFS (the native filesystem, D-009)

### FS-10 — ojjyFS design and on-disk format specification
- Phase: P3 · Size: M · Status: todo
- Depends: —
- Files: docs/ojjyfs/format.md (new)

**Goal.** Written spec before code: superblock (checkpoints ring), object
map, copy-on-write B+trees (fs tree, extent tree, free-space tree,
snapshot tree), inode/dirent/xattr records, checksums (CRC32C or
xxHash64), transaction model (atomic checkpoint), snapshots/clones,
volume groups (system + data volumes sharing space), encryption hooks
(per-file keys wrapped by volume key — SEC-30), compression flags,
sizes/limits, endianness (little), versioning and feature flags.
**Done when.** Spec reviewed (decision record D-021), test vectors listed.
**Tests.** —
**Pitfalls.** Design for crash safety first, performance second.
**Refs.** Rodeh, "B-trees, Shadowing, and Clones" (2008); general CoW
filesystem literature (no code copying).

### FS-11 — ojjyFS: `mkfs` and read-only mount
- Phase: P3 · Size: L · Status: todo
- Depends: FS-10, FS-01, BLK-03
- Files: kernel/fs/ojjyfs/, tools/mkojjyfs/

**Goal.** Host tool creating ojjyFS images from a directory; kernel mounts
read-only: B-tree lookups, directory iteration, file reads via extents,
checksum verification with clear errors.
**Done when.** The system volume can be built as ojjyFS and boots
read-only.
**Tests.** Host tests: mkfs → mount → compare trees; fuzz images.
**Pitfalls.** Share B-tree code between host tool and kernel (one source).
**Refs.** FS-10.

### FS-13 — ojjyFS write support (CoW transactions)
- Phase: P3 · Size: L · Status: todo
- Depends: FS-11, BLK-04
- Files: kernel/fs/ojjyfs/

**Goal.** Create/write/truncate/rename/unlink/mkdir with copy-on-write
updates grouped into transactions committed by an atomic checkpoint
every few seconds or on `fsync`; space allocator; delayed allocation.
**Done when.** FS-05 conformance passes; random-kill crash test
(`tests/qemu_crash_fs.py`) shows 100 kills with zero corruption.
**Tests.** Host tests with an in-memory block device + crash injection;
QEMU crash test.
**Pitfalls.** ENOSPC handling during CoW (reserve metadata space).
**Refs.** FS-10.

### FS-14 — ojjyFS check, repair and scrub
- Phase: P3 · Size: M · Status: todo
- Depends: FS-13
- Files: tools/fsck_ojjyfs (host + guest), kernel/fs/ojjyfs/scrub.c

**Goal.** Offline checker/repair (`fsck_ojjyfs`), online scrub verifying
checksums in the background, Disk Utility "First Aid" front end.
**Done when.** Injected corruptions are detected and repaired where the
format allows (older checkpoint, duplicate metadata).
**Tests.** Host corruption suite.
**Pitfalls.** Never "repair" by deleting user data silently: report.
**Refs.** —

### FS-15 — Snapshots and clones
- Phase: P3 · Size: M · Status: todo
- Depends: FS-13
- Files: kernel/fs/ojjyfs/snapshot.c

**Goal.** Read-only snapshots (create/list/delete/mount), file clones
(`cp --clone` instant copies), basis for backups (STO-01) and update
rollback (SVC-40).
**Done when.** Snapshot of the data volume taken in < 100 ms and mounted
read-only showing old contents.
**Tests.** Host + live tests.
**Pitfalls.** Space accounting with shared blocks.
**Refs.** —

### FS-16 — Transparent compression
- Phase: P6 · Size: M · Status: todo
- Depends: FS-13
- Files: kernel/fs/ojjyfs/compress.c

**Goal.** Per-extent compression (an original LZ4-class codec and later
zstd-class) for system files and opt-in folders.
**Done when.** System volume 30% smaller with no measurable launch-time
regression.
**Tests.** Host codec tests (round-trip, fuzz).
**Pitfalls.** Random access into compressed extents.
**Refs.** —

### FS-17 — Migrate user data from RAMFS snapshots to ojjyFS
- Phase: P3 · Size: M · Status: todo
- Depends: FS-13, FS-09
- Files: kernel/src/fs/persist.c, tools/finalize_image.py

**Context.** Jonas's accounts and files live in persistence slots at LBA
67584 (two 8 MiB slots) — never lose them.
**Goal.** On first boot of the new release: read the newest valid RAMFS
snapshot, create the ojjyFS data volume in the data partition region
(keeping the slots until migration is verified), copy `/Users` and
`/Library` with correct owners/modes, verify, then mark migrated. Image
builder learns the new layout but still preserves the old region.
**Done when.** Jonas's installed images migrate with all files and
accounts intact (hash-compared listing before/after), and a rollback
path is documented and tested.
**Tests.** Migration test on copies of the real installed images (private
copies only).
**Pitfalls.** Partition size; wallpaper pack at LBA 102400 must survive.
**Refs.** FILESYSTEM.md, AGENTS.md gotcha 12.

### FS-18 — Sealed system volume + data volume
- Phase: P5 · Size: M · Status: todo
- Depends: FS-15, SEC-20
- Files: kernel/fs/ojjyfs/seal.c, scripts/mkimg.sh

**Goal.** System volume read-only with a Merkle-tree seal verified at
mount (tamper → refuse/boot recovery); data volume writable; firmlinks
so `/Applications` combines both.
**Done when.** Modifying a system file offline is detected at boot.
**Tests.** QEMU tamper test.
**Pitfalls.** Updates must create a new sealed snapshot (SVC-40).
**Refs.** —

### FS-19 — Space accounting, quotas, purgeable space
- Phase: P6 · Size: S · Status: todo
- Depends: FS-13
- Files: kernel/fs/ojjyfs/space.c

**Goal.** Accurate free space with snapshots, per-user quotas (optional),
purgeable caches freed automatically when space is low; "About This
Mac"-like storage breakdown in Settings > Storage.
**Done when.** Storage pane shows categories (Apps, Documents, System,
Purgeable).
**Tests.** Live test.
**Pitfalls.** —
**Refs.** —

---

## Other filesystems

### FS-20 — FAT12/16/32
- Phase: P3 · Size: M · Status: todo
- Depends: FS-01, BLK-02
- Files: kernel/fs/fat/

**Goal.** Read/write FAT with long file names (VFAT, UTF-16), case
handling, timestamps, free-cluster bitmap cache, dirty flag; also used
for the EFI system partition (updates of the bootloader).
**Done when.** USB sticks formatted on macOS/Windows read and write
correctly; `fsck` on the Mac finds no errors after heavy use.
**Tests.** Host tests with images made by `mkfs.fat`/macOS; fuzz.
**Pitfalls.** Cluster chain loops; FAT32 FSInfo hints are hints only.
**Refs.** Microsoft FAT32 File System Specification (public).

### FS-21 — exFAT
- Phase: P5 · Size: M · Status: todo
- Depends: FS-20
- Files: kernel/fs/exfat/

**Goal.** Read/write exFAT (large USB drives, SD cards), up-case table,
allocation bitmap, checksums.
**Done when.** 64 GB exFAT drive works, macOS verifies it clean.
**Tests.** Host tests with images; fuzz.
**Pitfalls.** Entry set checksums.
**Refs.** Microsoft exFAT specification (public).

### FS-22 — ext4 (read, then write)
- Phase: P7 · Size: L · Status: todo
- Depends: FS-01, BLK-04
- Files: kernel/fs/ext4/

**Goal.** Read ext2/3/4 (extents, htree dirs, 64-bit, metadata_csum),
then write without journal replay bugs (journal replay on mount, write
with journaling). Needed for Linux disks and the Linux environment
image (LNX-40 may use it).
**Done when.** A Linux USB disk mounts read-only (step 1) / read-write
with `e2fsck` clean (step 2).
**Tests.** Host tests with `mke2fs` images; fuzz.
**Pitfalls.** Many feature flags; refuse unknown incompatible features.
**Refs.** ext4 disk layout documentation (kernel.org wiki, public).

### FS-23 — NTFS (read)
- Phase: P8 · Size: L · Status: todo
- Depends: FS-01, BLK-04
- Files: kernel/fs/ntfs/

**Goal.** Read NTFS volumes (MFT, attributes, runlists, indexes,
compression) so Windows disks and ojjyWin users can copy files.
**Done when.** Files copy from a Windows-formatted USB disk.
**Tests.** Host tests with images; fuzz.
**Pitfalls.** Fixup arrays; resident vs non-resident attributes.
**Refs.** Public NTFS documentation (e.g. "NTFS Documentation" by
R. Russon & Y. Fledel).

### FS-24 — ISO 9660, Joliet, Rock Ridge, UDF
- Phase: P5 · Size: S · Status: todo
- Depends: FS-01
- Files: kernel/fs/iso9660/

**Goal.** Mount `.iso` files and optical discs (read-only), UDF for DVDs.
**Done when.** A Linux distro ISO mounts and browses in Finder.
**Tests.** Host tests with `mkisofs` images.
**Pitfalls.** —
**Refs.** ECMA-119, ECMA-167.

### FS-25 — HFS+ and APFS (read-only, stretch)
- Phase: P9 · Size: L · Status: todo
- Depends: FS-01
- Files: kernel/fs/apfs/

**Goal.** Read Mac-formatted external drives (HFS+ first, APFS
unencrypted later).
**Done when.** Files copy from a Mac-formatted USB drive.
**Tests.** Host tests with images created on the Mac.
**Pitfalls.** Apple published an APFS reference — follow it, no copying.
**Refs.** Apple File System Reference (public PDF), TN1150 (HFS+).

---

## Volumes and data safety

### STO-01 — "Rewind" backups (Time Machine-style)
- Phase: P6 · Size: L · Status: todo
- Depends: FS-15, SVC-01, APP-DISK-01
- Files: services/backupd/, apps/Rewind

**Goal.** Hourly snapshots of the data volume, incremental backups to an
external ojjyFS disk or network share (SMB, NET-70), browsing history in
a "Rewind" app with a timeline, restore single files or the whole system
(from Recovery).
**Done when.** Delete a file, restore it from yesterday's backup via the
app; full restore in Recovery works on a fresh VM.
**Tests.** Live test with a second virtual disk.
**Pitfalls.** Backups must be encrypted when the source is (SEC-30).
**Refs.** —

### STO-02 — Disk management service (`diskd`)
- Phase: P3 · Size: M · Status: todo
- Depends: BLK-02, FS-11, DRV-CORE-02
- Files: services/diskd/

**Goal.** Privileged service for partitioning, formatting (ojjyFS, FAT,
exFAT), erasing, verifying, mounting/unmounting/ejecting, with an IPC API
used by Disk Utility, Finder and the installer.
**Done when.** Disk Utility (APP-DISK-01) formats a USB stick.
**Tests.** Live test with a virtual disk.
**Pitfalls.** Never act on the boot disk without explicit confirmation.
**Refs.** —

### STO-03 — Automount, volume names and safe eject
- Phase: P3 · Size: S · Status: todo
- Depends: STO-02
- Files: services/diskd/automount.c, Finder sidebar

**Goal.** New volumes mount under `/Volumes/<label>`, appear in Finder's
sidebar and on the Desktop, eject button flushes and unmounts, warning
notification if a disk was removed without ejecting.
**Done when.** USB stick round-trip in QEMU (insert, copy, eject, remove).
**Tests.** Live test.
**Pitfalls.** Label collisions (`USB`, `USB 1`).
**Refs.** —

### STO-04 — Trash with Put Back (system-wide)
- Phase: P3 · Size: S · Status: todo
- Depends: FS-07
- Files: lib/libojjy/trash.c, Finder

**Goal.** Per-volume `.Trashes` with original-path metadata (xattr),
Put Back, Empty Trash securely, auto-empty after 30 days (optional).
**Done when.** Put Back restores to the original folder after reboot.
**Tests.** Live test.
**Pitfalls.** Trash on removable volumes.
**Refs.** —
