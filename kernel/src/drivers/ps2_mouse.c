/*
 * ojjyOS v3 Kernel - PS/2 Mouse Driver
 *
 * Handles PS/2 mouse input with scroll wheel support.
 * VirtualBox emulates a standard PS/2 mouse on IRQ 12.
 */

#include "ps2_mouse.h"
#include "driver.h"
#include "input.h"
#include "../serial.h"
#include "../idt.h"
#include "../types.h"

/* PS/2 controller ports */
#define PS2_DATA_PORT   0x60
#define PS2_STATUS_PORT 0x64
#define PS2_CMD_PORT    0x64

/* PS/2 controller commands */
#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_PORT2   0xA7
#define PS2_CMD_ENABLE_PORT2    0xA8
#define PS2_CMD_TEST_PORT2      0xA9
#define PS2_CMD_WRITE_PORT2     0xD4

/* Mouse commands */
#define MOUSE_CMD_SET_DEFAULTS  0xF6
#define MOUSE_CMD_ENABLE        0xF4
#define MOUSE_CMD_DISABLE       0xF5
#define MOUSE_CMD_RESET         0xFF
#define MOUSE_CMD_SET_SAMPLE    0xF3
#define MOUSE_CMD_GET_ID        0xF2

/* Status bits */
#define PS2_STATUS_OUTPUT_FULL  0x01
#define PS2_STATUS_INPUT_FULL   0x02
#define PS2_STATUS_MOUSE_DATA   0x20

/* Mouse packet state machine */
typedef enum {
    MOUSE_STATE_BYTE1,
    MOUSE_STATE_BYTE2,
    MOUSE_STATE_BYTE3,
    MOUSE_STATE_BYTE4,  /* For scroll wheel mice */
} MouseState;

/* Driver state */
static struct {
    MouseState  state;
    uint8_t     packet[4];
    bool        has_scroll;     /* Has scroll wheel (ID 3 or 4) */
    uint8_t     mouse_id;       /* Mouse ID */
    uint8_t     prev_buttons;   /* Previous button state */
} mouse_state;

/* Forward declarations */
static bool ps2_mouse_probe(Driver *drv);
static int ps2_mouse_init_driver(Driver *drv);
static int ps2_mouse_shutdown(Driver *drv);
static bool ps2_mouse_handle_irq(Driver *drv, uint8_t irq);

/* Driver operations */
static DriverOps ps2_mouse_ops = {
    .probe = ps2_mouse_probe,
    .init = ps2_mouse_init_driver,
    .shutdown = ps2_mouse_shutdown,
    .handle_irq = ps2_mouse_handle_irq,
};

/* Driver instance */
static Driver ps2_mouse_driver = {
    .name = "ps2_mouse",
    .description = "PS/2 Mouse Driver with Scroll Wheel",
    .version = DRIVER_VERSION(1, 0, 0),
    .type = DRIVER_TYPE_INPUT,
    .ops = &ps2_mouse_ops,
};

/* External IRQ enable function */
extern void pic_enable_irq(uint8_t irq);
extern void pic_disable_irq(uint8_t irq);

/*
 * Wait for PS/2 controller to be ready for input
 */
static void ps2_wait_input(void)
{
    int timeout = 100000;
    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) && timeout > 0) {
        timeout--;
    }
}

/*
 * Wait for PS/2 controller to have output data
 */
static bool ps2_wait_output(void)
{
    int timeout = 100000;
    while (!(inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) && timeout > 0) {
        timeout--;
    }
    return timeout > 0;
}

/*
 * Send command to PS/2 controller
 */
static void ps2_send_cmd(uint8_t cmd)
{
    ps2_wait_input();
    outb(PS2_CMD_PORT, cmd);
}

/*
 * Send command to mouse (via controller)
 */
static void mouse_send_cmd(uint8_t cmd)
{
    ps2_wait_input();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_PORT2);
    ps2_wait_input();
    outb(PS2_DATA_PORT, cmd);
}

/*
 * Read response from mouse
 */
static uint8_t mouse_read_response(void)
{
    if (ps2_wait_output()) {
        return inb(PS2_DATA_PORT);
    }
    return 0xFF;
}

/*
 * Try to enable scroll wheel (sets mouse to ID 3)
 */
static bool mouse_try_enable_scroll(void)
{
    /* Magic sequence to enable scroll wheel:
     * Set sample rate to 200, then 100, then 80
     */
    mouse_send_cmd(MOUSE_CMD_SET_SAMPLE);
    mouse_read_response();
    mouse_send_cmd(200);
    mouse_read_response();

    mouse_send_cmd(MOUSE_CMD_SET_SAMPLE);
    mouse_read_response();
    mouse_send_cmd(100);
    mouse_read_response();

    mouse_send_cmd(MOUSE_CMD_SET_SAMPLE);
    mouse_read_response();
    mouse_send_cmd(80);
    mouse_read_response();

    /* Get mouse ID */
    mouse_send_cmd(MOUSE_CMD_GET_ID);
    mouse_read_response();  /* ACK */
    uint8_t id = mouse_read_response();

    serial_printf("[PS2_MOUSE] Mouse ID: %d\n", id);
    mouse_state.mouse_id = id;
    return (id == 3 || id == 4);
}

/*
 * Probe for PS/2 mouse
 */
static bool ps2_mouse_probe(Driver *drv)
{
    (void)drv;

    serial_printf("[PS2_MOUSE] Probing for PS/2 mouse...\n");

    /* Enable auxiliary port */
    ps2_send_cmd(PS2_CMD_ENABLE_PORT2);

    /* Test auxiliary port */
    ps2_send_cmd(PS2_CMD_TEST_PORT2);
    if (ps2_wait_output()) {
        uint8_t result = inb(PS2_DATA_PORT);
        if (result != 0x00) {
            serial_printf("[PS2_MOUSE] Port 2 test failed: 0x%x\n", result);
            return false;
        }
    }

    /* Reset mouse */
    mouse_send_cmd(MOUSE_CMD_RESET);
    mouse_read_response();  /* ACK */
    mouse_read_response();  /* Self-test pass (0xAA) */
    mouse_read_response();  /* Mouse ID */

    serial_printf("[PS2_MOUSE] Mouse found\n");
    return true;
}

/*
 * Initialize PS/2 mouse driver
 */
static int ps2_mouse_init_driver(Driver *drv)
{
    serial_printf("[PS2_MOUSE] Initializing...\n");

    /* Initialize state */
    mouse_state.state = MOUSE_STATE_BYTE1;
    mouse_state.has_scroll = false;
    mouse_state.mouse_id = 0;
    mouse_state.prev_buttons = 0;

    /* Try to enable scroll wheel */
    mouse_state.has_scroll = mouse_try_enable_scroll();
    if (mouse_state.has_scroll) {
        serial_printf("[PS2_MOUSE] Scroll wheel enabled\n");
    }

    /* Set defaults */
    mouse_send_cmd(MOUSE_CMD_SET_DEFAULTS);
    mouse_read_response();

    /* Enable mouse data reporting */
    mouse_send_cmd(MOUSE_CMD_ENABLE);
    mouse_read_response();

    /* Enable IRQ 12 in controller config */
    ps2_send_cmd(PS2_CMD_READ_CONFIG);
    if (ps2_wait_output()) {
        uint8_t config = inb(PS2_DATA_PORT);
        config |= 0x02;  /* Enable IRQ12 */
        config &= ~0x20; /* Enable clock for port 2 */
        ps2_send_cmd(PS2_CMD_WRITE_CONFIG);
        ps2_wait_input();
        outb(PS2_DATA_PORT, config);
    }

    /* Register IRQ handler */
    driver_register_irq(drv, 12);  /* Mouse is IRQ 12 */

    /* Enable IRQ 12 in PIC */
    pic_enable_irq(12);

    serial_printf("[PS2_MOUSE] Initialized (scroll=%d)\n", mouse_state.has_scroll);
    return 0;
}

/*
 * Shutdown mouse driver
 */
static int ps2_mouse_shutdown(Driver *drv)
{
    (void)drv;
    serial_printf("[PS2_MOUSE] Shutting down...\n");

    pic_disable_irq(12);

    /* Disable mouse data reporting */
    mouse_send_cmd(MOUSE_CMD_DISABLE);
    mouse_read_response();

    return 0;
}

/*
 * Process a complete mouse packet
 */
static void mouse_process_packet(void)
{
    uint8_t status = mouse_state.packet[0];
    int8_t dx = mouse_state.packet[1];
    int8_t dy = mouse_state.packet[2];
    int8_t dz = 0;

    if (mouse_state.has_scroll) {
        dz = (int8_t)mouse_state.packet[3];
    }

    /* Check for overflow (discard packet) */
    if (status & 0xC0) {
        return;
    }

    /* Apply sign extension based on status bits */
    if (status & 0x10) dx |= 0xFFFFFF00;
    if (status & 0x20) dy |= 0xFFFFFF00;

    /* PS/2 mouse Y is inverted */
    dy = -dy;

    /* Generate movement event */
    if (dx != 0 || dy != 0) {
        input_post_mouse_move(dx, dy);
    }

    /* Generate scroll event */
    if (dz != 0) {
        input_post_mouse_scroll(0, -dz);  /* Positive = scroll up */
    }

    /* Check button changes */
    uint8_t buttons = status & 0x07;

    for (int i = 0; i < 3; i++) {
        uint8_t mask = (1 << i);
        bool was_pressed = (mouse_state.prev_buttons & mask) != 0;
        bool is_pressed = (buttons & mask) != 0;

        if (is_pressed && !was_pressed) {
            /* Button pressed */
            input_post_mouse_button(INPUT_EVENT_MOUSE_BUTTON_DOWN, (MouseButton)i);
            serial_printf("[PS2_MOUSE] Button %d down\n", i);
        } else if (!is_pressed && was_pressed) {
            /* Button released */
            input_post_mouse_button(INPUT_EVENT_MOUSE_BUTTON_UP, (MouseButton)i);
            serial_printf("[PS2_MOUSE] Button %d up\n", i);
        }
    }

    mouse_state.prev_buttons = buttons;
}

/*
 * Handle mouse IRQ
 */
static bool ps2_mouse_handle_irq(Driver *drv, uint8_t irq)
{
    (void)drv;
    (void)irq;

    /* Check if data is from mouse */
    uint8_t status = inb(PS2_STATUS_PORT);
    if (!(status & PS2_STATUS_MOUSE_DATA)) {
        return false;
    }

    /* Read data byte */
    uint8_t data = inb(PS2_DATA_PORT);

    /* State machine for packet assembly */
    switch (mouse_state.state) {
        case MOUSE_STATE_BYTE1:
            /* First byte must have bit 3 set (always 1) */
            if (!(data & 0x08)) {
                /* Sync error - discard and wait for valid first byte */
                return true;
            }
            mouse_state.packet[0] = data;
            mouse_state.state = MOUSE_STATE_BYTE2;
            break;

        case MOUSE_STATE_BYTE2:
            mouse_state.packet[1] = data;
            mouse_state.state = MOUSE_STATE_BYTE3;
            break;

        case MOUSE_STATE_BYTE3:
            mouse_state.packet[2] = data;
            if (mouse_state.has_scroll) {
                mouse_state.state = MOUSE_STATE_BYTE4;
            } else {
                mouse_process_packet();
                mouse_state.state = MOUSE_STATE_BYTE1;
            }
            break;

        case MOUSE_STATE_BYTE4:
            mouse_state.packet[3] = data;
            mouse_process_packet();
            mouse_state.state = MOUSE_STATE_BYTE1;
            break;
    }

    return true;
}

/*
 * Get the PS/2 mouse driver
 */
Driver *ps2_mouse_get_driver(void)
{
    return &ps2_mouse_driver;
}

/*
 * Initialize PS/2 mouse (registers driver)
 */
void ps2_mouse_init(void)
{
    driver_register(&ps2_mouse_driver);
}
