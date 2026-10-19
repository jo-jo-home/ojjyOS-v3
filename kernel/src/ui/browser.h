/*
 * ojjyOS v3 - ojjyOS Web, the tabbed browser app (drawn inside the Web
 * window, which hands the browser its whole rectangle: the tab strip lives
 * in the title bar like Chrome's).
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_BROWSER_H
#define _OJJY_BROWSER_H

#include "types.h"
#include "../drivers/input.h"

void browser_open(void);                      /* window opened: first tab */
void browser_draw(int x, int y, int w, int h);/* whole window rect */
void browser_poll(void);                      /* every main-loop tick */
bool browser_click(int x, int y);
bool browser_titlebar_click(int x, int y);    /* tabs / + button: true when hit */
bool browser_right_click(int x, int y);       /* page context menu */
bool browser_hover(int x, int y);             /* true when something changed */
bool browser_key(KeyCode key, char ascii, uint8_t modifiers);
bool browser_shortcut(KeyCode key, uint8_t modifiers);   /* Cmd+... */
void browser_scroll(int pixels);
const char *browser_title(void);
void browser_go(const char *address);         /* navigate the active tab */
bool browser_loading(void);

#endif /* _OJJY_BROWSER_H */
