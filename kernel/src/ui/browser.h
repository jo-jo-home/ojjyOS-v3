/*
 * ojjyOS v3 - Web browser app (single instance, drawn inside the Web window)
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_BROWSER_H
#define _OJJY_BROWSER_H

#include "types.h"
#include "../drivers/input.h"

void browser_open(void);                      /* window opened: show start page once */
void browser_draw(int x, int y, int w, int h);
void browser_poll(void);                      /* every main-loop tick */
bool browser_click(int x, int y);
bool browser_hover(int x, int y);             /* true when the status line changed */
bool browser_key(KeyCode key, char ascii, uint8_t modifiers);
bool browser_shortcut(KeyCode key);           /* Cmd+L/R/[/]/. */
void browser_scroll(int pixels);
const char *browser_title(void);
void browser_go(const char *address);         /* navigate as if typed in the bar */
bool browser_loading(void);

#endif /* _OJJY_BROWSER_H */
