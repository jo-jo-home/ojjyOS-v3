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

/* Current keyboard state */
static uint8_t current_modifiers = 0;
static uint8_t key_states[KEY_MAX / 8 + 1];  /* Bitmap for key states */

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
    current_modifiers = 0;

    memset(key_states, 0, sizeof(key_states));
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

    /* Check for queue overflow */
    uint32_t next_head = (queue_head + 1) & INPUT_QUEUE_MASK;
    if (next_head == queue_tail) {
        /* Queue full - drop event */
        dropped_events++;
        serial_printf("[INPUT] WARNING: Queue overflow, dropped event\n");
        return;
    }

    /* Copy event to queue and add timestamp */
    event_queue[queue_head] = *event;
    event_queue[queue_head].timestamp = timer_get_ticks();
    queue_head = next_head;
    total_events++;
}

/*
 * Post a keyboard event (helper for keyboard drivers)
 */
void input_post_key_event(InputEventType type, uint8_t scancode,
                          KeyCode keycode, char ascii)
{
    /* Update key state tracking */
    if (keycode < KEY_MAX) {
        if (type == INPUT_EVENT_KEY_PRESS) {
            key_states[keycode / 8] |= (1 << (keycode % 8));
        } else if (type == INPUT_EVENT_KEY_RELEASE) {
            key_states[keycode / 8] &= ~(1 << (keycode % 8));
        }
    }

    InputEvent event = {0};
    event.type = type;
    event.key.scancode = scancode;
    event.key.keycode = keycode;
    event.key.ascii = ascii;
    event.key.modifiers = current_modifiers;

    input_post_event(&event);
}

/*
 * Post a mouse movement event (helper for mouse drivers)
 */
void input_post_mouse_move(int32_t dx, int32_t dy)
{
    /* Update absolute position with clamping */
    mouse_x += dx;
    mouse_y += dy;

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
    if (!input_has_event()) {
        return false;
    }

    if (event) {
        *event = event_queue[queue_tail];
    }
    queue_tail = (queue_tail + 1) & INPUT_QUEUE_MASK;
    return true;
}

/*
 * Wait for an event (blocking)
 * Returns true when event is available
 */
bool input_wait_event(InputEvent *event)
{
    while (!input_has_event()) {
        __asm__ volatile("hlt");
    }

    return input_poll_event(event);
}

/*
 * Peek at next event without removing
 */
bool input_peek_event(InputEvent *event)
{
    if (!input_has_event()) {
        return false;
    }

    if (event) {
        *event = event_queue[queue_tail];
    }
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
    mouse_max_x = width;
    mouse_max_y = height;

    /* Clamp current position to new bounds */
    if (mouse_x >= mouse_max_x) mouse_x = mouse_max_x - 1;
    if (mouse_y >= mouse_max_y) mouse_y = mouse_max_y - 1;

    serial_printf("[INPUT] Mouse bounds set to %dx%d\n", width, height);
}

/*
 * Check if a mouse button is currently pressed
 */
bool input_is_mouse_button_down(MouseButton button)
{
    return (mouse_buttons & (1 << button)) != 0;
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
