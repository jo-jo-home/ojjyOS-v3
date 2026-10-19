/*
 * ojjyOS v3 Kernel - virtio-gpu 2D display driver (aarch64, virtio-mmio)
 *
 * QEMU's ramfb firmware display only offers 4:3 modes (640x480, 800x600,
 * 1024x768), which leaves black bars in a widescreen/16:10 full-screen
 * window. virtio-gpu reports the host window's preferred size (QEMU's
 * xres/yres, 1280x800 by default) and accepts any scanout size, so the
 * desktop can match the Mac's screen shape.
 *
 * Model: one 2D resource in guest RAM is the visible screen. The
 * framebuffer layer draws into it as usual and calls the flush hook with
 * the changed rectangle; we then ask the host to copy (TRANSFER_TO_HOST_2D)
 * and show (RESOURCE_FLUSH) that rectangle. Commands are synchronous and
 * only issued from the main loop or the halted panic path.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "virtio.h"
#include "mmio.h"
#include "virtio_devices.h"
#include "framebuffer.h"
#include "memory.h"
#include "serial.h"
#include "string.h"
#include "timer.h"

#include "pci.h"

#define GPU_CMD_GET_DISPLAY_INFO   0x0100
#define GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define GPU_CMD_SET_SCANOUT        0x0103
#define GPU_CMD_RESOURCE_FLUSH     0x0104
#define GPU_CMD_TRANSFER_TO_HOST   0x0105
#define GPU_CMD_ATTACH_BACKING     0x0106
#define GPU_RESP_OK_NODATA         0x1100
#define GPU_RESP_OK_DISPLAY_INFO   0x1101
#define GPU_FORMAT_B8G8R8X8        2       /* bytes B,G,R,X = our 0x00RRGGBB */
#define GPU_RESOURCE_ID            1
#define GPU_MAX_SCANOUTS           16

/* Largest screen we drive (2x HiDPI of a 1920x1200 layout); bigger host
 * sizes are halved. The runner asks for 2880x1800 under HVF (crisp 2x UI)
 * and 1440x900 under emulation. */
#define GPU_MAX_W  3840
#define GPU_MAX_H  2400

typedef struct { uint32_t type, flags; uint64_t fence_id; uint32_t ctx_id, padding; } PACKED GpuHdr;
typedef struct { uint32_t x, y, width, height; } PACKED GpuRect;
typedef struct { GpuHdr hdr; struct { GpuRect r; uint32_t enabled, flags; } modes[GPU_MAX_SCANOUTS]; } PACKED GpuDisplayInfo;
typedef struct { GpuHdr hdr; uint32_t resource_id, format, width, height; } PACKED GpuCreate2D;
typedef struct { GpuHdr hdr; uint32_t resource_id, nr_entries; uint64_t addr; uint32_t length, padding; } PACKED GpuAttach;
typedef struct { GpuHdr hdr; GpuRect r; uint32_t scanout_id, resource_id; } PACKED GpuSetScanout;
typedef struct { GpuHdr hdr; GpuRect r; uint64_t offset; uint32_t resource_id, padding; } PACKED GpuTransfer;
typedef struct { GpuHdr hdr; GpuRect r; uint32_t resource_id, padding; } PACKED GpuFlush;

static VirtQueueMem gpu_mem;
static VirtQueue ctrlq;
static union {
    GpuHdr hdr; GpuCreate2D create; GpuAttach attach; GpuSetScanout scanout;
} gpu_req __attribute__((aligned(16)));
static union { GpuHdr hdr; GpuDisplayInfo info; } gpu_resp __attribute__((aligned(16)));

static VirtioDev *gpu_handle;
static uint32_t gpu_w, gpu_h;
static uint32_t *gpu_pixels;
static bool gpu_ready, gpu_broken;
static uint64_t last_auto_flush;

/* Wait for `count` used entries; bounded so a wedged device can't hang us. */
static bool gpu_wait(int count)
{
    uint64_t deadline = virtio_now_ms() + 2000;
    while (count > 0) {
        if (virtio_poll_used(&ctrlq, NULL)) { count--; continue; }
        if (virtio_now_ms() > deadline) {
            if (!gpu_broken) serial_printf("[VIRTIO-GPU] Device stopped answering\n");
            gpu_broken = true;
            return false;
        }
    }
    return true;
}

/* One request/response command, synchronous. */
static uint32_t gpu_command(const void *req, uint32_t req_len, uint32_t resp_len)
{
    memset(&gpu_resp, 0, resp_len);
    ctrlq.free_head = 0;
    uint16_t head = virtio_desc_add(&ctrlq, (uint64_t)(uintptr_t)req, req_len, VRING_DESC_F_NEXT);
    virtio_desc_add(&ctrlq, (uint64_t)(uintptr_t)&gpu_resp, resp_len, VRING_DESC_F_WRITE);
    virtio_submit(&ctrlq, head);
    if (!gpu_wait(1)) return 0;
    return gpu_resp.hdr.type;
}

/* Screen updates are asynchronous: each update (TRANSFER_TO_HOST_2D +
 * RESOURCE_FLUSH, two chains) gets its own slot of 4 descriptors and its
 * own request/response buffers, so the CPU can go back to reading input
 * while the host copies pixels. A synchronous round trip per cursor move
 * used to cost several ms on a busy host. fb_present() calls
 * virtio_gpu_sync() before overwriting the scanout memory, so the host
 * never reads a half-copied frame. */
#define GPU_SLOTS 8
typedef struct {
    GpuTransfer xfer;
    GpuFlush flush;
    GpuHdr resp_xfer, resp_flush;
} __attribute__((aligned(16))) GpuSlot;
static GpuSlot gpu_slots[GPU_SLOTS];
static uint8_t gpu_slot_pending[GPU_SLOTS];      /* chains still with the host */
static int gpu_nslots, gpu_inflight;

static void gpu_reap(void)
{
    while (virtio_poll_used(&ctrlq, NULL)) {
        int slot = ctrlq.free_head / 4;           /* poll_used leaves the chain head here */
        if (slot < gpu_nslots && gpu_slot_pending[slot]) {
            gpu_slot_pending[slot]--;
            gpu_inflight--;
        }
    }
}

/* Wait (bounded) until no update is in flight, or until a slot is free. */
static int gpu_wait_slot(bool all)
{
    uint64_t deadline = virtio_now_ms() + 2000;
    for (;;) {
        gpu_reap();
        if (all ? gpu_inflight == 0 : true) {
            if (all) return 0;
            for (int i = 0; i < gpu_nslots; i++)
                if (!gpu_slot_pending[i]) return i;
        }
        if (virtio_now_ms() > deadline) {
            if (!gpu_broken) serial_printf("[VIRTIO-GPU] Device stopped answering\n");
            gpu_broken = true;
            return -1;
        }
    }
}

static void virtio_gpu_sync(void)
{
    if (gpu_ready && !gpu_broken && gpu_inflight) gpu_wait_slot(true);
}

/* Copy rectangle (x,y,w,h) of guest memory to the host and show it. */
static void gpu_update(int x, int y, int w, int h)
{
    if (!gpu_ready || gpu_broken) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)gpu_w) w = (int)gpu_w - x;
    if (y + h > (int)gpu_h) h = (int)gpu_h - y;
    if (w <= 0 || h <= 0) return;
    GpuRect r = { (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h };

    int slot = gpu_wait_slot(false);
    if (slot < 0) return;
    GpuSlot *s = &gpu_slots[slot];
    memset(s, 0, sizeof(*s));
    s->xfer.hdr.type = GPU_CMD_TRANSFER_TO_HOST;
    s->xfer.r = r;
    s->xfer.offset = ((uint64_t)y * gpu_w + (uint64_t)x) * 4;
    s->xfer.resource_id = GPU_RESOURCE_ID;
    s->flush.hdr.type = GPU_CMD_RESOURCE_FLUSH;
    s->flush.r = r;
    s->flush.resource_id = GPU_RESOURCE_ID;

    ctrlq.free_head = (uint16_t)(slot * 4);
    uint16_t a = virtio_desc_add(&ctrlq, (uint64_t)(uintptr_t)&s->xfer, sizeof(s->xfer), VRING_DESC_F_NEXT);
    virtio_desc_add(&ctrlq, (uint64_t)(uintptr_t)&s->resp_xfer, sizeof(GpuHdr), VRING_DESC_F_WRITE);
    uint16_t b = virtio_desc_add(&ctrlq, (uint64_t)(uintptr_t)&s->flush, sizeof(s->flush), VRING_DESC_F_NEXT);
    virtio_desc_add(&ctrlq, (uint64_t)(uintptr_t)&s->resp_flush, sizeof(GpuHdr), VRING_DESC_F_WRITE);
    gpu_slot_pending[slot] = 2;
    gpu_inflight += 2;
    virtio_submit(&ctrlq, a);
    virtio_submit(&ctrlq, b);
    if (gpu_nslots == 1) gpu_wait_slot(true);    /* tiny queue: stay synchronous */
}

/* Pick the screen size from the host's preferred mode for scanout 0. */
static void gpu_choose_size(uint32_t *w, uint32_t *h)
{
    *w = 1280; *h = 800;
    memset(&gpu_req, 0, sizeof(gpu_req));
    gpu_req.hdr.type = GPU_CMD_GET_DISPLAY_INFO;
    if (gpu_command(&gpu_req, sizeof(GpuHdr), sizeof(GpuDisplayInfo)) == GPU_RESP_OK_DISPLAY_INFO &&
        gpu_resp.info.modes[0].enabled && gpu_resp.info.modes[0].r.width >= 640 &&
        gpu_resp.info.modes[0].r.height >= 480) {
        *w = gpu_resp.info.modes[0].r.width;
        *h = gpu_resp.info.modes[0].r.height;
    }
    while (*w > GPU_MAX_W || *h > GPU_MAX_H) { *w /= 2; *h /= 2; }
    if (*w < 640 || *h < 480) { *w = 1280; *h = 800; }
    *w &= ~1u;                      /* fb_present copies two pixels at a time */
}

/* Create the 2D resource, attach it to guest RAM and show it. Shared by
 * the virtio-mmio and virtio-pci probes. */
static bool gpu_bringup(VirtioDev *dev, BootInfo *info)
{
    uint32_t w, h;
    gpu_choose_size(&w, &h);
    uint32_t *pixels = pmm_alloc_bytes((size_t)w * h * 4);
    if (!pixels) return false;

    memset(&gpu_req, 0, sizeof(gpu_req));
    gpu_req.create.hdr.type = GPU_CMD_RESOURCE_CREATE_2D;
    gpu_req.create.resource_id = GPU_RESOURCE_ID;
    gpu_req.create.format = GPU_FORMAT_B8G8R8X8;
    gpu_req.create.width = w;
    gpu_req.create.height = h;
    if (gpu_command(&gpu_req, sizeof(GpuCreate2D), sizeof(GpuHdr)) != GPU_RESP_OK_NODATA) goto fail;

    memset(&gpu_req, 0, sizeof(gpu_req));
    gpu_req.attach.hdr.type = GPU_CMD_ATTACH_BACKING;
    gpu_req.attach.resource_id = GPU_RESOURCE_ID;
    gpu_req.attach.nr_entries = 1;
    gpu_req.attach.addr = (uint64_t)(uintptr_t)pixels;
    gpu_req.attach.length = w * h * 4;
    if (gpu_command(&gpu_req, sizeof(GpuAttach), sizeof(GpuHdr)) != GPU_RESP_OK_NODATA) goto fail;

    memset(&gpu_req, 0, sizeof(gpu_req));
    gpu_req.scanout.hdr.type = GPU_CMD_SET_SCANOUT;
    gpu_req.scanout.r = (GpuRect){ 0, 0, w, h };
    gpu_req.scanout.scanout_id = 0;
    gpu_req.scanout.resource_id = GPU_RESOURCE_ID;
    if (gpu_command(&gpu_req, sizeof(GpuSetScanout), sizeof(GpuHdr)) != GPU_RESP_OK_NODATA) goto fail;

    gpu_pixels = pixels;
    gpu_w = w; gpu_h = h;
    gpu_ready = true;
    gpu_handle = dev;
    info->fb_addr = (uint64_t)(uintptr_t)pixels;
    info->fb_width = w;
    info->fb_height = h;
    info->fb_pitch = w * 4;
    info->fb_bpp = 32;
    fb_init(info);
    gpu_nslots = ctrlq.size / 4 < GPU_SLOTS ? ctrlq.size / 4 : GPU_SLOTS;
    if (gpu_nslots < 1) gpu_nslots = 1;
    fb_set_flush_hook(gpu_update);
    fb_set_sync_hook(virtio_gpu_sync);
    serial_printf("[VIRTIO-GPU] Display %dx%d @ %p attached (%s)\n",
        (int)w, (int)h, pixels,
        dev->kind == VIRTIO_TRANSPORT_PCI ? "PCI" : "virtio-mmio");
    return true;
fail:
    serial_printf("[VIRTIO-GPU] Display setup failed (resp 0x%x)\n", gpu_resp.hdr.type);
    return false;
}

bool virtio_gpu_display_init(BootInfo *info)
{
    /* These fixed slots exist only on QEMU virt. Reading them on VZ
     * aborts before the PCI GPU or the late serial sink can be reached. */
    for (int i = 0; platform.qemu && i < VIRTIO_MMIO_SLOTS && !gpu_ready; i++) {
        uint32_t base = VIRTIO_MMIO_BASE + (uint32_t)i * VIRTIO_MMIO_STRIDE;
        if (mmio_read32(base + VIO_MAGIC) != 0x74726976u || mmio_read32(base + VIO_VERSION) != 2 ||
            mmio_read32(base + VIO_DEVICE_ID) != VIRTIO_ID_GPU) continue;

        VirtioDev *dev = virtio_dev_mmio_slot(base);
        if (!dev) return false;
        dev->device_id = VIRTIO_ID_GPU;
        if (virtio_dev_begin(dev, 0) != 0) continue;
        if (virtio_dev_queue_init(dev, &ctrlq, &gpu_mem, 0) != 0) continue;
        virtio_dev_ready(dev);
        if (!gpu_bringup(dev, info)) return false;
        return true;
    }

    /* virtio-pci (Apple VZ): BARs are assigned by firmware; the display
     * comes up before the PCI bus scan so the splash can appear. */
    PciDev pci;
    if (pci_find(0x1af4, 0x1050, &pci) == 0) {
        VirtioDev *dev = NULL;
        if (virtio_dev_pci_setup(&pci, &dev) == 0) {
            pci_enable(&pci);
            if (virtio_dev_begin(dev, 0) == 0 &&
                virtio_dev_queue_init(dev, &ctrlq, &gpu_mem, 0) == 0) {
                virtio_dev_ready(dev);
                gpu_bringup(dev, info);
            }
        }
    }
    return gpu_ready;
}

bool virtio_gpu_owns(const VirtioDev *dev)
{
    return gpu_ready && dev == gpu_handle;
}

/* Main loop: direct (non-back-buffered) drawing such as the fallback
 * console has no present step, so push the whole screen ~20 times a
 * second while it is in use. */
void virtio_gpu_poll(void)
{
    if (!gpu_ready || fb_backbuffer_active()) return;
    uint64_t now = timer_get_ticks();
    if (now - last_auto_flush < 50) return;
    last_auto_flush = now;
    gpu_update(0, 0, (int)gpu_w, (int)gpu_h);
}
