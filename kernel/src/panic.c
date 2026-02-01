/*
 * ojjyOS v3 Kernel - Panic Handler Implementation
 *
 * Displays a clean panic screen with error information.
 */

#include "panic.h"
#include "framebuffer.h"
#include "serial.h"
#include "string.h"
#include "console.h"

/*
 * Draw panic screen
 */
static void draw_panic_screen(const char *message, InterruptFrame *frame)
{
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    /* Fill background with panic color */
    fb_clear(COLOR_PANIC_BG);

    /* Draw header */
    int y = 60;
    fb_draw_string(60, y, "ojjyOS v3 - Kernel Panic", COLOR_PANIC_TEXT, COLOR_PANIC_BG);

    y += 40;
    fb_draw_string(60, y, "The system has encountered a fatal error and cannot continue.",
                   COLOR_PANIC_TEXT, COLOR_PANIC_BG);

    /* Draw error message */
    y += 40;
    fb_draw_string(60, y, "Error: ", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
    fb_draw_string(60 + 7 * 8, y, message, COLOR_WHITE, COLOR_PANIC_BG);

    /* Draw register dump if available */
    if (frame) {
        y += 40;
        fb_draw_string(60, y, "Register State:", COLOR_PANIC_TEXT, COLOR_PANIC_BG);

        char buf[128];
        y += 20;

        /* Format each register line */
        utoa(frame->rax, buf, 16);
        fb_draw_string(60, y, "RAX: 0x", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(60 + 7 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);

        utoa(frame->rbx, buf, 16);
        fb_draw_string(260, y, "RBX: 0x", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(260 + 7 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);

        utoa(frame->rcx, buf, 16);
        fb_draw_string(460, y, "RCX: 0x", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(460 + 7 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);

        y += 18;
        utoa(frame->rdx, buf, 16);
        fb_draw_string(60, y, "RDX: 0x", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(60 + 7 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);

        utoa(frame->rsi, buf, 16);
        fb_draw_string(260, y, "RSI: 0x", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(260 + 7 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);

        utoa(frame->rdi, buf, 16);
        fb_draw_string(460, y, "RDI: 0x", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(460 + 7 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);

        y += 18;
        utoa(frame->rsp, buf, 16);
        fb_draw_string(60, y, "RSP: 0x", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(60 + 7 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);

        utoa(frame->rbp, buf, 16);
        fb_draw_string(260, y, "RBP: 0x", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(260 + 7 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);

        utoa(frame->rip, buf, 16);
        fb_draw_string(460, y, "RIP: 0x", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(460 + 7 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);

        y += 18;
        utoa(frame->rflags, buf, 16);
        fb_draw_string(60, y, "RFLAGS: 0x", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(60 + 10 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);

        utoa(frame->error_code, buf, 16);
        fb_draw_string(260, y, "Error Code: 0x", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(260 + 14 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);

        utoa(frame->int_num, buf, 10);
        fb_draw_string(460, y, "INT: ", COLOR_PANIC_TEXT, COLOR_PANIC_BG);
        fb_draw_string(460 + 5 * 8, y, buf, COLOR_WHITE, COLOR_PANIC_BG);
    }

    /* Draw footer */
    y = height - 80;
    fb_draw_string(60, y, "The system has been halted to prevent damage.",
                   COLOR_PANIC_TEXT, COLOR_PANIC_BG);
    y += 20;
    fb_draw_string(60, y, "Please restart your computer.",
                   COLOR_PANIC_TEXT, COLOR_PANIC_BG);

    (void)width;
}

/*
 * Kernel panic
 */
void panic(const char *message)
{
    /* Disable interrupts */
    cli();

    /* Log to serial */
    serial_printf("\n\n");
    serial_printf("========================================\n");
    serial_printf("        KERNEL PANIC\n");
    serial_printf("========================================\n");
    serial_printf("Error: %s\n", message);
    serial_printf("========================================\n");

    /* Draw panic screen */
    draw_panic_screen(message, NULL);

    /* Halt forever */
    while (1) {
        hlt();
    }
}

/*
 * Panic with interrupt frame
 */
void panic_with_frame(const char *message, InterruptFrame *frame)
{
    /* Disable interrupts */
    cli();

    /* Log to serial */
    serial_printf("\n\n");
    serial_printf("========================================\n");
    serial_printf("        KERNEL PANIC\n");
    serial_printf("========================================\n");
    serial_printf("Error: %s\n", message);
    serial_printf("\nRegisters:\n");
    serial_printf("  RAX: 0x%p  RBX: 0x%p\n", frame->rax, frame->rbx);
    serial_printf("  RCX: 0x%p  RDX: 0x%p\n", frame->rcx, frame->rdx);
    serial_printf("  RSI: 0x%p  RDI: 0x%p\n", frame->rsi, frame->rdi);
    serial_printf("  RSP: 0x%p  RBP: 0x%p\n", frame->rsp, frame->rbp);
    serial_printf("  RIP: 0x%p  RFLAGS: 0x%p\n", frame->rip, frame->rflags);
    serial_printf("  INT: %d  Error: 0x%x\n", frame->int_num, frame->error_code);
    serial_printf("========================================\n");

    /* Draw panic screen */
    draw_panic_screen(message, frame);

    /* Halt forever */
    while (1) {
        hlt();
    }
}
