/* Real PCI transport at a host MMIO boundary. Created by Jonas Lee. */
#include "arch/aarch64/virtio.h"
#include "arch/aarch64/virtio_devices.h"
#include "arch/aarch64/pci.h"
#include "arch/aarch64/mmio.h"
#include "test_support.h"

#define ECAM 0x40000000ULL
#define BAR  0x100020000ULL
Platform platform = { .ecam = ECAM };
static uint32_t config[64];
static uint8_t status;
static uint16_t selected_queue, queue_size, enabled, notified;
static uint32_t feature_select, driver_select, driver_features[2];
static uint64_t queue_desc, queue_avail, queue_used;
static unsigned status_writes, ack_seen, reset_reads;
static bool reject_features, stuck_reset;

uint32_t mmio_read32(uintptr_t a)
{
    if (a >= ECAM && a < ECAM + 256) return config[(a - ECAM) / 4];
    if (a == BAR + 4 && status >= 3) return feature_select ? 1u : (2u | (1u << 5) | (1u << 3));
    return 0;
}
void mmio_write32(uintptr_t a, uint32_t v)
{
    if (a >= ECAM && a < ECAM + 256) { config[(a - ECAM) / 4] = v; return; }
    if (a == BAR) feature_select = v;
    if (a == BAR + 8) driver_select = v;
    if (a == BAR + 12 && driver_select < 2) driver_features[driver_select] = v;
    uint64_t *field = NULL;
    if (a == BAR + 0x20 || a == BAR + 0x24) field = &queue_desc;
    if (a == BAR + 0x28 || a == BAR + 0x2C) field = &queue_avail;
    if (a == BAR + 0x30 || a == BAR + 0x34) field = &queue_used;
    if (field) {
        if (a & 4) *field = (*field & 0xFFFFFFFFULL) | ((uint64_t)v << 32);
        else *field = (*field & 0xFFFFFFFF00000000ULL) | v;
    }
}
uint16_t mmio_read16(uintptr_t a)
{
    if (a == BAR + 0x12) return 4;
    if (a == BAR + 0x18) return selected_queue < 4 ? 64 : 0;
    if (a == BAR + 0x1E) return selected_queue * 2;
    if (a == BAR + 0x800A) return 1500;
    return 0;
}
void mmio_write16(uintptr_t a, uint16_t v)
{
    if (a == BAR + 0x16) selected_queue = v;
    if (a == BAR + 0x18) queue_size = v;
    if (a == BAR + 0x1C) enabled = v;
    if (a == BAR + 0x4000 + selected_queue * 8) notified = v;
}
uint8_t mmio_read8(uintptr_t a)
{
    if (a == BAR + 0x14) {
        if (stuck_reset) return 15;
        if (reset_reads) { reset_reads--; return 15; }
        return status;
    }
    return 0;
}
void mmio_write8(uintptr_t a, uint8_t v)
{
    if (a != BAR + 0x14) return;
    status_writes++;
    if (!v) { status = 0; ack_seen = 0; reset_reads = 2; }
    else if (v == 1 && !status && !reset_reads) { status = 1; ack_seen++; }
    else if (v == 3 && ack_seen) status = 3;
    else if (v == 11 && ack_seen) status = reject_features ? 3 : 11;
    else if (v == 15 && status == 11) status = 15;
    /* VZ-like boundary: a combined ACK|DRIVER without the ACK step is
     * ignored, so the former implementation fails this fixture. */
}
uint32_t virtio_host_mmio_read(uint32_t b, uint32_t o) { (void)b; (void)o; return 0; }
void virtio_host_mmio_write(uint32_t b, uint32_t o, uint32_t v) { (void)b; (void)o; (void)v; }
int virtio_blk_attach(VirtioDev *d, bool f) { (void)d; (void)f; return -1; }
int virtio_input_attach(VirtioDev *d) { (void)d; return -1; }
int virtio_net_attach(VirtioDev *d, bool m) { (void)d; (void)m; return -1; }
int virtio_rng_attach(VirtioDev *d) { (void)d; return -1; }
int virtio_console_attach(VirtioDev *d) { (void)d; return -1; }
bool virtio_gpu_owns(const VirtioDev *d) { (void)d; return false; }

static void capability(unsigned at, unsigned next, unsigned type, unsigned off, unsigned len)
{
    config[at / 4] = 9 | (next << 8) | ((type == 2 ? 20u : 16u) << 16) | (type << 24);
    config[at / 4 + 1] = 0; /* BAR 0 */
    config[at / 4 + 2] = off;
    config[at / 4 + 3] = len;
}

int main(void)
{
    CHECK(platform_table_ips(0) == 0, "32-bit PA capability retained");
    CHECK(platform_table_ips(1) == 1, "36-bit VZ capability never truncated to 32 bits");
    CHECK(platform_table_ips(2) == 2 && platform_table_ips(5) == 2,
          "40-bit table choice capped to supported layout");
    config[0] = 0x10431AF4;
    config[1] = 1u << 20; /* capability list */
    config[4] = 0x20004; config[5] = 1; /* 64-bit BAR above 4 GiB */
    config[0x34 / 4] = 0x40;
    capability(0x40, 0x50, 1, 0, 0x38);
    capability(0x50, 0x64, 2, 0x4000, 0x40);
    config[0x60 / 4] = 4; /* notify multiplier */
    capability(0x64, 0x74, 3, 0x1000, 1);
    capability(0x74, 0, 4, 0x8000, 12);
    PciDev pci;
    CHECK(pci_find(0x1AF4, 0x1043, &pci) == 0, "real ECAM PCI lookup");
    CHECK(pci.bars[0] == BAR && pci.bar_64[0], "64-bit BAR decoded without truncation");
    pci_enable(&pci);
    CHECK((config[1] & 6) == 6, "memory and bus mastering enabled");
    VirtioDev *dev = NULL;
    CHECK(virtio_dev_pci_setup(&pci, &dev) == 0 && dev->common == BAR,
          "modern capability walk locates common configuration");
    CHECK(dev->devcfg == BAR + 0x8000 && dev->notify_mult == 4, "device/notify capabilities decoded");
    CHECK(virtio_dev_offered(dev, 0) == 0, "uninitialized VZ-like device hides its features");
    CHECK(virtio_dev_negotiate(dev, 2) == 0 && ack_seen == 1 && status_writes == 4,
          "reset completion then separate ACK, DRIVER and FEATURES_OK");
    CHECK(driver_features[0] == 2 && driver_features[1] == 1 && dev->features == 2,
          "features read after DRIVER; multiport and VERSION_1 negotiated");
    CHECK(virtio_dev_num_queues(dev) == 4, "queue count read at correct access width");
    static VirtQueueMem mem;
    VirtQueue q;
    CHECK(virtio_dev_queue_init(dev, &q, &mem, 3) == 0 && q.size == 64 && queue_size == 64,
          "PCI queue depth negotiated at 64, not storage capacity");
    CHECK(enabled == 1 && queue_desc == (uintptr_t)mem.desc &&
          queue_avail == (uintptr_t)&mem.avail && queue_used == (uintptr_t)&mem.used,
          "all three queue addresses programmed before enabling");
    CHECK(q.notify_addr == BAR + 0x4000 + 3 * 2 * 4, "per-queue notify offset multiplied correctly");
    notified = 99;
    virtio_notify(&q);
    CHECK(notified == 3, "control-out notifies its own queue");
    virtio_dev_ready(dev);
    CHECK(status == 15, "DRIVER_OK preserves prior status flags");
    CHECK(virtio_dev_cfg16(dev, 10) == 1500, "network MTU uses a 16-bit config read");
    CHECK(virtio_dev_queue_init(dev, &q, &mem, 4) != 0, "absent queue fails without publishing storage");
    reject_features = true;
    CHECK(virtio_dev_negotiate(dev, 2) != 0, "rejected FEATURES_OK detected");
    reject_features = false;
    CHECK(virtio_dev_negotiate(dev, 0xFFFFFFFF) == 0 &&
          driver_features[0] == (2u | (1u << 5) | (1u << 3)),
          "supported mask intersected with offered bits after reset");
    stuck_reset = true;
    CHECK(virtio_dev_negotiate(dev, 2) != 0, "stuck reset has a bounded deadline");
    stuck_reset = false;
    /* RNG is legal without a DEVICE capability. Use a distinct PCI id so
     * this does not return the already-cached console handle. */
    pci.device = 0x1044;
    capability(0x64, 0, 3, 0x1000, 1);
    CHECK(virtio_dev_pci_setup(&pci, &dev) == 0 && !dev->devcfg,
          "entropy device does not require nonexistent device configuration");
    return finish();
}
