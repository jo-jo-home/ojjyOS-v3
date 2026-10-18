/*
 * ojjyOS v3 Kernel - virtio-console driver (aarch64)
 *
 * Apple's Virtualization framework has no PL011 UART: the kernel log goes
 * out through the PCI console device. Apple's device is a multiport
 * virtio-serial exactly like Linux's drivers/char/virtio_console.c:
 *
 *   queue 0 = receiveq0   (host -> guest data; we post RX buffers)
 *   queue 1 = transmitq0  (guest -> host data)
 *   queue 2 = control-in   (host -> guest control messages)
 *   queue 3 = control-out  (guest -> host control messages)
 *
 * Apple's binary says it plainly: "Cannot transmit data for closed port
 * id." The host drops data for a port until the guest has sent
 * VIRTIO_CONSOLE_DEVICE_READY, then VIRTIO_CONSOLE_PORT_READY(1) and
 * VIRTIO_CONSOLE_PORT_OPEN(1) on the control-out queue (Apple does not
 * implement the num_queues register, so detect the layout by device id
 * instead: every console we attach on PCI is Apple's).
 *
 * Registering with serial_set_sink() replays the boot-log ring, so the
 * host's serial file receives everything from the banner onward. Transmit
 * is synchronous: one descriptor per batch, polled with a wall-clock
 * deadline (never a spin count).
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "virtio.h"
#include "virtio_devices.h"
#include "serial.h"
#include "string.h"
#include "memory.h"

/* virtio-serial control messages (Linux uapi linux/virtio_console.h) */
#define VIRTIO_CONSOLE_DEVICE_READY  0
#define VIRTIO_CONSOLE_PORT_READY    3
#define VIRTIO_CONSOLE_PORT_OPEN     6
typedef struct {
    uint32_t id;         /* port number */
    uint16_t event;
    uint16_t value;
} PACKED ConsoleControl;

#define CONSOLE_TX_LIMIT       256   /* flush threshold without newline */
#define CONSOLE_CTRL_TIMEOUT_MS 2000 /* control round-trip deadline */
#define CONSOLE_DATA_TIMEOUT_MS 100  /* data flush deadline */
#define CONSOLE_GIVEUP_LIMIT   3      /* consecutive timeouts before muting */

static VirtQueueMem console_tx_mem, console_ctrl_mem, console_ctrl_in_mem;
static VirtQueue console_txq, console_ctrlq, console_ctrl_in;
static uint8_t console_ctrl_in_buf[128] __attribute__((aligned(16)));
static uint8_t *console_buf;        /* 4 KiB, from the page allocator */
static uint32_t console_len;
static bool console_attached, console_muted, ctrl_ready;
static unsigned console_timeouts;

static void console_flush(void)
{
    if (!console_attached || console_muted || console_len == 0) {
        console_len = 0;
        return;
    }

    console_txq.free_head = 0;
    uint16_t head = virtio_desc_add(&console_txq,
        (uint64_t)(uintptr_t)console_buf, console_len, 0);
    if (head == 0xFFFF) return;

    virtio_submit(&console_txq, head);

    uint64_t deadline = virtio_now_ms() + CONSOLE_DATA_TIMEOUT_MS;
    while (!virtio_poll_used(&console_txq, NULL)) {
        if (virtio_now_ms() > deadline) {
            if (++console_timeouts >= CONSOLE_GIVEUP_LIMIT) {
                console_muted = true;   /* Dead device: drop, never hang */
            }
            break;
        }
    }
    console_len = 0;
}

/* Serial sink: called for every byte the kernel prints, including the
 * boot-log replay that serial_set_sink() performs at registration. */
static void console_sink(char c)
{
    if (!console_buf) return;
    if (console_len >= 4096) console_flush();
    console_buf[console_len++] = (uint8_t)c;
    if (c == '\n' || console_len >= CONSOLE_TX_LIMIT) console_flush();
}

/* Send one control message on the control-out queue and wait for the
 * device to hand the descriptor back, exactly like Linux's
 * __send_control_msg(). */
static bool console_send_control(uint16_t event, uint16_t value)
{
    static ConsoleControl msg __attribute__((aligned(16)));
    if (!ctrl_ready) return false;
    msg.id = 0;
    msg.event = event;
    msg.value = value;

    console_ctrlq.free_head = 0;
    uint16_t head = virtio_desc_add(&console_ctrlq,
        (uint64_t)(uintptr_t)&msg, sizeof(msg), 0);
    if (head == 0xFFFF) return false;
    virtio_submit(&console_ctrlq, head);

    uint64_t deadline = virtio_now_ms() + CONSOLE_CTRL_TIMEOUT_MS;
    while (!virtio_poll_used(&console_ctrlq, NULL)) {
        if (virtio_now_ms() > deadline) return false;
    }
    return true;
}

int virtio_console_attach(VirtioDev *dev)
{
    if (console_attached) return -1;

    if (virtio_dev_queue_init(dev, &console_txq, &console_tx_mem, 1) != 0 ||
        virtio_dev_queue_init(dev, &console_ctrl_in, &console_ctrl_in_mem, 2) != 0 ||
        virtio_dev_queue_init(dev, &console_ctrlq, &console_ctrl_mem, 3) != 0) {
        return -1;
    }
    virtio_dev_ready(dev);
    ctrl_ready = true;

    /* Give the host a buffer for its control replies (PORT_ADD, PORT_NAME,
     * RESIZE), like fill_queue() in Linux. */
    console_ctrl_in.free_head = 0;
    uint16_t rxh = virtio_desc_add(&console_ctrl_in,
        (uint64_t)(uintptr_t)console_ctrl_in_buf,
        sizeof(console_ctrl_in_buf), VRING_DESC_F_WRITE);
    if (rxh != 0xFFFF) virtio_submit(&console_ctrl_in, rxh);

    /* Open port 0 (the serial attachment). */
    if (!console_send_control(VIRTIO_CONSOLE_DEVICE_READY, 0) ||
        !console_send_control(VIRTIO_CONSOLE_PORT_READY, 1) ||
        !console_send_control(VIRTIO_CONSOLE_PORT_OPEN, 1)) {
        serial_printf("[VIRTIO-CONSOLE] Control queue setup failed\n");
        return -1;
    }

    console_buf = pmm_alloc_bytes(4096);
    if (!console_buf) return -1;

    console_attached = true;
    serial_set_sink(console_sink);     /* replays the whole boot log */
    serial_printf("[VIRTIO-CONSOLE] Serial output attached (virtio-serial port 0)\n");
    return 0;
}

void virtio_console_poll(void)
{
    /* Push partial lines so progress output without newlines still flows. */
    if (console_attached && !console_muted && console_len) console_flush();
}
