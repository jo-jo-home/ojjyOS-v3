/*
 * ojjyOS v3 - TextEdit core (the part of the TextEdit and Notes editors
 * that neither draws nor opens files)
 *
 * The document is a grid of up to TEXTEDIT_MAX_LINES lines of up to
 * TEXTEDIT_LINE_MAX-1 characters (soft wrap raised that from 79: a file
 * that does not fit opens read-only, so paragraphs need the room). This
 * module owns every operation on that grid: cursor movement, selection,
 * insert/delete, the wrap layout that turns logical lines into visual
 * rows, and the word/character counts. Drawing, VFS loading/saving and
 * key mapping stay in the compositor; because these functions only work
 * on the struct, tests/test_textedit.c runs them on the host.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_UI_TEXTEDIT_H
#define _OJJY_UI_TEXTEDIT_H

#include "types.h"

#define TEXTEDIT_MAX_LINES  64
#define TEXTEDIT_LINE_MAX   256     /* incl. NUL; soft wrap raised this from 80 */

/* Never lay a document out narrower than this, whatever the window does. */
#define TEXTEDIT_WRAP_MIN_COLS 16

typedef struct {
    char lines[TEXTEDIT_MAX_LINES][TEXTEDIT_LINE_MAX];
    int line_count;
    int scroll;                  /* First visible wrap row */
    int cursor_line;
    int cursor_col;
    char file_path[128];
    char status[48];
    bool dirty;
    bool read_only;              /* file does not fit the grid: never written back */
    bool sel_active;
    int sel_line;
    int sel_start;
    int sel_end;
    int sel_anchor;               /* column where Shift-selection began */
    bool sel_dragging;
    int goal_col;                /* column Up/Down keep aiming for across rows */
    int view_cols;               /* wrap width of the last draw (16 until drawn) */
    int view_rows;               /* visible rows of the last draw (for Page keys) */
} TextEditState;

/* One visual row on screen: the logical line and the column range it
 * shows. Empty lines have one row of length 0. */
typedef struct {
    int line;
    int start;
    int len;
} TextEditWrapRow;

/* Worst case: every line wrapped every TEXTEDIT_WRAP_MIN_COLS columns,
 * plus one row per line for its remainder and one of slack. */
#define TEXTEDIT_WRAP_MAX_ROWS \
    (TEXTEDIT_MAX_LINES * ((TEXTEDIT_LINE_MAX - 2) / TEXTEDIT_WRAP_MIN_COLS + 2) + 1)

/* ---- editing core ---- */

void textedit_clear(TextEditState *edit);
void textedit_normalize_selection(TextEditState *edit);
void textedit_clear_selection(TextEditState *edit);
/* Copy the selection into `out` (truncated to size); no-op without one. */
void textedit_copy_selection(TextEditState *edit, char *out, size_t size);
/* Delete the selected range and place the cursor at its start. */
void textedit_delete_selection(TextEditState *edit);

/* Remove `line` (never the last one); the cursor lands on its start. */
void textedit_delete_line(TextEditState *edit, int line);
/* Insert `text` as a new line at `line` (no-op when the grid is full). */
void textedit_insert_line(TextEditState *edit, int line, const char *text);
/* Type one character at the cursor. False when the line is full or the
 * document is read-only (status says so; nothing changes). */
bool textedit_insert_char(TextEditState *edit, char c);
/* Enter: split the line at the cursor, or start a new empty line. False
 * when the grid is full. */
bool textedit_newline(TextEditState *edit);
/* Backspace: delete the selection, the character left of the cursor, or
 * join with the previous line when that fits (refuses rather than
 * truncating when the two lines together are too long). */
bool textedit_backspace(TextEditState *edit);

/* ---- movement ---- */

/* Left/Right; `word_skip` jumps to the previous/next word boundary and
 * `select` extends the selection from its anchor. */
void textedit_move_horiz(TextEditState *edit, int delta, bool word_skip, bool select);
/* Home/End: to the start/end of the logical line. */
void textedit_move_line_edge(TextEditState *edit, bool end, bool select);
/* Up/Down over visual (wrapped) rows, keeping the goal column; clamps at
 * the first/last row. Returns false when it could not move. */
bool textedit_move_visual(TextEditState *edit, int cols, int delta);

/* ---- wrap layout and counts ---- */

/* Lays the whole document out for a view `cols` columns wide (clamped to
 * at least TEXTEDIT_WRAP_MIN_COLS). The rows live in a single shared
 * scratch buffer: valid until the next call, which is fine for the one
 * compositor and for the host tests. */
const TextEditWrapRow *textedit_wrap_rows(const TextEditState *edit, int cols, int *row_count);
/* Index of the row the cursor is on (the earlier row at a wrap point). */
int textedit_cursor_row(const TextEditState *edit, const TextEditWrapRow *rows, int row_count);
/* Words are non-whitespace runs; characters are all visible bytes
 * (newlines are not counted). */
void textedit_counts(const TextEditState *edit, int *words, int *chars);

#endif /* _OJJY_UI_TEXTEDIT_H */
