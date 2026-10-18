/*
 * ojjyOS v3 Kernel - virtio-input Driver (aarch64)
 *
 * Drives QEMU's virtio-keyboard-device and virtio-tablet-device
 * (evdev-style event streams over virtio-mmio) and feeds the shared
 * input event queue used by the desktop:
 *
 *   EV_KEY  + keycode < 128   ->  keyboard events (ASCII translation)
 *   EV_KEY  + BTN_*           ->  mouse button events
 *   EV_ABS  + ABS_X/ABS_Y     ->  absolute framebuffer positions
 *   EV_REL  + REL_WHEEL        ->  scroll events
 *
 * Events are harvested by polling: the 1 kHz timer keeps the kernel
 * main loop alive, and a64_poll_input() drains the event queues.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "virtio.h"
#include "mmio.h"
#include "virtio_devices.h"
#include "drivers/input.h"
#include "serial.h"
#include "string.h"

/* virtio-input config selectors (per Linux virtio_input.h) */
#define VIRTIO_INPUT_CFG_ABS_INFO    0x12

/* evdev event types */
#define EV_SYN    0
#define EV_KEY    1
#define EV_REL    2
#define EV_ABS    3

/* mouse buttons */
#define BTN_LEFT     0x110
#define BTN_RIGHT    0x111
#define BTN_MIDDLE   0x112

/* relative axes */
#define REL_WHEEL    0x08

/* absolute axes */
#define ABS_X        0
#define ABS_Y        1

#define INPUT_EVENTS_PER_BUF    64
#define MAX_INPUT_DEVICES       4

typedef struct {
    uint16_t type;
    uint16_t code;
    int32_t value;
} PACKED VirtioInputEvent;

typedef struct {
    VirtQueue vq;
    bool has_pointer;
    int32_t abs_min_x, abs_max_x, abs_min_y, abs_max_y;
    int32_t last_px, last_py;
    bool have_x, have_y, position_pending;
    VirtioInputEvent events[INPUT_EVENTS_PER_BUF];
} InputDevState;

static InputDevState input_devs[MAX_INPUT_DEVICES];
static int input_dev_count = 0;

/* Linux keycode -> kernel KeyCode (full keyboard + modifiers + nav) */
static const KeyCode key_to_keycode[128] = {
    [1] = KEY_ESCAPE,
    [2] = KEY_1, [3] = KEY_2, [4] = KEY_3, [5] = KEY_4, [6] = KEY_5,
    [7] = KEY_6, [8] = KEY_7, [9] = KEY_8, [10] = KEY_9, [11] = KEY_0,
    [12] = KEY_MINUS, [13] = KEY_EQUALS,
    [14] = KEY_BACKSPACE,
    [15] = KEY_TAB,
    [16] = KEY_Q, [17] = KEY_W, [18] = KEY_E, [19] = KEY_R, [20] = KEY_T,
    [21] = KEY_Y, [22] = KEY_U, [23] = KEY_I, [24] = KEY_O, [25] = KEY_P,
    [26] = KEY_LBRACKET, [27] = KEY_RBRACKET,
    [28] = KEY_ENTER,
    [29] = KEY_LCTRL,
    [30] = KEY_A, [31] = KEY_S, [32] = KEY_D, [33] = KEY_F, [34] = KEY_G,
    [35] = KEY_H, [36] = KEY_J, [37] = KEY_K, [38] = KEY_L,
    [39] = KEY_SEMICOLON, [40] = KEY_QUOTE, [41] = KEY_BACKTICK,
    [42] = KEY_LSHIFT,
    [43] = KEY_BACKSLASH,
    [44] = KEY_Z, [45] = KEY_X, [46] = KEY_C, [47] = KEY_V, [48] = KEY_B,
    [49] = KEY_N, [50] = KEY_M,
    [51] = KEY_COMMA, [52] = KEY_PERIOD, [53] = KEY_SLASH,
    [54] = KEY_RSHIFT,
    [55] = KEY_KPMULTIPLY,
    [56] = KEY_LALT,
    [57] = KEY_SPACE,
    [58] = KEY_CAPSLOCK,
    /* F1..F12 = 59..68, 87, 88 */
    [59] = KEY_F1,  [60] = KEY_F2,  [61] = KEY_F3,
    [62] = KEY_F4,  [63] = KEY_F5,  [64] = KEY_F6,
    [65] = KEY_F7,  [66] = KEY_F8,  [67] = KEY_F9,
    [68] = KEY_F10, [69] = KEY_NUMLOCK, [70] = KEY_SCROLLLOCK,
    [87] = KEY_F11, [88] = KEY_F12,
    [96] = KEY_ENTER,
    [97] = KEY_RCTRL,
    [100] = KEY_RALT,
    [102] = KEY_HOME,
    [103] = KEY_UP,
    [104] = KEY_PAGEUP,
    [105] = KEY_LEFT,
    [106] = KEY_RIGHT,
    [107] = KEY_END,
    [108] = KEY_DOWN,
    [109] = KEY_PAGEDOWN,
    [110] = KEY_INSERT,
    [111] = KEY_DELETE,
    [125] = KEY_LSUPER,
    [126] = KEY_RSUPER,
};

/* Linux keycode -> ASCII (unshifted) */
static const char key_to_ascii[128] = {
    [2] = '1', [3] = '2', [4] = '3', [5] = '4', [6] = '5',
    [7] = '6', [8] = '7', [9] = '8', [10] = '9', [11] = '0',
    [12] = '-', [13] = '=', [14] = '\b', [15] = '\t',
    [16] = 'q', [17] = 'w', [18] = 'e', [19] = 'r', [20] = 't',
    [21] = 'y', [22] = 'u', [23] = 'i', [24] = 'o', [25] = 'p',
    [26] = '[', [27] = ']', [28] = '\n',
    [30] = 'a', [31] = 's', [32] = 'd', [33] = 'f', [34] = 'g',
    [35] = 'h', [36] = 'j', [37] = 'k', [38] = 'l',
    [39] = ';', [40] = '\'', [41] = '`',
    [43] = '\\',
    [44] = 'z', [45] = 'x', [46] = 'c', [47] = 'v', [48] = 'b',
    [49] = 'n', [50] = 'm',
    [51] = ',', [52] = '.', [53] = '/',
    [55] = '*', [57] = ' ', [96] = '\n',
};

/* Linux keycode -> ASCII (shifted) */
static const char key_to_ascii_shift[128] = {
    [2] = '!', [3] = '@', [4] = '#', [5] = '$', [6] = '%',
    [7] = '^', [8] = '&', [9] = '*', [10] = '(', [11] = ')',
    [12] = '_', [13] = '+', [14] = '\b', [15] = '\t',
    [16] = 'Q', [17] = 'W', [18] = 'E', [19] = 'R', [20] = 'T',
    [21] = 'Y', [22] = 'U', [23] = 'I', [24] = 'O', [25] = 'P',
    [26] = '{', [27] = '}', [28] = '\n',
    [30] = 'A', [31] = 'S', [32] = 'D', [33] = 'F', [34] = 'G',
    [35] = 'H', [36] = 'J', [37] = 'K', [38] = 'L',
    [39] = ':', [40] = '"', [41] = '~',
    [43] = '|',
    [44] = 'Z', [45] = 'X', [46] = 'C', [47] = 'V', [48] = 'B',
    [49] = 'N', [50] = 'M',
    [51] = '<', [52] = '>', [53] = '?',
    [55] = '*', [57] = ' ', [96] = '\n',
};

void virtio_input_handle_key(uint16_t code, int32_t value)
{
    if (value < 0 || value > 2) return;
    bool down = (value != 0);

    /* Pointer buttons */
    if (code == BTN_LEFT || code == BTN_RIGHT || code == BTN_MIDDLE) {
        MouseButton button = MOUSE_BUTTON_LEFT;
        if (code == BTN_RIGHT) button = MOUSE_BUTTON_RIGHT;
        if (code == BTN_MIDDLE) button = MOUSE_BUTTON_MIDDLE;
        input_post_mouse_button(
            down ? INPUT_EVENT_MOUSE_BUTTON_DOWN : INPUT_EVENT_MOUSE_BUTTON_UP,
            button);
        return;
    }

    if (code >= 128) {
        return;
    }

    KeyCode keycode = key_to_keycode[code];
    if (keycode == KEY_NONE) {
        return;
    }

    char ascii = 0;
    const char plain = key_to_ascii[code];
    if (plain) {
        uint8_t mods = input_get_modifiers();
        bool shifted = (mods & INPUT_MOD_SHIFT) != 0;
        if ((mods & INPUT_MOD_CAPSLOCK) && plain >= 'a' && plain <= 'z') {
            shifted = !shifted;
        }
        ascii = shifted ? key_to_ascii_shift[code] : plain;
    }

    input_post_key_event(
        value == 2 ? INPUT_EVENT_KEY_REPEAT :
            (down ? INPUT_EVENT_KEY_PRESS : INPUT_EVENT_KEY_RELEASE),
        (uint8_t)code, keycode, ascii);
}

static void handle_abs_event(InputDevState *dev, int code, int value)
{
    int32_t bounds_w = 1, bounds_h = 1;
    input_get_bounds(&bounds_w, &bounds_h);

    int32_t px = dev->last_px;
    int32_t py = dev->last_py;

    if (code == ABS_X && dev->abs_max_x > dev->abs_min_x) {
        int64_t v = MAX(dev->abs_min_x, MIN(value, dev->abs_max_x));
        px = (int32_t)(((v - dev->abs_min_x) * (bounds_w - 1)) /
                       ((int64_t)dev->abs_max_x - dev->abs_min_x));
        dev->have_x = true;
    } else if (code == ABS_Y && dev->abs_max_y > dev->abs_min_y) {
        int64_t v = MAX(dev->abs_min_y, MIN(value, dev->abs_max_y));
        py = (int32_t)(((v - dev->abs_min_y) * (bounds_h - 1)) /
                       ((int64_t)dev->abs_max_y - dev->abs_min_y));
        dev->have_y = true;
    } else {
        return;
    }

    dev->last_px = px;
    dev->last_py = py;
    dev->position_pending = true;
}

static void flush_pointer(InputDevState *dev)
{
    if (dev->position_pending && dev->have_x && dev->have_y) {
        input_post_mouse_absolute(dev->last_px, dev->last_py);
        dev->position_pending = false;
    }
}

static void handle_event(InputDevState *dev, const VirtioInputEvent *ev)
{
    switch (ev->type) {
        case EV_KEY:
            /* A button can precede SYN_REPORT in the same device batch. */
            flush_pointer(dev);
            virtio_input_handle_key(ev->code, ev->value);
            break;
        case EV_REL:
            if (ev->code == REL_WHEEL && ev->value != 0) {
                input_post_mouse_scroll(0, -ev->value);
            }
            break;
        case EV_ABS:
            if (dev->has_pointer) {
                handle_abs_event(dev, ev->code, ev->value);
            }
            break;
        case EV_SYN:
            if (ev->code == 0) flush_pointer(dev); /* SYN_REPORT */
            break;
        default:
            break;
    }
}

/*
 * Read the absinfo max value for one axis from the device config.
 */
static bool input_abs_range(VirtioDev *dev, uint8_t axis, int32_t *min, int32_t *max)
{
    virtio_dev_cfg_write8(dev, 0, VIRTIO_INPUT_CFG_ABS_INFO);   /* select */
    virtio_dev_cfg_write8(dev, 1, axis);                         /* subsel */
    virtio_memory_barrier();

    /* absinfo: min(u32), max(u32), fuzz, flat, res at cfg+8 */
    if (virtio_dev_cfg8(dev, 2) < 20) return false;
    *min = (int32_t)virtio_dev_cfg32(dev, 8);
    *max = (int32_t)virtio_dev_cfg32(dev, 12);
    return *max > *min;
}

int virtio_input_attach(VirtioDev *dev)
{
    if (input_dev_count >= MAX_INPUT_DEVICES) {
        return -1;
    }

    int idx = input_dev_count;
    InputDevState *dev_state = &input_devs[idx];
    memset(dev_state, 0, sizeof(*dev_state));

    /* Static queue memory per device */
    static VirtQueueMem input_queue_mem[MAX_INPUT_DEVICES];
    if (virtio_dev_queue_init(dev, &dev_state->vq, &input_queue_mem[idx], 0) != 0) {
        return -1;
    }

    /* A device reporting ABS ranges is a pointer (tablet) */
    if (input_abs_range(dev, ABS_X, &dev_state->abs_min_x, &dev_state->abs_max_x) &&
        input_abs_range(dev, ABS_Y, &dev_state->abs_min_y, &dev_state->abs_max_y)) {
        dev_state->has_pointer = true;
    }

    virtio_dev_ready(dev);

    /* Prime the queue with 64 individual 8-byte event descriptors
     * (QEMU virtio-input requires one descriptor per VirtioInputEvent,
     * and drops batches if fewer descriptors than batch_size are available). */
    uint16_t count = MIN(INPUT_EVENTS_PER_BUF, dev_state->vq.size);
    for (uint16_t b = 0; b < count; b++) {
        dev_state->vq.mem->desc[b].addr  = (uint64_t)(uintptr_t)&dev_state->events[b];
        dev_state->vq.mem->desc[b].len   = sizeof(VirtioInputEvent);
        dev_state->vq.mem->desc[b].flags = VRING_DESC_F_WRITE;
        dev_state->vq.mem->desc[b].next  = 0;
        dev_state->vq.mem->avail.ring[b] = b;
    }
    virtio_memory_barrier();
    dev_state->vq.avail_idx = count;
    dev_state->vq.mem->avail.idx = count;
    virtio_memory_barrier();
    virtio_notify(&dev_state->vq);

    serial_printf("[VIRTIO-INPUT] Device %d attached (%s, abs %dx%d)\n",
        idx, dev_state->has_pointer ? "pointer" : "keyboard",
        (int)dev_state->abs_max_x, (int)dev_state->abs_max_y);
    input_dev_count++;
    return 0;
}

#ifdef OJJY_HOST_TEST
/* Exercise the real decoder without pretending host memory is device MMIO. */
static InputDevState test_pointer;
void virtio_input_test_pointer_init(int32_t min_x, int32_t max_x, int32_t min_y, int32_t max_y)
{
    memset(&test_pointer, 0, sizeof(test_pointer));
    test_pointer.has_pointer = true;
    test_pointer.abs_min_x = min_x; test_pointer.abs_max_x = max_x;
    test_pointer.abs_min_y = min_y; test_pointer.abs_max_y = max_y;
}
void virtio_input_test_pointer_event(uint16_t type, uint16_t code, int32_t value)
{
    VirtioInputEvent event = {type, code, value};
    handle_event(&test_pointer, &event);
}
#endif

/*
 * Drain all pending events from every input device.
 */
void virtio_input_poll(void)
{
    for (int i = 0; i < input_dev_count; i++) {
        InputDevState *dev = &input_devs[i];
        VirtQueue *vq = &dev->vq;
        bool recycled = false;

        /* Bound each harvest so a busy device cannot starve the main loop.
         * Device-owned indices must be reloaded even under optimization. */
        for (unsigned budget = 0; budget < vq->size &&
             vq->used_idx != *(volatile uint16_t *)&vq->mem->used.idx; budget++) {
            virtio_memory_barrier();
            uint16_t u_slot = vq->used_idx & (vq->size - 1);
            uint32_t desc_id = vq->mem->used.elems[u_slot].id;
            vq->used_idx = (uint16_t)(vq->used_idx + 1);

            if (desc_id < MIN(INPUT_EVENTS_PER_BUF, vq->size)) {
                if (vq->mem->used.elems[u_slot].len == sizeof(VirtioInputEvent)) {
                    VirtioInputEvent event = dev->events[desc_id];
                    handle_event(dev, &event);
                }

                /* Re-publish this descriptor in the available ring */
                uint16_t a_slot = vq->avail_idx & (vq->size - 1);
                vq->mem->avail.ring[a_slot] = desc_id;
                virtio_memory_barrier();
                vq->avail_idx = (uint16_t)(vq->avail_idx + 1);
                vq->mem->avail.idx = vq->avail_idx;
                recycled = true;
            }
        }

        if (recycled) {
            virtio_memory_barrier();
            virtio_notify(vq);
        }
    }
}
