/* Exercise the actual evdev and PS/2 decoders and shared queue. */
#include "drivers/input.h"
#include "drivers/ps2_keyboard.h"
#include "arch/aarch64/virtio.h"
#include "arch/aarch64/virtio_devices.h"
#include "test_support.h"

static uint64_t now;
uint64_t timer_get_ticks(void) { return now; }
int driver_register(Driver *driver) { (void)driver; return 0; }
void driver_register_irq(Driver *driver, uint8_t irq) { (void)driver; (void)irq; }
void pic_enable_irq(uint8_t irq) { (void)irq; }
void pic_disable_irq(uint8_t irq) { (void)irq; }
int virtio_queue_init(VirtQueue *q, uint32_t base, VirtQueueMem *mem)
{ (void)q; (void)base; (void)mem; return -1; }
void virtio_dev_ready(uint32_t base) { (void)base; }

static InputEvent event;
static void drain(void) { while (input_poll_event(&event)) {} }
static void key(int code, int value) { virtio_input_handle_key((uint16_t)code, value); }
static void extended(uint8_t code) { ps2_keyboard_handle_scancode(0xE0); ps2_keyboard_handle_scancode(code); }

int main(void)
{
    input_init();
    const struct { int raw; KeyCode expected; } mappings[] = {
        {14, KEY_BACKSPACE}, {102, KEY_HOME}, {103, KEY_UP}, {104, KEY_PAGEUP},
        {105, KEY_LEFT}, {106, KEY_RIGHT}, {107, KEY_END}, {108, KEY_DOWN},
        {109, KEY_PAGEDOWN}, {110, KEY_INSERT}, {111, KEY_DELETE},
        {97, KEY_RCTRL}, {100, KEY_RALT}, {125, KEY_LSUPER}, {126, KEY_RSUPER},
        {69, KEY_NUMLOCK}, {70, KEY_SCROLLLOCK}, {87, KEY_F11}, {88, KEY_F12}
    };
    for (unsigned i = 0; i < ARRAY_SIZE(mappings); i++) {
        key(mappings[i].raw, 1);
        CHECK(input_poll_event(&event) && event.type == INPUT_EVENT_KEY_PRESS &&
              event.key.keycode == mappings[i].expected, "evdev key mapping");
        key(mappings[i].raw, 0);
        CHECK(input_poll_event(&event) && event.type == INPUT_EVENT_KEY_RELEASE &&
              event.key.keycode == mappings[i].expected, "evdev release mapping");
    }
    input_init();
    key(24, 1);
    CHECK(input_poll_event(&event) && event.key.ascii == 'o', "first o is delivered");
    for (int i = 0; i < 2000; i++) key(24, 1);
    CHECK(!input_has_event(), "duplicate downs cannot flood queue before repeat delay");
    now = 449; key(24, 2);
    CHECK(!input_has_event(), "repeat waits 450ms");
    now = 450; key(24, 2);
    CHECK(input_poll_event(&event) && event.type == INPUT_EVENT_KEY_REPEAT &&
          event.key.ascii == 'o', "delayed repeat remains usable");
    now = 451; key(24, 2);
    CHECK(!input_has_event(), "repeat interval bounded");
    now = 485; key(24, 1);
    CHECK(input_poll_event(&event) && event.type == INPUT_EVENT_KEY_REPEAT, "duplicate down classified as repeat");
    key(24, 0); drain();
    now = 1000; key(24, 2);
    CHECK(!input_has_event() && !input_is_key_down(KEY_O), "repeat after release ignored");
    for (int i = 0; i < 2; i++) { key(24, 1); key(24, 0); }
    int presses = 0;
    while (input_poll_event(&event)) if (event.type == INPUT_EVENT_KEY_PRESS) presses++;
    CHECK(presses == 2, "deliberate repeated presses preserved");
    key(14, 1); drain();
    now += 450; key(14, 2);
    CHECK(input_poll_event(&event) && event.key.ascii == '\b' &&
          event.key.keycode == KEY_BACKSPACE, "Backspace repeats as Backspace, not stale o");
    key(14, 0); drain();

    key(42, 1); key(54, 1); key(42, 0); drain();
    CHECK(input_get_modifiers() & INPUT_MOD_SHIFT, "right Shift survives left Shift release");
    key(24, 1);
    CHECK(input_poll_event(&event) && event.key.ascii == 'O', "remaining Shift translates uppercase");
    key(24, 0); key(54, 0); drain();
    CHECK(!(input_get_modifiers() & INPUT_MOD_SHIFT), "both Shift releases clear modifier");
    key(58, 1); key(58, 1); key(58, 2); key(58, 0); drain();
    CHECK(input_get_modifiers() & INPUT_MOD_CAPSLOCK, "CapsLock toggles only on initial press");
    key(24, 1);
    CHECK(input_poll_event(&event) && event.key.ascii == 'O', "CapsLock affects letters");
    key(24, 0); drain();
    key(125, 1); key(24, 1); drain();
    now += 1000; key(24, 1);
    CHECK(!input_has_event(), "held shortcuts do not repeat");
    key(24, 0); key(125, 0); drain();
    key(28, 1); drain(); now += 1000; key(28, 1);
    CHECK(!input_has_event(), "Enter cannot repeatedly submit setup or shell command");
    key(28, 0); drain();
    key(65535, 1); key(24, -1); key(24, 3);
    CHECK(!input_has_event(), "invalid hardware events ignored");

    input_init();
    ps2_keyboard_handle_scancode(0x2A); ps2_keyboard_handle_scancode(0x36);
    ps2_keyboard_handle_scancode(0xAA); drain();
    CHECK(input_get_modifiers() & INPUT_MOD_SHIFT, "PS/2 also tracks both Shift keys");
    ps2_keyboard_handle_scancode(0x18);
    CHECK(input_poll_event(&event) && event.key.ascii == 'O', "PS/2 uppercase o");
    ps2_keyboard_handle_scancode(0x98); ps2_keyboard_handle_scancode(0xB6); drain();
    extended(0x53);
    CHECK(input_poll_event(&event) && event.key.keycode == KEY_DELETE, "PS/2 extended Delete");
    extended(0xD3); drain();
    extended(0x5B); extended(0x5C); extended(0xDB); drain();
    CHECK(input_get_modifiers() & INPUT_MOD_SUPER, "right Super survives left release");
    extended(0xDC); drain();
    CHECK(!(input_get_modifiers() & INPUT_MOD_SUPER), "Super clears on final release");
    extended(0x1C);
    CHECK(input_poll_event(&event) && event.key.ascii == '\n', "keypad Enter works");
    extended(0x9C); drain();
    const uint8_t pause[] = {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5};
    for (unsigned i = 0; i < ARRAY_SIZE(pause); i++) ps2_keyboard_handle_scancode(pause[i]);
    CHECK(!input_has_event() && input_get_modifiers() == 0, "Pause sequence cannot leave phantom modifiers");

    input_init();
    input_set_mouse_position(100, 100);
    for (int i = 0; i < 1000; i++) input_post_mouse_move(1, 0);
    CHECK(input_poll_event(&event) && event.motion.dx == 1000 && !input_has_event(),
          "adjacent mouse movement coalesces without flooding");
    InputEvent raw = {0};
    raw.type = INPUT_EVENT_MOUSE_SCROLL;
    for (int i = 0; i < 1000; i++) input_post_event(&raw);
    key(42, 1); key(42, 0);
    bool release_seen = false;
    while (input_poll_event(&event)) if (event.type == INPUT_EVENT_KEY_RELEASE) release_seen = true;
    CHECK(release_seen && !input_is_key_down(KEY_LSHIFT), "release survives motion overload");
    CHECK(input_get_dropped_count() > 0, "overload counted without serial log flood");
    input_init();
    raw.type = INPUT_EVENT_KEY_PRESS;
    for (int i = 0; i < 1000; i++) input_post_event(&raw);
    key(42, 1); key(42, 0);
    release_seen = false;
    int count = 0;
    while (input_poll_event(&event)) {
        count++;
        if (event.type == INPUT_EVENT_KEY_RELEASE) release_seen = true;
    }
    CHECK(count == INPUT_QUEUE_SIZE - 1 && release_seen, "release survives a completely full ring");
    CHECK(!input_has_event(), "ring drains after wrapping and overflow");
    input_init(); input_set_mouse_bounds(1024, 768);
    virtio_input_test_pointer_init(0, 32767, 0, 32767);
    virtio_input_test_pointer_event(3, 0, 3203);
    CHECK(!input_has_event(), "first X waits for coherent absolute report");
    virtio_input_test_pointer_event(3, 1, 4272);
    CHECK(!input_has_event(), "XY published at SYN_REPORT not half a move");
    virtio_input_test_pointer_event(0, 0, 0);
    CHECK(input_poll_event(&event) && event.motion.absolute && event.motion.x == 99 && event.motion.y == 99,
          "first tablet report is an absolute position, not a discarded delta");
    virtio_input_test_pointer_event(3, 0, 32767); virtio_input_test_pointer_event(3, 1, 32767);
    virtio_input_test_pointer_event(1, 0x110, 1);
    CHECK(input_poll_event(&event) && event.motion.absolute && event.motion.x == 1023 && event.motion.y == 767,
          "pending position flushed before button");
    CHECK(input_poll_event(&event) && event.type == INPUT_EVENT_MOUSE_BUTTON_DOWN &&
          event.button.absolute && event.button.x == 1023 && event.button.y == 767, "click keeps tablet coordinates");
    virtio_input_test_pointer_event(0, 0, 0);
    CHECK(!input_has_event(), "SYN after click does not duplicate motion");
    virtio_input_test_pointer_init(-100, 100, 10, 110);
    virtio_input_test_pointer_event(3, 0, -2147483647); virtio_input_test_pointer_event(3, 1, 2147483647);
    virtio_input_test_pointer_event(0, 0, 0);
    CHECK(input_poll_event(&event) && event.motion.x == 0 && event.motion.y == 767,
          "device min/max and out-of-range coordinates clamped before scaling");
    virtio_input_test_pointer_event(3, 0, 0); virtio_input_test_pointer_event(3, 1, 60);
    virtio_input_test_pointer_event(0, 0, 0);
    CHECK(input_poll_event(&event) && event.motion.x == 511 && event.motion.y == 383, "nonzero minima normalized");
    for (int i = 0; i < 1000; i++) input_post_mouse_absolute(i % 1024, i % 768);
    CHECK(input_poll_event(&event) && event.motion.absolute && event.motion.x == 999 && event.motion.y == 231 &&
          !input_has_event(), "absolute coalescing keeps latest endpoint, never sums positions");
    input_post_mouse_absolute(10, 20); input_post_mouse_move(1, 2);
    CHECK(input_poll_event(&event) && event.motion.absolute, "absolute and relative events don't coalesce");
    CHECK(input_poll_event(&event) && !event.motion.absolute && event.motion.dx == 1, "PS/2 deltas remain relative");
    input_post_mouse_move(2147483647, -2147483647);
    CHECK(input_poll_event(&event) && event.motion.x == 1023 && event.motion.y == 0, "extreme relative movement cannot overflow");
    return finish();
}
