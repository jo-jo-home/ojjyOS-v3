/*
 * ojjyOS v3 Kernel - PS/2 Keyboard Driver
 *
 * Integrated with driver model and input event system.
 * Handles standard PS/2 keyboard on IRQ 1.
 */

#include "ps2_keyboard.h"
#include "driver.h"
#include "input.h"
#include "../serial.h"
#include "../idt.h"
#include "../types.h"

/* PS/2 ports */
#define PS2_DATA_PORT   0x60
#define PS2_STATUS_PORT 0x64

/* Modifier state */
static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool super_pressed = false;
static bool caps_lock = false;
static bool num_lock = false;

/* Forward declarations */
static bool ps2_kbd_probe(Driver *drv);
static int ps2_kbd_init(Driver *drv);
static int ps2_kbd_shutdown(Driver *drv);
static bool ps2_kbd_handle_irq(Driver *drv, uint8_t irq);

/* Driver operations */
static DriverOps ps2_kbd_ops = {
    .probe = ps2_kbd_probe,
    .init = ps2_kbd_init,
    .shutdown = ps2_kbd_shutdown,
    .handle_irq = ps2_kbd_handle_irq,
};

/* Driver instance */
static Driver ps2_kbd_driver = {
    .name = "ps2_keyboard",
    .description = "PS/2 Keyboard Driver",
    .version = DRIVER_VERSION(1, 0, 0),
    .type = DRIVER_TYPE_INPUT,
    .flags = DRIVER_FLAG_CRITICAL,  /* Keyboard is critical */
    .ops = &ps2_kbd_ops,
};

/* External functions */
extern void pic_enable_irq(uint8_t irq);
extern void pic_disable_irq(uint8_t irq);

/* Scancode to KeyCode mapping */
static const KeyCode scancode_to_keycode[128] = {
    [0x01] = KEY_ESCAPE,
    [0x02] = KEY_1, [0x03] = KEY_2, [0x04] = KEY_3, [0x05] = KEY_4,
    [0x06] = KEY_5, [0x07] = KEY_6, [0x08] = KEY_7, [0x09] = KEY_8,
    [0x0A] = KEY_9, [0x0B] = KEY_0,
    [0x0C] = KEY_MINUS, [0x0D] = KEY_EQUALS, [0x0E] = KEY_BACKSPACE,
    [0x0F] = KEY_TAB,
    [0x10] = KEY_Q, [0x11] = KEY_W, [0x12] = KEY_E, [0x13] = KEY_R,
    [0x14] = KEY_T, [0x15] = KEY_Y, [0x16] = KEY_U, [0x17] = KEY_I,
    [0x18] = KEY_O, [0x19] = KEY_P,
    [0x1A] = KEY_LBRACKET, [0x1B] = KEY_RBRACKET, [0x1C] = KEY_ENTER,
    [0x1D] = KEY_LCTRL,
    [0x1E] = KEY_A, [0x1F] = KEY_S, [0x20] = KEY_D, [0x21] = KEY_F,
    [0x22] = KEY_G, [0x23] = KEY_H, [0x24] = KEY_J, [0x25] = KEY_K,
    [0x26] = KEY_L,
    [0x27] = KEY_SEMICOLON, [0x28] = KEY_QUOTE, [0x29] = KEY_BACKTICK,
    [0x2A] = KEY_LSHIFT, [0x2B] = KEY_BACKSLASH,
    [0x2C] = KEY_Z, [0x2D] = KEY_X, [0x2E] = KEY_C, [0x2F] = KEY_V,
    [0x30] = KEY_B, [0x31] = KEY_N, [0x32] = KEY_M,
    [0x33] = KEY_COMMA, [0x34] = KEY_PERIOD, [0x35] = KEY_SLASH,
    [0x36] = KEY_RSHIFT,
    [0x37] = KEY_KPMULTIPLY,
    [0x38] = KEY_LALT,
    [0x39] = KEY_SPACE,
    [0x3A] = KEY_CAPSLOCK,
    [0x3B] = KEY_F1, [0x3C] = KEY_F2, [0x3D] = KEY_F3, [0x3E] = KEY_F4,
    [0x3F] = KEY_F5, [0x40] = KEY_F6, [0x41] = KEY_F7, [0x42] = KEY_F8,
    [0x43] = KEY_F9, [0x44] = KEY_F10,
    [0x45] = KEY_NUMLOCK, [0x46] = KEY_SCROLLLOCK,
    [0x47] = KEY_HOME, [0x48] = KEY_UP, [0x49] = KEY_PAGEUP,
    [0x4A] = KEY_KPMINUS,
    [0x4B] = KEY_LEFT, [0x4C] = KEY_KP5, [0x4D] = KEY_RIGHT,
    [0x4E] = KEY_KPPLUS,
    [0x4F] = KEY_END, [0x50] = KEY_DOWN, [0x51] = KEY_PAGEDOWN,
    [0x52] = KEY_INSERT, [0x53] = KEY_DELETE,
    [0x57] = KEY_F11, [0x58] = KEY_F12,
};

/* Scancode to ASCII (unshifted) */
static const char scancode_to_ascii[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0,
};

/* Scancode to ASCII (shifted) */
static const char scancode_to_ascii_shift[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0,
};

/* Special scancodes */
#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_LCTRL    0x1D
#define SC_LALT     0x38
#define SC_CAPS     0x3A
#define SC_NUMLOCK  0x45

/*
 * Update modifier state and push to input subsystem
 */
static void update_modifiers(void)
{
    uint8_t mods = 0;
    if (shift_pressed) mods |= INPUT_MOD_SHIFT;
    if (ctrl_pressed)  mods |= INPUT_MOD_CTRL;
    if (alt_pressed)   mods |= INPUT_MOD_ALT;
    if (super_pressed) mods |= INPUT_MOD_SUPER;
    if (caps_lock)     mods |= INPUT_MOD_CAPSLOCK;
    if (num_lock)      mods |= INPUT_MOD_NUMLOCK;

    input_set_modifiers(mods);
}

/*
 * Probe for keyboard
 */
static bool ps2_kbd_probe(Driver *drv)
{
    (void)drv;
    serial_printf("[PS2_KBD] Probing for PS/2 keyboard...\n");

    /* Clear any pending data */
    while (inb(PS2_STATUS_PORT) & 0x01) {
        inb(PS2_DATA_PORT);
    }

    /* Keyboard is assumed present (standard PC hardware) */
    serial_printf("[PS2_KBD] Keyboard detected\n");
    return true;
}

/*
 * Initialize keyboard
 */
static int ps2_kbd_init(Driver *drv)
{
    serial_printf("[PS2_KBD] Initializing...\n");

    /* Reset modifier state */
    shift_pressed = false;
    ctrl_pressed = false;
    alt_pressed = false;
    super_pressed = false;
    caps_lock = false;
    num_lock = false;
    update_modifiers();

    /* Register IRQ handler */
    driver_register_irq(drv, 1);  /* Keyboard is IRQ 1 */

    /* Enable IRQ 1 */
    pic_enable_irq(1);

    serial_printf("[PS2_KBD] Keyboard initialized\n");
    return 0;
}

/*
 * Shutdown keyboard
 */
static int ps2_kbd_shutdown(Driver *drv)
{
    (void)drv;
    serial_printf("[PS2_KBD] Shutting down...\n");

    pic_disable_irq(1);
    return 0;
}

/*
 * Handle keyboard IRQ
 */
static bool ps2_kbd_handle_irq(Driver *drv, uint8_t irq)
{
    (void)drv;
    (void)irq;

    uint8_t scancode = inb(PS2_DATA_PORT);

    /* Check for key release */
    bool released = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;

    /* Handle modifier keys */
    switch (code) {
        case SC_LSHIFT:
        case SC_RSHIFT:
            shift_pressed = !released;
            update_modifiers();
            return true;
        case SC_LCTRL:
            ctrl_pressed = !released;
            update_modifiers();
            return true;
        case SC_LALT:
            alt_pressed = !released;
            update_modifiers();
            return true;
        case SC_CAPS:
            if (!released) caps_lock = !caps_lock;
            update_modifiers();
            return true;
        case SC_NUMLOCK:
            if (!released) num_lock = !num_lock;
            update_modifiers();
            return true;
    }

    /* Get keycode */
    KeyCode keycode = KEY_NONE;
    if (code < 128) {
        keycode = scancode_to_keycode[code];
    }

    /* Convert to ASCII */
    char ascii = 0;
    if (code < 128) {
        bool shifted = shift_pressed;

        /* Caps lock affects letters */
        if (caps_lock) {
            char c = scancode_to_ascii[code];
            if (c >= 'a' && c <= 'z') {
                shifted = !shifted;
            }
        }

        ascii = shifted ? scancode_to_ascii_shift[code]
                        : scancode_to_ascii[code];
    }

    /* Post event */
    InputEventType type = released ? INPUT_EVENT_KEY_RELEASE : INPUT_EVENT_KEY_PRESS;
    input_post_key_event(type, scancode, keycode, ascii);

    /* Debug output for key press */
    if (!released) {
        if (ascii >= 32 && ascii < 127) {
            serial_printf("[PS2_KBD] Key: '%c' (scan=0x%x, key=%d)\n",
                         ascii, scancode, keycode);
        } else {
            serial_printf("[PS2_KBD] Key: scan=0x%x, key=%d\n", scancode, keycode);
        }
    }

    return true;
}

/*
 * Get driver
 */
Driver *ps2_keyboard_get_driver(void)
{
    return &ps2_kbd_driver;
}

/*
 * Initialize (register driver)
 */
void ps2_keyboard_init(void)
{
    driver_register(&ps2_kbd_driver);
}
