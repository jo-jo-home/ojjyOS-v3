/*
 * ojjyOS v3 - TextEdit core implementation
 *
 * Pure operations on the editor grid: no drawing, no VFS. The compositor
 * keeps load/save and key mapping; tests/test_textedit.c runs every
 * function here on the host (including under ASan/UBSan).
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "ui/textedit.h"
#include "string.h"

static bool char_is_space(char c)
{
    return c == ' ' || c == '\t';
}

void textedit_clear(TextEditState *edit)
{
    edit->line_count = 1;
    edit->cursor_line = 0;
    edit->cursor_col = 0;
    edit->scroll = 0;
    edit->goal_col = 0;
    memset(edit->lines, 0, sizeof(edit->lines));
    edit->dirty = false;
    edit->read_only = false;
    edit->sel_active = false;
    edit->sel_line = 0;
    edit->sel_start = 0;
    edit->sel_end = 0;
    edit->sel_anchor = 0;
    edit->sel_dragging = false;
}

void textedit_normalize_selection(TextEditState *edit)
{
    if (!edit || !edit->sel_active) return;
    if (edit->sel_start > edit->sel_end) {
        int tmp = edit->sel_start;
        edit->sel_start = edit->sel_end;
        edit->sel_end = tmp;
    }
}

void textedit_clear_selection(TextEditState *edit)
{
    if (!edit) return;
    edit->sel_active = false;
}

/* After a cursor move: start/extend the selection from its anchor, or
 * drop it. The anchor is where the first Shift move began, so selecting
 * left and right of it both work (the old code only extended right). */
static void selection_after_move(TextEditState *edit, int anchor, bool select)
{
    if (!select) {
        textedit_clear_selection(edit);
        return;
    }
    if (!edit->sel_active) {
        edit->sel_active = true;
        edit->sel_line = edit->cursor_line;
        edit->sel_anchor = anchor;
    }
    edit->sel_start = MIN(edit->sel_anchor, edit->cursor_col);
    edit->sel_end = MAX(edit->sel_anchor, edit->cursor_col);
}

void textedit_copy_selection(TextEditState *edit, char *out, size_t size)
{
    if (!edit || !out || size == 0) return;
    out[0] = '\0';
    if (!edit->sel_active) return;
    textedit_normalize_selection(edit);
    if (edit->sel_line < 0 || edit->sel_line >= edit->line_count) return;

    int len = (int)strlen(edit->lines[edit->sel_line]);
    int start = edit->sel_start;
    int end = edit->sel_end;
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return;

    int copy = end - start;
    if (copy > (int)size - 1) copy = (int)size - 1;
    memcpy(out, &edit->lines[edit->sel_line][start], (size_t)copy);
    out[copy] = '\0';
}

void textedit_delete_selection(TextEditState *edit)
{
    if (!edit || !edit->sel_active) return;
    if (edit->sel_line < 0 || edit->sel_line >= edit->line_count) return;

    textedit_normalize_selection(edit);
    int line = edit->sel_line;
    int start = edit->sel_start;
    int end = edit->sel_end;
    int len = (int)strlen(edit->lines[line]);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) {
        textedit_clear_selection(edit);
        return;
    }

    for (int i = end; i <= len; i++) {
        edit->lines[line][start + (i - end)] = edit->lines[line][i];
    }
    edit->cursor_line = line;
    edit->cursor_col = start;
    edit->goal_col = start;
    edit->dirty = true;
    strcpy(edit->status, "Edited");
    textedit_clear_selection(edit);
}

void textedit_delete_line(TextEditState *edit, int line)
{
    if (!edit) return;
    if (line < 0 || line >= edit->line_count) return;
    for (int i = line + 1; i < edit->line_count; i++) {
        strncpy(edit->lines[i - 1], edit->lines[i], TEXTEDIT_LINE_MAX - 1);
        edit->lines[i - 1][TEXTEDIT_LINE_MAX - 1] = '\0';
    }
    if (edit->line_count > 1) {
        edit->line_count--;
    }
    if (edit->cursor_line >= edit->line_count) {
        edit->cursor_line = edit->line_count - 1;
    }
    if (edit->cursor_line < 0) edit->cursor_line = 0;
    edit->cursor_col = 0;
    edit->goal_col = 0;
    edit->dirty = true;
    strcpy(edit->status, "Edited");
}

void textedit_insert_line(TextEditState *edit, int line, const char *text)
{
    if (!edit || !text) return;
    if (edit->line_count >= TEXTEDIT_MAX_LINES) return;
    if (line < 0) line = 0;
    if (line > edit->line_count) line = edit->line_count;

    for (int i = edit->line_count; i > line; i--) {
        strncpy(edit->lines[i], edit->lines[i - 1], TEXTEDIT_LINE_MAX - 1);
        /* Guarantee termination when the shifted line is full-width */
        edit->lines[i][TEXTEDIT_LINE_MAX - 1] = '\0';
    }
    strncpy(edit->lines[line], text, TEXTEDIT_LINE_MAX - 1);
    edit->lines[line][TEXTEDIT_LINE_MAX - 1] = '\0';
    edit->line_count++;
    edit->dirty = true;
    strcpy(edit->status, "Edited");
}

bool textedit_insert_char(TextEditState *edit, char c)
{
    if (!edit || c < 32 || c > 126) return false;
    if (edit->sel_active) {
        textedit_delete_selection(edit);
    }
    int len = (int)strlen(edit->lines[edit->cursor_line]);
    if (edit->cursor_col > len) {
        edit->cursor_col = len;
    }
    /* Insert (shift right) only while the line has room; when the line is
     * full nothing changes (the old code silently overwrote a character
     * instead, then later dropped it). */
    if (edit->cursor_col >= TEXTEDIT_LINE_MAX - 1 || len >= TEXTEDIT_LINE_MAX - 1) {
        strcpy(edit->status, "Line full");
        return false;
    }
    for (int i = len; i >= edit->cursor_col; i--) {
        edit->lines[edit->cursor_line][i + 1] = edit->lines[edit->cursor_line][i];
    }
    edit->lines[edit->cursor_line][edit->cursor_col] = c;
    edit->cursor_col++;
    edit->goal_col = edit->cursor_col;
    edit->dirty = true;
    strcpy(edit->status, "Edited");
    return true;
}

bool textedit_newline(TextEditState *edit)
{
    if (!edit) return false;
    /* The grid keeps one spare line below the cursor (same bound the old
     * editor had), so a split can never fall off the end. */
    if (edit->line_count >= TEXTEDIT_MAX_LINES - 1) {
        strcpy(edit->status, "Document full");
        return false;
    }
    if (edit->cursor_col < (int)strlen(edit->lines[edit->cursor_line])) {
        /* Split: the tail becomes a new line after the cursor line
         * (insert_line bumps line_count exactly once). */
        char tail[TEXTEDIT_LINE_MAX];
        strncpy(tail, &edit->lines[edit->cursor_line][edit->cursor_col], sizeof(tail) - 1);
        tail[sizeof(tail) - 1] = '\0';
        edit->lines[edit->cursor_line][edit->cursor_col] = '\0';
        textedit_insert_line(edit, edit->cursor_line + 1, tail);
        edit->cursor_line++;
    } else {
        /* New empty line at the cursor position */
        edit->cursor_line++;
        if (edit->cursor_line >= edit->line_count) {
            edit->line_count = edit->cursor_line + 1;
            edit->lines[edit->cursor_line][0] = '\0';
        }
    }
    edit->cursor_col = 0;
    edit->goal_col = 0;
    edit->dirty = true;
    strcpy(edit->status, "Edited");
    return true;
}

bool textedit_backspace(TextEditState *edit)
{
    if (!edit) return false;
    if (edit->sel_active) {
        textedit_delete_selection(edit);
        return true;
    }
    if (edit->cursor_col > 0) {
        /* Delete the character left of the cursor by shifting the rest
         * of the line left (the old editor truncated the line at the
         * cursor instead, losing everything after a mid-line caret). */
        int len = (int)strlen(edit->lines[edit->cursor_line]);
        edit->cursor_col--;
        for (int i = edit->cursor_col; i < len; i++) {
            edit->lines[edit->cursor_line][i] = edit->lines[edit->cursor_line][i + 1];
        }
        edit->goal_col = edit->cursor_col;
        edit->dirty = true;
        strcpy(edit->status, "Edited");
        return true;
    }
    if (edit->cursor_line > 0) {
        int prev = edit->cursor_line - 1;
        int prev_len = (int)strlen(edit->lines[prev]);
        int cur_len = (int)strlen(edit->lines[edit->cursor_line]);
        /* Join only when the merged line fits; the old code appended
         * unconditionally and silently truncated the tail. */
        if (prev_len + cur_len > TEXTEDIT_LINE_MAX - 1) {
            strcpy(edit->status, "Can't join: line full");
            return false;
        }
        memcpy(edit->lines[prev] + prev_len, edit->lines[edit->cursor_line], (size_t)cur_len);
        edit->lines[prev][prev_len + cur_len] = '\0';
        textedit_delete_line(edit, edit->cursor_line);
        edit->cursor_line = prev;
        edit->cursor_col = prev_len;
        edit->goal_col = prev_len;
        edit->dirty = true;
        strcpy(edit->status, "Edited");
        return true;
    }
    return false;                            /* cursor at the very start */
}

void textedit_move_horiz(TextEditState *edit, int delta, bool word_skip, bool select)
{
    if (!edit || delta == 0) return;
    int anchor = edit->cursor_col;
    int len = (int)strlen(edit->lines[edit->cursor_line]);

    if (delta > 0) {                        /* right / next word */
        int col = edit->cursor_col;
        if (word_skip) {
            while (col < len && edit->lines[edit->cursor_line][col] != ' ') col++;
            while (col < len && edit->lines[edit->cursor_line][col] == ' ') col++;
        } else if (col < len) {
            col++;
        }
        edit->cursor_col = col;
    } else {                                /* left / previous word */
        int col = edit->cursor_col;
        if (word_skip) {
            while (col > 0 && edit->lines[edit->cursor_line][col - 1] == ' ') col--;
            while (col > 0 && edit->lines[edit->cursor_line][col - 1] != ' ') col--;
        } else if (col > 0) {
            col--;
        }
        edit->cursor_col = col;
    }
    edit->goal_col = edit->cursor_col;
    selection_after_move(edit, anchor, select);
}

void textedit_move_line_edge(TextEditState *edit, bool end, bool select)
{
    if (!edit) return;
    int anchor = edit->cursor_col;
    edit->cursor_col = end ? (int)strlen(edit->lines[edit->cursor_line]) : 0;
    edit->goal_col = edit->cursor_col;
    selection_after_move(edit, anchor, select);
}

bool textedit_move_visual(TextEditState *edit, int cols, int delta)
{
    if (!edit || delta == 0) return false;
    int row_count;
    const TextEditWrapRow *rows = textedit_wrap_rows(edit, cols, &row_count);
    if (row_count < 1) return false;

    int row = textedit_cursor_row(edit, rows, row_count);
    int target = row + delta;
    if (target < 0) target = 0;
    if (target > row_count - 1) target = row_count - 1;
    if (target == row) return false;

    const TextEditWrapRow *r = &rows[target];
    /* Keep aiming at the goal column, clamped into the target row. */
    int col = edit->goal_col;
    if (col < r->start) col = r->start;
    if (col > r->start + r->len) col = r->start + r->len;
    edit->cursor_line = r->line;
    edit->cursor_col = col;
    return true;
}

const TextEditWrapRow *textedit_wrap_rows(const TextEditState *edit, int cols, int *row_count)
{
    /* Single shared scratch layout: big enough for the worst case, and
     * only valid until the next call (one compositor, one test run). */
    static TextEditWrapRow rows[TEXTEDIT_WRAP_MAX_ROWS];
    if (!edit || !row_count) {
        if (row_count) *row_count = 0;
        return rows;
    }
    if (cols < TEXTEDIT_WRAP_MIN_COLS) cols = TEXTEDIT_WRAP_MIN_COLS;

    int count = 0;
    int lines = edit->line_count > 0 ? edit->line_count : 1;
    for (int i = 0; i < lines; i++) {
        const char *text = edit->line_count > 0 ? edit->lines[i] : "";
        int len = (int)strlen(text);
        int start = 0;
        while (len - start > cols && count < TEXTEDIT_WRAP_MAX_ROWS) {
            int end = start + cols;
            /* Break after the last space inside the window; a word wider
             * than the window hard-breaks at the edge. end always moves
             * at least one column, so this terminates. */
            for (int s = end - 1; s >= start; s--) {
                if (text[s] == ' ') {
                    end = s + 1;
                    break;
                }
            }
            rows[count].line = i;
            rows[count].start = start;
            rows[count].len = end - start;
            count++;
            start = end;
        }
        if (count < TEXTEDIT_WRAP_MAX_ROWS) {
            rows[count].line = i;
            rows[count].start = start;
            rows[count].len = len - start;
            count++;
        }
    }
    *row_count = count;
    return rows;
}

int textedit_cursor_row(const TextEditState *edit, const TextEditWrapRow *rows, int row_count)
{
    if (!edit || !rows || row_count < 1) return 0;
    for (int r = 0; r < row_count; r++) {
        if (rows[r].line != edit->cursor_line) continue;
        /* A cursor sitting exactly at a wrap point belongs to the earlier
         * row: the caret rests at the end of that row. */
        if (edit->cursor_col <= rows[r].start + rows[r].len) return r;
    }
    return row_count - 1;
}

void textedit_counts(const TextEditState *edit, int *words, int *chars)
{
    int w = 0;
    int c = 0;
    if (edit) {
        for (int i = 0; i < edit->line_count; i++) {
            bool in_word = false;
            for (const char *p = edit->lines[i]; *p; p++) {
                c++;
                if (char_is_space(*p)) {
                    in_word = false;
                } else if (!in_word) {
                    in_word = true;
                    w++;
                }
            }
            /* The newline that ends a line also ends a word. */
        }
    }
    if (words) *words = w;
    if (chars) *chars = c;
}
