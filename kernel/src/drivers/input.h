/*
 * ojjyOS v3 Kernel - Input Event Subsystem
 *
 * Unified input event queue for all input devices (keyboard, mouse, touch, etc.)
 * This queue feeds the compositor for UI event handling.
 *
 * Architecture:
 *   - Ring buffer for lock-free single-producer/single-consumer
 *   - Events have timestamps for ordering
 *   - Mouse position tracked internally with bounds clamping
 *   - Modifier key state tracked globally
 */

#ifndef _OJJY_INPUT_H
#define _OJJY_INPUT_H

#include "../types.h"

/*
 * Input event types
 */
typedef enum {
    INPUT_EVENT_NONE = 0,

    /* Keyboard events */
    INPUT_EVENT_KEY_PRESS,          /* Key pressed */
    INPUT_EVENT_KEY_RELEASE,        /* Key released */
    INPUT_EVENT_KEY_REPEAT,         /* Key auto-repeat (future) */

    /* Mouse events */
    INPUT_EVENT_MOUSE_MOVE,         /* Mouse moved */
    INPUT_EVENT_MOUSE_BUTTON_DOWN,  /* Button pressed */
    INPUT_EVENT_MOUSE_BUTTON_UP,    /* Button released */
    INPUT_EVENT_MOUSE_SCROLL,       /* Scroll wheel */
    INPUT_EVENT_MOUSE_ENTER,        /* Mouse entered window (future) */
    INPUT_EVENT_MOUSE_LEAVE,        /* Mouse left window (future) */

} InputEventType;

/*
 * Mouse button identifiers
 */
typedef enum {
    MOUSE_BUTTON_LEFT   = 0,
    MOUSE_BUTTON_RIGHT  = 1,
    MOUSE_BUTTON_MIDDLE = 2,
    MOUSE_BUTTON_4      = 3,        /* Forward */
    MOUSE_BUTTON_5      = 4,        /* Back */
} MouseButton;

/*
 * Keyboard modifier flags
 */
#define INPUT_MOD_SHIFT     (1 << 0)
#define INPUT_MOD_CTRL      (1 << 1)
#define INPUT_MOD_ALT       (1 << 2)
#define INPUT_MOD_SUPER     (1 << 3)    /* Windows/Command key */
#define INPUT_MOD_CAPSLOCK  (1 << 4)
#define INPUT_MOD_NUMLOCK   (1 << 5)

/*
 * Key codes (subset - expandable)
 */
typedef enum {
    KEY_NONE = 0,
    KEY_ESCAPE,
    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
    KEY_MINUS, KEY_EQUALS, KEY_BACKSPACE, KEY_TAB,
    KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
    KEY_LBRACKET, KEY_RBRACKET, KEY_ENTER, KEY_LCTRL,
    KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
    KEY_SEMICOLON, KEY_QUOTE, KEY_BACKTICK, KEY_LSHIFT, KEY_BACKSLASH,
    KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M,
    KEY_COMMA, KEY_PERIOD, KEY_SLASH, KEY_RSHIFT, KEY_KPMULTIPLY,
    KEY_LALT, KEY_SPACE, KEY_CAPSLOCK,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_NUMLOCK, KEY_SCROLLLOCK,
    KEY_HOME, KEY_UP, KEY_PAGEUP, KEY_KPMINUS,
    KEY_LEFT, KEY_KP5, KEY_RIGHT, KEY_KPPLUS,
    KEY_END, KEY_DOWN, KEY_PAGEDOWN,
    KEY_INSERT, KEY_DELETE,
    KEY_RCTRL, KEY_RALT, KEY_LSUPER, KEY_RSUPER, KEY_MENU,
    KEY_MAX
} KeyCode;

/*
 * Input event structure
 */
typedef struct {
    InputEventType  type;           /* Event type */
    uint64_t        timestamp;      /* Tick count when event occurred */

    union {
        /* Keyboard event data */
        struct {
            uint8_t     scancode;   /* Raw scancode from hardware */
            KeyCode     keycode;    /* Translated key code */
            char        ascii;      /* ASCII character (0 if none) */
            uint8_t     modifiers;  /* Active modifier flags */
        } key;

        /* Mouse motion event data */
        struct {
            int32_t     dx;         /* Relative X delta */
            int32_t     dy;         /* Relative Y delta */
            int32_t     x;          /* Absolute X position */
            int32_t     y;          /* Absolute Y position */
        } motion;

        /* Mouse button event data */
        struct {
            MouseButton button;     /* Which button */
            int32_t     x;          /* X position at event */
            int32_t     y;          /* Y position at event */
            uint8_t     modifiers;  /* Active modifier flags */
        } button;

        /* Mouse scroll event data */
        struct {
            int32_t     dx;         /* Horizontal scroll (+ = right) */
            int32_t     dy;         /* Vertical scroll (+ = up) */
            int32_t     x;          /* X position */
            int32_t     y;          /* Y position */
        } scroll;
    };
} InputEvent;

/*
 * Event queue configuration
 */
#define INPUT_QUEUE_SIZE    256     /* Must be power of 2 */

/*
 * Initialize input subsystem
 */
void input_init(void);

/*
 * Event queue operations (called by input drivers)
 */
void input_post_event(const InputEvent *event);
void input_post_key_event(InputEventType type, uint8_t scancode,
                          KeyCode keycode, char ascii);
void input_post_mouse_move(int32_t dx, int32_t dy);
void input_post_mouse_button(InputEventType type, MouseButton button);
void input_post_mouse_scroll(int32_t dx, int32_t dy);

/*
 * Event queue operations (called by event consumers)
 */
bool input_has_event(void);
bool input_poll_event(InputEvent *event);   /* Non-blocking */
bool input_wait_event(InputEvent *event);   /* Blocking */
bool input_peek_event(InputEvent *event);   /* Look without removing */

/*
 * Mouse state
 */
void input_get_mouse_position(int32_t *x, int32_t *y);
void input_set_mouse_position(int32_t x, int32_t y);
void input_set_mouse_bounds(int32_t width, int32_t height);
bool input_is_mouse_button_down(MouseButton button);
uint8_t input_get_mouse_buttons(void);      /* Bitmask of pressed buttons */

/*
 * Keyboard state
 */
uint8_t input_get_modifiers(void);
void input_set_modifiers(uint8_t mods);
bool input_is_key_down(KeyCode keycode);

/*
 * Statistics
 */
uint64_t input_get_event_count(void);
uint64_t input_get_dropped_count(void);

#endif /* _OJJY_INPUT_H */
