/*
 * Host test for the TextEdit core (kernel/src/ui/textedit.c): the soft
 * wrap layout that turns logical lines into visual rows (break at
 * spaces, hard-break long words, one row per empty line), cursor
 * movement over those rows with the goal column, Home/End, selection
 * extension from its anchor, the editing operations (insert, newline
 * split, backspace join that refuses to truncate), the word/character
 * counts and the wrap invariants over a deterministic fuzz loop.
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "ui/textedit.h"
#include "test_support.h"

static void set_doc(TextEditState *edit, const char *const *lines, int count)
{
    textedit_clear(edit);
    edit->line_count = count;
    for (int i = 0; i < count && i < TEXTEDIT_MAX_LINES; i++) {
        strncpy(edit->lines[i], lines[i], TEXTEDIT_LINE_MAX - 1);
        edit->lines[i][TEXTEDIT_LINE_MAX - 1] = '\0';
    }
}

static void test_wrap_basics(void)
{
    TextEditState edit;
    int n;
    const TextEditWrapRow *rows;

    textedit_clear(&edit);
    rows = textedit_wrap_rows(&edit, 30, &n);
    CHECK(n == 1 && rows[0].line == 0 && rows[0].start == 0 && rows[0].len == 0,
          "empty document has one empty row");

    const char *one[] = {"hello"};
    set_doc(&edit, one, 1);
    rows = textedit_wrap_rows(&edit, 20, &n);
    CHECK(n == 1 && rows[0].line == 0 && rows[0].start == 0 && rows[0].len == 5,
          "short line is one row");

    const char *exact[] = {"0123456789abcdef"};
    set_doc(&edit, exact, 1);
    rows = textedit_wrap_rows(&edit, 16, &n);
    CHECK(n == 1 && rows[0].len == 16, "exactly the width is one row");

    const char *wrap[] = {"abc def ghi jkl mno"};
    set_doc(&edit, wrap, 1);
    rows = textedit_wrap_rows(&edit, 16, &n);
    CHECK(n == 2 && rows[0].start == 0 && rows[0].len == 16 && rows[1].start == 16 && rows[1].len == 3,
          "wraps after the last space in the window");

    const char *hard[] = {"aaaaaaaaaaaaaaaaaaaaaaaa"};
    set_doc(&edit, hard, 1);
    rows = textedit_wrap_rows(&edit, 16, &n);
    CHECK(n == 2 && rows[0].len == 16 && rows[1].start == 16 && rows[1].len == 8,
          "word wider than the view hard-breaks");

    const char *lead[] = {" aaaaaaaaaaaaaaaaaaaaaaaa"};
    set_doc(&edit, lead, 1);
    rows = textedit_wrap_rows(&edit, 16, &n);
    CHECK(n == 3 && rows[0].len == 1 && rows[1].start == 1 && rows[1].len == 16 &&
          rows[2].start == 17, "space that cannot join its word keeps its own row");

    const char *many[] = {"aaaaaaaaaa  bbbbbbbbbb"};
    set_doc(&edit, many, 1);
    rows = textedit_wrap_rows(&edit, 16, &n);
    CHECK(n == 2 && rows[0].len == 12 && rows[1].start == 12 && rows[1].len == 10,
          "runs of spaces wrap with the first word that fits");

    const char *trail[] = {"aaaaaaaaaaaaaa   "};
    set_doc(&edit, trail, 1);
    rows = textedit_wrap_rows(&edit, 16, &n);
    CHECK(n == 2 && rows[0].len == 16 && rows[1].start == 16 && rows[1].len == 1,
          "trailing spaces take the last row");

    /* Narrow views clamp to the minimum width. */
    const char *wide[] = {"01234567890123456789"};
    set_doc(&edit, wide, 1);
    rows = textedit_wrap_rows(&edit, 4, &n);
    CHECK(n == 2 && rows[0].len == TEXTEDIT_WRAP_MIN_COLS &&
          rows[1].start == TEXTEDIT_WRAP_MIN_COLS,
          "view narrower than the minimum wraps at 16 columns");

    const char *doc3[] = {"first line", "", "third"};
    set_doc(&edit, doc3, 3);
    rows = textedit_wrap_rows(&edit, 30, &n);
    CHECK(n == 3 && rows[1].line == 1 && rows[1].len == 0 && rows[2].line == 2,
          "every line, even an empty one, keeps a row");
}

static void test_wrap_worst_case(void)
{
    TextEditState edit;
    textedit_clear(&edit);
    edit.line_count = TEXTEDIT_MAX_LINES;
    for (int i = 0; i < TEXTEDIT_MAX_LINES; i++) {
        memset(edit.lines[i], 'a', TEXTEDIT_LINE_MAX - 1);
        edit.lines[i][TEXTEDIT_LINE_MAX - 1] = '\0';
    }
    int n;
    textedit_wrap_rows(&edit, TEXTEDIT_WRAP_MIN_COLS, &n);
    CHECK(n == TEXTEDIT_MAX_LINES *
                  ((TEXTEDIT_LINE_MAX - 1 + TEXTEDIT_WRAP_MIN_COLS - 1) / TEXTEDIT_WRAP_MIN_COLS),
          "64 full lines wrap into the expected row count");
    CHECK(n <= TEXTEDIT_WRAP_MAX_ROWS, "worst case fits the scratch size");
}

static void test_cursor_row(void)
{
    TextEditState edit;
    const char *doc[] = {"abc def ghi jkl mno", "second"};
    set_doc(&edit, doc, 2);
    int n;
    const TextEditWrapRow *rows = textedit_wrap_rows(&edit, 16, &n);
    /* The first line wraps after the space at column 15. */
    CHECK(n == 3, "two lines, one wrapped: three rows");

    edit.cursor_line = 0;
    edit.cursor_col = 15;                      /* the wrap-point space */
    CHECK(textedit_cursor_row(&edit, rows, n) == 0,
          "cursor at a wrap point belongs to the earlier row");
    edit.cursor_col = 17;
    CHECK(textedit_cursor_row(&edit, rows, n) == 1, "cursor past the break is on the next row");
    edit.cursor_line = 1;
    edit.cursor_col = 6;
    CHECK(textedit_cursor_row(&edit, rows, n) == 2, "cursor at line end is on its row");
    edit.cursor_col = 0;
    CHECK(textedit_cursor_row(&edit, rows, n) == 2, "cursor at line start is on its row");
}

/* Layout invariants over deterministic pseudo-random documents. */
static void test_wrap_fuzz(void)
{
    unsigned seed = 12345;
    int cases = 0;
    for (int iter = 0; iter < 500; iter++) {
        TextEditState edit;
        textedit_clear(&edit);
        seed = seed * 1103515245u + 12345u;
        edit.line_count = 1 + (int)((seed >> 8) % 8);
        for (int i = 0; i < edit.line_count; i++) {
            seed = seed * 1103515245u + 12345u;
            int len = (int)((seed >> 8) % 90);
            for (int c = 0; c < len; c++) {
                seed = seed * 1103515245u + 12345u;
                edit.lines[i][c] = "ab  cd "[(seed >> 8) % 7];
            }
            edit.lines[i][len] = '\0';
        }
        static const int widths[] = {16, 17, 20, 33, 64};
        seed = seed * 1103515245u + 12345u;
        int cols = widths[(seed >> 8) % 5];
        int n;
        const TextEditWrapRow *rows = textedit_wrap_rows(&edit, cols, &n);
        cases++;

        int ok = n >= edit.line_count;
        int last_line = -1, last_start = 0, last_len = 0;
        for (int r = 0; r < n && ok; r++) {
            const TextEditWrapRow *row = &rows[r];
            int len = (int)strlen(edit.lines[row->line]);
            if (row->line < last_line) ok = 0;                     /* ordered by line */
            if (row->line == last_line && row->start != last_start + last_len) ok = 0;
            if (row->line == last_line && row->len == 0) ok = 0;   /* no empty split rows */
            if (row->start + row->len > len) ok = 0;               /* stays inside the line */
            if (r < n - 1 && rows[r + 1].line == row->line) {
                /* A row followed by more of the same line: at most one
                 * window wide, and word-breaks end just after a space. */
                if (row->len > cols) ok = 0;
                if (row->len < cols && edit.lines[row->line][row->start + row->len - 1] != ' ') ok = 0;
            }
            last_line = row->line;
            last_start = row->start;
            last_len = row->len;
        }
        /* Every line was reached and the last one runs to its end. */
        if (ok) ok = last_line == edit.line_count - 1;
        if (ok) ok = rows[n - 1].start + rows[n - 1].len ==
                         (int)strlen(edit.lines[edit.line_count - 1]);
        /* The cursor maps into the row that contains it. */
        if (ok) {
            edit.cursor_line = last_line;
            edit.cursor_col = rows[n - 1].start + rows[n - 1].len;
            int r = textedit_cursor_row(&edit, rows, n);
            ok = r >= 0 && r < n && rows[r].line == edit.cursor_line &&
                 edit.cursor_col >= rows[r].start &&
                 edit.cursor_col <= rows[r].start + rows[r].len;
        }
        if (!ok) {
            failures++;
            emit("FAIL: fuzz wrap invariant, case:");
            char num[12];
            utoa((uint64_t)cases, num, 10);
            emit(num);
            return;
        }
    }
    CHECK(cases == 500, "fuzz ran 500 wrap layouts");
}

static void test_movement(void)
{
    TextEditState edit;
    const char *doc[] = {"one two three four five six seven eight", "short", "x"};
    set_doc(&edit, doc, 3);

    edit.cursor_line = 0;
    edit.cursor_col = 0;
    edit.goal_col = 0;
    int n;
    const TextEditWrapRow *rows = textedit_wrap_rows(&edit, 16, &n);
    CHECK(n >= 5, "long line wraps into several rows");
    CHECK(textedit_move_visual(&edit, 16, 1), "Down moves");
    CHECK(edit.cursor_line == 0 && edit.cursor_col == rows[1].start,
          "Down from the row start lands at the next row start");

    /* The goal column survives short rows on the way down. */
    edit.cursor_line = 0;
    edit.cursor_col = 30;
    edit.goal_col = 30;
    int r30 = textedit_cursor_row(&edit, rows, n);
    CHECK(textedit_move_visual(&edit, 16, 1), "Down from mid-line");
    CHECK(edit.cursor_line == rows[r30 + 1].line, "Down lands on the next row's line");
    CHECK(edit.cursor_col >= rows[r30 + 1].start &&
          edit.cursor_col <= rows[r30 + 1].start + rows[r30 + 1].len,
          "cursor stays inside the target row");
    while (edit.cursor_line < 2) {
        if (!textedit_move_visual(&edit, 16, 1)) break;
    }
    CHECK(edit.cursor_line == 2 && edit.cursor_col == 1,
          "goal column clamps to the end of the last line");
    CHECK(textedit_move_visual(&edit, 16, 1) == false, "Down clamps at the last row");
    CHECK(textedit_move_visual(&edit, 16, -5) == true && edit.cursor_line == 0,
          "big Up jumps back to the first line");

    /* Left/Right move inside the logical line; the caret walks through
     * wrap points for free because wrapping adds no characters. */
    const char *doc2[] = {"alpha beta gamma delta", "z"};
    set_doc(&edit, doc2, 2);
    textedit_wrap_rows(&edit, 16, &n);
    CHECK(n == 3, "alpha beta gamma delta wraps once");
    edit.cursor_line = 0;
    edit.cursor_col = 0;
    edit.goal_col = 0;
    textedit_move_visual(&edit, 16, 2);
    CHECK(edit.cursor_line == 1, "two Downs cross the wrap and the line");
    textedit_move_horiz(&edit, -1, false, false);
    CHECK(edit.cursor_line == 1 && edit.cursor_col == 0,
          "Left at a line start stays there");
    textedit_move_line_edge(&edit, true, false);
    textedit_move_horiz(&edit, 1, false, false);
    CHECK(edit.cursor_line == 1 && edit.cursor_col == 1,
          "Right at a line end stays there");
    edit.cursor_line = 0;
    edit.cursor_col = 20;                       /* inside the last row */
    textedit_move_horiz(&edit, 1, false, false);
    CHECK(edit.cursor_col == 21, "Right walks past a wrap point inside the line");
    textedit_move_horiz(&edit, -1, false, false);
    CHECK(edit.cursor_col == 20, "Left walks back over the wrap point");

    /* Word jumps (Ctrl+arrows). */
    const char *doc3[] = {"hello  world"};
    set_doc(&edit, doc3, 1);
    edit.cursor_col = 0;
    textedit_move_horiz(&edit, 1, true, false);
    CHECK(edit.cursor_col == 7, "Ctrl+Right lands before the second word");
    textedit_move_horiz(&edit, -1, true, false);
    CHECK(edit.cursor_col == 0, "Ctrl+Left returns to the start");

    /* Home/End. */
    textedit_move_line_edge(&edit, true, false);
    CHECK(edit.cursor_col == 12, "End reaches the line end");
    textedit_move_line_edge(&edit, false, false);
    CHECK(edit.cursor_col == 0, "Home reaches the line start");
}

static void test_selection(void)
{
    TextEditState edit;
    const char *doc[] = {"hello world"};
    set_doc(&edit, doc, 1);

    /* Shift+Right extends one char per press. */
    edit.cursor_col = 2;
    textedit_move_horiz(&edit, 1, false, true);
    CHECK(edit.sel_active && edit.sel_start == 2 && edit.sel_end == 3, "first Shift+Right selects one");
    textedit_move_horiz(&edit, 1, false, true);
    CHECK(edit.sel_start == 2 && edit.sel_end == 4, "second Shift+Right extends");

    /* Shift+Left extends back over the same ground (the old code moved
     * the selection instead of growing it). */
    textedit_move_horiz(&edit, -1, false, true);
    CHECK(edit.sel_start == 2 && edit.sel_end == 3, "Shift+Left shrinks the tail");
    edit.cursor_col = 5;
    textedit_clear_selection(&edit);
    textedit_move_horiz(&edit, -1, false, true);
    textedit_move_horiz(&edit, -1, false, true);
    CHECK(edit.sel_active && edit.sel_start == 3 && edit.sel_end == 5,
          "Shift+Left twice selects two chars");

    /* Copy, then delete. */
    char clip[TEXTEDIT_LINE_MAX];
    textedit_copy_selection(&edit, clip, sizeof(clip));
    CHECK(strcmp(clip, "lo") == 0, "copy picks the selected range");
    textedit_delete_selection(&edit);
    CHECK(strcmp(edit.lines[0], "hel world") == 0 && edit.cursor_col == 3,
          "delete removes the selection");
    CHECK(!edit.sel_active && edit.dirty, "delete clears the selection and marks the doc");

    /* Plain movement drops the selection. */
    edit.cursor_col = 4;
    textedit_move_horiz(&edit, 1, false, true);
    CHECK(edit.sel_active, "selection started");
    textedit_move_horiz(&edit, 1, false, false);
    CHECK(!edit.sel_active, "plain move drops the selection");
}

static void test_editing(void)
{
    TextEditState edit;
    const char *doc[] = {"hello", "world"};
    set_doc(&edit, doc, 2);

    /* Typing at the line cap stops without eating characters. */
    const char *full[] = {""};
    set_doc(&edit, full, 1);
    for (int i = 0; i < TEXTEDIT_LINE_MAX - 1; i++) {
        if (!textedit_insert_char(&edit, 'a')) break;
    }
    CHECK((int)strlen(edit.lines[0]) == TEXTEDIT_LINE_MAX - 1, "line fills to the cap");
    CHECK(textedit_insert_char(&edit, 'b') == false, "insert at the cap refuses");
    CHECK(edit.lines[0][TEXTEDIT_LINE_MAX - 2] == 'a' && strcmp(edit.status, "Line full") == 0,
          "refused insert changed nothing and said why");

    /* Enter splits at the cursor; the space after the caret moves to
     * the new line with the tail (like macOS). */
    const char *doc2[] = {"hello world"};
    set_doc(&edit, doc2, 1);
    edit.cursor_col = 5;
    CHECK(textedit_newline(&edit), "newline splits");
    CHECK(edit.line_count == 2 && strcmp(edit.lines[0], "hello") == 0 &&
          strcmp(edit.lines[1], " world") == 0 && edit.cursor_line == 1 && edit.cursor_col == 0,
          "split keeps both halves and moves the cursor");

    /* Enter at a line end appends a truly empty line (the old code kept
     * whatever bytes were left in that grid slot). */
    const char *doc3[] = {"a", "b"};
    set_doc(&edit, doc3, 2);
    textedit_delete_line(&edit, 0);           /* leaves "b" behind in slot 1 */
    edit.cursor_line = 0;
    edit.cursor_col = 1;
    CHECK(textedit_newline(&edit), "newline appends");
    CHECK(edit.line_count == 2 && edit.lines[1][0] == '\0',
          "appended line starts empty, not stale");
    textedit_insert_char(&edit, 'x');
    CHECK(strcmp(edit.lines[1], "x") == 0, "typing lands in the fresh line");

    /* Backspace joins two lines that fit. */
    const char *doc4[] = {"hello", "world"};
    set_doc(&edit, doc4, 2);
    edit.cursor_line = 1;
    edit.cursor_col = 0;
    CHECK(textedit_backspace(&edit), "backspace joins");
    CHECK(edit.line_count == 1 && strcmp(edit.lines[0], "helloworld") == 0 &&
          edit.cursor_col == 5, "join keeps every character");

    /* Backspace refuses to join lines that would not fit (the old code
     * silently truncated the tail). */
    char long1[TEXTEDIT_LINE_MAX], long2[TEXTEDIT_LINE_MAX];
    memset(long1, 'a', sizeof(long1) - 1);
    long1[sizeof(long1) - 1] = '\0';
    memset(long2, 'b', 200);
    long2[200] = '\0';
    const char *doc5[] = {long1, long2};
    set_doc(&edit, doc5, 2);
    edit.cursor_line = 1;
    edit.cursor_col = 0;
    CHECK(textedit_backspace(&edit) == false, "join that would not fit is refused");
    CHECK(strcmp(edit.status, "Can't join: line full") == 0 &&
          (int)strlen(edit.lines[0]) == TEXTEDIT_LINE_MAX - 1 && edit.lines[1][0] == 'b',
          "refused join changed nothing and said why");

    /* Backspace inside a line deletes the char left of the cursor. */
    const char *doc6[] = {"abc"};
    set_doc(&edit, doc6, 1);
    edit.cursor_col = 2;
    textedit_backspace(&edit);
    CHECK(strcmp(edit.lines[0], "ac") == 0 && edit.cursor_col == 1, "backspace deletes one char");
}

static void test_counts(void)
{
    TextEditState edit;
    int words, chars;

    textedit_clear(&edit);
    textedit_counts(&edit, &words, &chars);
    CHECK(words == 0 && chars == 0, "empty document counts zero");

    const char *doc1[] = {"hello"};
    set_doc(&edit, doc1, 1);
    textedit_counts(&edit, &words, &chars);
    CHECK(words == 1 && chars == 5, "one word, five characters");

    const char *doc2[] = {"hello  world"};
    set_doc(&edit, doc2, 1);
    textedit_counts(&edit, &words, &chars);
    CHECK(words == 2 && chars == 12, "runs of spaces split words");

    const char *doc3[] = {"one two", "three"};
    set_doc(&edit, doc3, 2);
    textedit_counts(&edit, &words, &chars);
    CHECK(words == 3 && chars == 12, "newlines split words, not counted as characters");

    const char *doc4[] = {"", ""};
    set_doc(&edit, doc4, 2);
    textedit_counts(&edit, &words, &chars);
    CHECK(words == 0 && chars == 0, "empty lines count nothing");

    const char *doc5[] = {"a\tb", "   "};
    set_doc(&edit, doc5, 2);
    textedit_counts(&edit, &words, &chars);
    CHECK(words == 2 && chars == 6, "tabs are whitespace, spaces alone are no word");

    const char *doc6[] = {"don't stop believing"};
    set_doc(&edit, doc6, 1);
    textedit_counts(&edit, &words, &chars);
    CHECK(words == 3 && chars == 20, "punctuation stays in its word");
}

int main(void)
{
    test_wrap_basics();
    test_wrap_worst_case();
    test_cursor_row();
    test_wrap_fuzz();
    test_movement();
    test_selection();
    test_editing();
    test_counts();
    return finish();
}
