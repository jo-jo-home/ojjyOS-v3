/*
 * ojjyOS v3 Kernel - Text Console Implementation
 *
 * Text mode console over framebuffer with scrolling.
 */

#include "console.h"
#include "framebuffer.h"
#include "font.h"
#include "string.h"
#include "serial.h"

/* Console state */
static int con_col = 0;
static int con_row = 0;
static int con_cols = 0;
static int con_rows = 0;
static Color con_fg = COLOR_TEXT;
static Color con_bg = COLOR_CREAM;

/*
 * Initialize console
 */
void console_init(void)
{
    con_cols = fb_get_width() / FONT_WIDTH;
    con_rows = fb_get_height() / FONT_HEIGHT;
    con_col = 0;
    con_row = 0;

    /* Default colors */
    con_fg = COLOR_TEXT;
    con_bg = COLOR_CREAM;
}

/*
 * Set console colors
 */
void console_set_colors(Color fg, Color bg)
{
    con_fg = fg;
    con_bg = bg;
}

/*
 * Clear console
 */
void console_clear(void)
{
    fb_clear(con_bg);
    con_col = 0;
    con_row = 0;
}

/*
 * Scroll the console up by one line
 */
static void console_scroll(void)
{
    /* Copy all lines up */
    fb_copy_rect(0, 0, 0, FONT_HEIGHT,
                 fb_get_width(), fb_get_height() - FONT_HEIGHT);

    /* Clear the last line */
    fb_fill_rect(0, (con_rows - 1) * FONT_HEIGHT,
                 fb_get_width(), FONT_HEIGHT, con_bg);

    con_row = con_rows - 1;
}

/*
 * Print a character
 */
void console_putc(char c)
{
    /* Also output to serial */
    serial_putc(c);

    if (c == '\n') {
        con_col = 0;
        con_row++;
    } else if (c == '\r') {
        con_col = 0;
    } else if (c == '\t') {
        con_col = (con_col + 4) & ~3;
    } else if (c == '\b') {
        if (con_col > 0) {
            con_col--;
            fb_draw_char(con_col * FONT_WIDTH, con_row * FONT_HEIGHT,
                        ' ', con_fg, con_bg);
        }
    } else {
        /* Draw the character */
        fb_draw_char(con_col * FONT_WIDTH, con_row * FONT_HEIGHT,
                    c, con_fg, con_bg);
        con_col++;
    }

    /* Handle line wrap */
    if (con_col >= con_cols) {
        con_col = 0;
        con_row++;
    }

    /* Handle scroll */
    if (con_row >= con_rows) {
        console_scroll();
    }
}

/*
 * Print a string
 */
void console_puts(const char *s)
{
    while (*s) {
        console_putc(*s++);
    }
}

/*
 * Formatted print (supports %s, %d, %u, %x, %p, %c, %%)
 */
void console_printf(const char *fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            console_putc(*fmt++);
            continue;
        }

        fmt++;  /* Skip '%' */

        /* Handle width/padding (simple version) */
        int width = 0;
        int pad_zero = 0;

        if (*fmt == '0') {
            pad_zero = 1;
            fmt++;
        }

        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Handle 'l' length modifier */
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                fmt++;  /* Consume second 'l' in 'll' */
            }
        }

        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(args, const char *);
            if (!s) s = "(null)";
            int len = strlen(s);
            while (len < width) {
                console_putc(' ');
                width--;
            }
            console_puts(s);
            break;
        }
        case 'd': {
            int64_t val;
            if (is_long) {
                val = __builtin_va_arg(args, int64_t);
            } else {
                val = __builtin_va_arg(args, int);
            }
            char buf[32];
            itoa(val, buf, 10);
            int len = strlen(buf);
            while (len < width) {
                console_putc(pad_zero ? '0' : ' ');
                width--;
            }
            console_puts(buf);
            break;
        }
        case 'u': {
            uint64_t val;
            if (is_long) {
                val = __builtin_va_arg(args, uint64_t);
            } else {
                val = __builtin_va_arg(args, unsigned int);
            }
            char buf[32];
            utoa(val, buf, 10);
            int len = strlen(buf);
            while (len < width) {
                console_putc(pad_zero ? '0' : ' ');
                width--;
            }
            console_puts(buf);
            break;
        }
        case 'x': {
            uint64_t val;
            if (is_long) {
                val = __builtin_va_arg(args, uint64_t);
            } else {
                val = __builtin_va_arg(args, unsigned int);
            }
            char buf[32];
            utoa(val, buf, 16);
            int len = strlen(buf);
            while (len < width) {
                console_putc(pad_zero ? '0' : ' ');
                width--;
            }
            console_puts(buf);
            break;
        }
        case 'p': {
            uint64_t val = __builtin_va_arg(args, uint64_t);
            console_puts("0x");
            char buf[32];
            utoa(val, buf, 16);
            int len = strlen(buf);
            while (len < 16) {
                console_putc('0');
                len++;
            }
            console_puts(buf);
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(args, int);
            console_putc(c);
            break;
        }
        case '%':
            console_putc('%');
            break;
        default:
            console_putc('%');
            console_putc(*fmt);
            break;
        }

        fmt++;
    }

    __builtin_va_end(args);
}

/*
 * Set cursor position
 */
void console_set_cursor(int x, int y)
{
    con_col = x;
    con_row = y;

    if (con_col < 0) con_col = 0;
    if (con_col >= con_cols) con_col = con_cols - 1;
    if (con_row < 0) con_row = 0;
    if (con_row >= con_rows) con_row = con_rows - 1;
}

/*
 * Get cursor position
 */
void console_get_cursor(int *x, int *y)
{
    if (x) *x = con_col;
    if (y) *y = con_row;
}
