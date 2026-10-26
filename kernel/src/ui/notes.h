/*
 * ojjyOS v3 - Notes library (the part of the Notes app that does not draw)
 *
 * Every note is a plain-text file in ~/Notes. This module lists that
 * folder in title order (RAMFS keeps no timestamps, so there is no
 * "recently edited" order yet), builds the sidebar preview from a note's
 * first non-blank line, picks names for new notes, creates the folder on
 * first use (copying the old single ~/Documents/Notes.txt in, once) and
 * decides when an edit is due to be autosaved. It only talks to the VFS,
 * so tests/test_notes.c runs it against a real RAMFS.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_NOTES_H
#define _OJJY_NOTES_H

#include "types.h"

#define NOTES_MAX           48      /* /Users has 256 RAMFS nodes in total */
#define NOTES_NAME_MAX      64      /* file name incl. ".txt" and the NUL */
#define NOTES_PREVIEW_MAX   48
#define NOTES_DIR_MAX       128
#define NOTES_AUTOSAVE_MS   1000    /* quiet time after the last edit */

typedef struct {
    char name[NOTES_NAME_MAX];          /* "Groceries.txt" */
    char preview[NOTES_PREVIEW_MAX];    /* first non-blank line, printable ASCII */
} NoteEntry;

typedef struct {
    char dir[NOTES_DIR_MAX];            /* absolute path of ~/Notes */
    NoteEntry items[NOTES_MAX];
    int count;
    int selected;                       /* index into items, -1 = none */
    int scroll;                         /* first visible sidebar row */
    bool more;                          /* the folder holds more notes than NOTES_MAX */
    uint64_t last_edit_ms;              /* when the open note last changed */
} NotesLibrary;

/* A note is a regular "*.txt" file (any case) that is not hidden and whose
 * name fits NOTES_NAME_MAX. */
bool notes_is_note_name(const char *name);
/* "Groceries.txt" -> "Groceries" (always NUL-terminated, may be cut). */
void notes_title(const char *name, char *out, size_t size);
/* First non-blank line of `text`, trimmed; tabs become spaces, other
 * control and non-ASCII bytes are dropped. "" when there is none. */
void notes_preview(const char *text, size_t len, char *out, size_t size);
/* True once a dirty note has been left alone for NOTES_AUTOSAVE_MS. */
bool notes_autosave_due(bool dirty, uint64_t last_edit_ms, uint64_t now_ms);

/* Points the library at <home>/Notes and lists it. Creates the folder when
 * it is missing and then copies <home>/Documents/Notes.txt into it (the old
 * Notes app kept everything in that one file; it is left in place).
 * Returns 1 when that copy happened, 0 when the folder could be listed,
 * -1 when there is no usable folder (a file is in the way, or no space). */
int notes_open(NotesLibrary *lib, const char *home);
/* Re-reads the folder; the selection follows its note by name (-1 when
 * that note is gone). Returns the number of notes listed or -1. */
int notes_scan(NotesLibrary *lib);
/* Index of the note called `name` (case-insensitive), or -1. */
int notes_find(const NotesLibrary *lib, const char *name);
/* Full path of note `index`; -1 for a bad index or a path too long. */
int notes_path(const NotesLibrary *lib, int index, char *out, size_t size);
/* Picks the first free name "New Note.txt", "New Note 2.txt", ... */
int notes_new_name(const NotesLibrary *lib, char *out, size_t size);
/* Creates an empty note with a new name, re-lists the folder and selects
 * it. Returns its index, or -1 (library full, or the file can't be made). */
int notes_create(NotesLibrary *lib);
/* Re-reads the preview line of note `index` from disk (after a save). */
void notes_refresh(NotesLibrary *lib, int index);

#endif /* _OJJY_NOTES_H */
