/*
 * ojjyOS v3 Kernel - Input Subsystem Implementation
 *
 * Ring buffer for input events from all input devices.
 * Single-producer/single-consumer safe for IRQ handler use.
 */

#include "input.h"
#include "../serial.h"
#include "../timer.h"
#include "../string.h"

/* Queue mask for power-of-2 size */
#define INPUT_QUEUE_MASK (INPUT_QUEUE_SIZE - 1)

/* Event ring buffer */
static InputEvent event_queue[INPUT_QUEUE_SIZE];
static volatile uint32_t queue_head = 0;  /* Write position (producer) */
static volatile uint32_t queue_tail = 0;  /* Read position (consumer) */

/* Current mouse state */
static int32_t mouse_x = 0;
static int32_t mouse_y = 0;
static int32_t mouse_max_x = 1920;
static int32_t mouse_max_y = 1080;
static uint8_t mouse_buttons = 0;
static bool mouse_absolute;

/* Current keyboard state */
static uint8_t current_modifiers = 0;
static uint8_t key_states[KEY_MAX / 8 + 1];  /* Bitmap for key states */
static uint64_t key_pressed_at[KEY_MAX];
static uint64_t key_repeated_at[KEY_MAX];

/* The current kernel is single-CPU. Preserve IRQ state when main-loop
 * consumption shares the queue with PS/2 IRQ producers. */
static uint64_t input_lock(void)
{
#ifdef OJJY_HOST_TEST
    return 0;
#elif defined(__x86_64__)
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
#else
    uint64_t flags;
    __asm__ volatile("mrs %0, daif; msr daifset, #2" : "=r"(flags) : : "memory");
    return flags;
#endif
}

static void input_unlock(uint64_t flags)
{
#ifdef OJJY_HOST_TEST
    (void)flags;
#elif defined(__x86_64__)
    if (flags & (1ULL << 9)) __asm__ volatile("sti" ::: "memory");
#else
    __asm__ volatile("msr daif, %0" : : "r"(flags) : "memory");
#endif
}

static void update_key_modifiers(void)
{
    uint8_t mods = current_modifiers & (INPUT_MOD_CAPSLOCK | INPUT_MOD_NUMLOCK);
    if (input_is_key_down(KEY_LSHIFT) || input_is_key_down(KEY_RSHIFT)) mods |= INPUT_MOD_SHIFT;
    if (input_is_key_down(KEY_LCTRL) || input_is_key_down(KEY_RCTRL)) mods |= INPUT_MOD_CTRL;
    if (input_is_key_down(KEY_LALT) || input_is_key_down(KEY_RALT)) mods |= INPUT_MOD_ALT;
    if (input_is_key_down(KEY_LSUPER) || input_is_key_down(KEY_RSUPER)) mods |= INPUT_MOD_SUPER;
    current_modifiers = mods;
}

static bool repeatable_key(KeyCode keycode, char ascii)
{
    if (current_modifiers & (INPUT_MOD_CTRL | INPUT_MOD_ALT | INPUT_MOD_SUPER)) return false;
    return (ascii >= 32 && ascii <= 126) || keycode == KEY_BACKSPACE ||
        keycode == KEY_DELETE || keycode == KEY_LEFT || keycode == KEY_RIGHT ||
        keycode == KEY_UP || keycode == KEY_DOWN || keycode == KEY_HOME ||
        keycode == KEY_END || keycode == KEY_PAGEUP || keycode == KEY_PAGEDOWN;
}

/* Statistics */
static uint64_t total_events = 0;
static uint64_t dropped_events = 0;

/*
 * Initialize input subsystem
 */
void input_init(void)
{
    serial_printf("[INPUT] Initializing input subsystem...\n");

    queue_head = 0;
    queue_tail = 0;

    mouse_x = mouse_max_x / 2;
    mouse_y = mouse_max_y / 2;
    mouse_buttons = 0;
    mouse_absolute = false;
    current_modifiers = 0;

    memset(key_states, 0, sizeof(key_states));
    memset(key_pressed_at, 0, sizeof(key_pressed_at));
    memset(key_repeated_at, 0, sizeof(key_repeated_at));
    memset(event_queue, 0, sizeof(event_queue));

    total_events = 0;
    dropped_events = 0;

    serial_printf("[INPUT] Input subsystem ready (queue size=%d)\n", INPUT_QUEUE_SIZE);
}

/*
 * Post a raw event to the queue (called by drivers)
 */
void input_post_event(const InputEvent *event)
{
    if (!event) return;
    uint64_t flags = input_lock();

    /* Coalesce adjacent pointer moves, preserving their position in the
     * stream relative to clicks and keys. Never print from the overflow path. */
    if (event->type == INPUT_EVENT_MOUSE_MOVE && queue_head != queue_tail) {
        InputEvent *last = &event_queue[(queue_head - 1) & INPUT_QUEUE_MASK];
        if (last->type == INPUT_EVENT_MOUSE_MOVE && last->motion.absolute == event->motion.absolute) {
            if (event->motion.absolute) {
                *last = *event;
                last->timestamp = timer_get_ticks();
                total_events++;
                input_unlock(flags);
                return;
            }
            int64_t dx = (int64_t)last->motion.dx + event->motion.dx;
            int64_t dy = (int64_t)last->motion.dy + event->motion.dy;
            if (dx >= -2147483648LL && dx <= 2147483647LL &&
                dy >= -2147483648LL && dy <= 2147483647LL) {
                last->motion = event->motion;
                last->motion.dx = (int32_t)dx;
                last->motion.dy = (int32_t)dy;
                last->timestamp = timer_get_ticks();
                total_events++;
                input_unlock(flags);
                return;
            }
        }
    }

    /* Keep room for key/button transitions when motion or repeats flood in. */
    uint32_t free_slots = (queue_tail - queue_head - 1) & INPUT_QUEUE_MASK;
    if (free_slots <= 16 && (event->type == INPUT_EVENT_KEY_REPEAT ||
        event->type == INPUT_EVENT_MOUSE_MOVE || event->type == INPUT_EVENT_MOUSE_SCROLL)) {
        dropped_events++;
        input_unlock(flags);
        return;
    }

    /* Check for queue overflow */
    uint32_t next_head = (queue_head + 1) & INPUT_QUEUE_MASK;
    if (next_head == queue_tail) {
        /* Prefer losing an old non-release over a new key/button release.
         * Shift the bounded ring while IRQs are masked, keeping FIFO order. */
        dropped_events++;
        uint32_t victim = queue_tail;
        for (uint32_t pos = queue_tail; pos != queue_head; pos = (pos + 1) & INPUT_QUEUE_MASK) {
            InputEventType type = event_queue[pos].type;
            if (type != INPUT_EVENT_KEY_RELEASE && type != INPUT_EVENT_MOUSE_BUTTON_UP) {
                victim = pos;
                break;
            }
        }
        for (uint32_t pos = victim; pos != queue_tail; pos = (pos - 1) & INPUT_QUEUE_MASK) {
            event_queue[pos] = event_queue[(pos - 1) & INPUT_QUEUE_MASK];
        }
        queue_tail = (queue_tail + 1) & INPUT_QUEUE_MASK;
    }

    /* Copy event to queue and add timestamp */
    event_queue[queue_head] = *event;
    event_queue[queue_head].timestamp = timer_get_ticks();
    queue_head = next_head;
    total_events++;
    input_unlock(flags);
}

/*
 * Post a keyboard event (helper for keyboard drivers)
 */
void input_post_key_event(InputEventType type, uint8_t scancode,
                          KeyCode keycode, char ascii)
{
    if (keycode <= KEY_NONE || keycode >= KEY_MAX) return;
    uint64_t flags = input_lock();
    uint64_t now = timer_get_ticks();
    bool was_down = input_is_key_down(keycode);
    if (type == INPUT_EVENT_KEY_PRESS && was_down) type = INPUT_EVENT_KEY_REPEAT;
    if (type == INPUT_EVENT_KEY_REPEAT) {
        if (!was_down || !repeatable_key(keycode, ascii) ||
            now - key_pressed_at[keycode] < 450 || now - key_repeated_at[keycode] < 35) {
            input_unlock(flags);
            return;
        }
        key_repeated_at[keycode] = now;
    } else if (type == INPUT_EVENT_KEY_PRESS) {
        key_pressed_at[keycode] = key_repeated_at[keycode] = now;
        if (keycode == KEY_CAPSLOCK) current_modifiers ^= INPUT_MOD_CAPSLOCK;
        if (keycode == KEY_NUMLOCK) current_modifiers ^= INPUT_MOD_NUMLOCK;
    } else if (type != INPUT_EVENT_KEY_RELEASE) {
        input_unlock(flags);
        return;
    }
    /* Update key state tracking */
    if (keycode < KEY_MAX) {
        if (type == INPUT_EVENT_KEY_PRESS) {
            key_states[keycode / 8] |= (1 << (keycode % 8));
        } else if (type == INPUT_EVENT_KEY_RELEASE) {
            key_states[keycode / 8] &= ~(1 << (keycode % 8));
        }
    }
    update_key_modifiers();

    InputEvent event = {0};
    event.type = type;
    event.key.scancode = scancode;
    event.key.keycode = keycode;
    event.key.ascii = ascii;
    event.key.modifiers = current_modifiers;

    input_post_event(&event);
    input_unlock(flags);
}

/*
 * Post a mouse movement event (helper for mouse drivers)
 */
void input_post_mouse_move(int32_t dx, int32_t dy)
{
    mouse_absolute = false;
    /* Update absolute position with clamping */
    int64_t x = (int64_t)mouse_x + dx, y = (int64_t)mouse_y + dy;
    mouse_x = (int32_t)MAX(0, MIN(x, mouse_max_x - 1));
    mouse_y = (int32_t)MAX(0, MIN(y, mouse_max_y - 1));

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= mouse_max_x) mouse_x = mouse_max_x - 1;
    if (mouse_y >= mouse_max_y) mouse_y = mouse_max_y - 1;

    InputEvent event = {0};
    event.type = INPUT_EVENT_MOUSE_MOVE;
    event.motion.dx = dx;
    event.motion.dy = dy;
    event.motion.x = mouse_x;
    event.motion.y = mouse_y;

    input_post_event(&event);
}

/* Absolute devices have already been mapped to framebuffer pixels. Keep the
 * first position and every later position, not a history-dependent delta. */
void input_post_mouse_absolute(int32_t x, int32_t y)
{
    mouse_absolute = true;
    input_set_mouse_position(x, y);
    InputEvent event = {0};
    event.type = INPUT_EVENT_MOUSE_MOVE;
    event.motion.absolute = true;
    event.motion.x = mouse_x;
    event.motion.y = mouse_y;
    input_post_event(&event);
}

/*
 * Post a mouse button event (helper for mouse drivers)
 */
void input_post_mouse_button(InputEventType type, MouseButton button)
{
    /* Update button state */
    if (type == INPUT_EVENT_MOUSE_BUTTON_DOWN) {
        mouse_buttons |= (1 << button);
    } else if (type == INPUT_EVENT_MOUSE_BUTTON_UP) {
        mouse_buttons &= ~(1 << button);
    }

    InputEvent event = {0};
    event.type = type;
    event.button.button = button;
    event.button.x = mouse_x;
    event.button.y = mouse_y;
    event.button.modifiers = current_modifiers;
    event.button.absolute = mouse_absolute;

    input_post_event(&event);
}

/*
 * Post a mouse scroll event (helper for mouse drivers)
 */
void input_post_mouse_scroll(int32_t dx, int32_t dy)
{
    InputEvent event = {0};
    event.type = INPUT_EVENT_MOUSE_SCROLL;
    event.scroll.dx = dx;
    event.scroll.dy = dy;
    event.scroll.x = mouse_x;
    event.scroll.y = mouse_y;

    input_post_event(&event);
}

/*
 * Check if events are available
 */
bool input_has_event(void)
{
    return queue_head != queue_tail;
}

/*
 * Poll for an event (non-blocking)
 * Returns true if event was available, false if queue empty
 */
bool input_poll_event(InputEvent *event)
{
    uint64_t flags = input_lock();
    if (!input_has_event()) {
        input_unlock(flags);
        return false;
    }

    if (event) {
        *event = event_queue[queue_tail];
    }
    queue_tail = (queue_tail + 1) & INPUT_QUEUE_MASK;
    input_unlock(flags);
    return true;
}

/*
 * Wait for an event (blocking)
 * Returns true when event is available
 */
bool input_wait_event(InputEvent *event)
{
    while (!input_has_event()) {
        hlt();
    }

    return input_poll_event(event);
}

/*
 * Peek at next event without removing
 */
bool input_peek_event(InputEvent *event)
{
    uint64_t flags = input_lock();
    if (!input_has_event()) {
        input_unlock(flags);
        return false;
    }

    if (event) {
        *event = event_queue[queue_tail];
    }
    input_unlock(flags);
    return true;
}

/*
 * Get current mouse position
 */
void input_get_mouse_position(int32_t *x, int32_t *y)
{
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
}

/*
 * Set mouse position (e.g., for warping)
 */
void input_set_mouse_position(int32_t x, int32_t y)
{
    mouse_x = x;
    mouse_y = y;

    /* Clamp to bounds */
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= mouse_max_x) mouse_x = mouse_max_x - 1;
    if (mouse_y >= mouse_max_y) mouse_y = mouse_max_y - 1;
}

/*
 * Set mouse bounds (called when display resolution changes)
 */
void input_set_mouse_bounds(int32_t width, int32_t height)
{
    /* Validate: non-positive bounds would produce a -1 cursor position */
    if (width > 0) mouse_max_x = width;
    if (height > 0) mouse_max_y = height;

    /* Clamp current position to new bounds */
    if (mouse_x >= mouse_max_x) mouse_x = mouse_max_x - 1;
    if (mouse_y >= mouse_max_y) mouse_y = mouse_max_y - 1;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;

    serial_printf("[INPUT] Mouse bounds set to %dx%d\n", mouse_max_x, mouse_max_y);
}

/*
 * Check if a mouse button is currently pressed
 */
bool input_is_mouse_button_down(MouseButton button)
{
    return (mouse_buttons & (1 << button)) != 0;
}

/*
 * Get the current mouse bounds (screen size), used by absolute
 * pointer devices (e.g. virtio tablets) to map to pixels.
 */
void input_get_bounds(int32_t *width, int32_t *height)
{
    if (width) *width = mouse_max_x;
    if (height) *height = mouse_max_y;
}

/*
 * Get bitmask of all pressed mouse buttons
 */
uint8_t input_get_mouse_buttons(void)
{
    return mouse_buttons;
}

/*
 * Get current modifier key state
 */
uint8_t input_get_modifiers(void)
{
    return current_modifiers;
}

/*
 * Set modifier key state (called by keyboard driver)
 */
void input_set_modifiers(uint8_t mods)
{
    current_modifiers = mods;
}

/*
 * Check if a key is currently pressed
 */
bool input_is_key_down(KeyCode keycode)
{
    if (keycode >= KEY_MAX) return false;
    return (key_states[keycode / 8] & (1 << (keycode % 8))) != 0;
}

/*
 * Get total events processed
 */
uint64_t input_get_event_count(void)
{
    return total_events;
}

/*
 * Get dropped event count
 */
uint64_t input_get_dropped_count(void)
{
    return dropped_events;
}
