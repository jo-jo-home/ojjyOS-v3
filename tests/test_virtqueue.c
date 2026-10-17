/* Actual transport with a host MMIO boundary. Created by Jonas Lee. */
#include "arch/aarch64/virtio.h"
#include "arch/aarch64/virtio_devices.h"
#include "test_support.h"

static uint32_t offered, selected;
uint32_t virtio_host_mmio_read(uint32_t base, uint32_t off)
{ (void)base; return off == VIO_QUEUE_NUM_MAX ? offered : 0; }
void virtio_host_mmio_write(uint32_t base, uint32_t off, uint32_t value)
{ (void)base; if (off == VIO_QUEUE_NUM) selected = value; }
int virtio_blk_attach(uint32_t base, bool flush) { (void)base; (void)flush; return -1; }
int virtio_input_attach(uint32_t base) { (void)base; return -1; }

int main(void)
{
    static VirtQueueMem memory;
    VirtQueue queue;
    const uint16_t sizes[] = {8, 64, 128};
    for (unsigned test = 0; test < ARRAY_SIZE(sizes); test++) {
        offered = sizes[test];
        CHECK(virtio_queue_init(&queue, 0, &memory) == 0 && queue.size == offered && selected == offered,
              "transport records actual negotiated size");
        /* More than 64 completions reproduced the old QEMU input mismatch.
         * Also cross the 16-bit index wrap, which is independent of depth. */
        for (unsigned i = 0; i < 65536 + 257; i++) {
            uint16_t head = (uint16_t)(i % queue.size);
            virtio_submit(&queue, head);
            if (memory.avail.ring[(uint16_t)i & (queue.size - 1)] != head) {
                CHECK(false, "available ring wraps at negotiated depth"); break;
            }
            memory.used.elems[(uint16_t)i & (queue.size - 1)].id = head;
            memory.used.elems[(uint16_t)i & (queue.size - 1)].len = 8;
            memory.used.idx = (uint16_t)(i + 1);
            uint32_t length = 0;
            if (!virtio_poll_used(&queue, &length) || length != 8 || queue.free_head != head) {
                CHECK(false, "used ring wraps at negotiated depth"); break;
            }
        }
        CHECK(queue.used_idx == (uint16_t)(65536 + 257) && queue.avail_idx == queue.used_idx,
              "ring survives repeated depth and 16-bit index wraps");
        queue.free_head = queue.size;
        CHECK(virtio_desc_add(&queue, 0, 8, 0) == 0xFFFF, "descriptor bounds follow negotiated depth");
    }
    offered = 0;
    CHECK(virtio_queue_init(&queue, 0, &memory) == -1, "absent queue rejected");
    return finish();
}
