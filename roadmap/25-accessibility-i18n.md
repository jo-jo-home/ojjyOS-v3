# 25 — Accessibility and internationalization

Accessibility (`A11Y-*`) and languages/input (`I18N-*`) are system-wide
qualities, built into ojjyKit (UIK-05, UIK-45) and the shell, not added
per app. Today: Reduce Motion, Reduce Transparency and a 24-hour clock
exist; text is US-English only; keyboard input is US layout.

---

## Accessibility

### A11Y-01 — Accessibility service and screen reader ("ojjyReader")
- Phase: P4 · Size: L · Status: todo
- Depends: UIK-05, A11Y-02, WS-04
- Files: services/a11yd/, apps/ojjyReader

**Goal.** Service aggregating apps' accessibility trees; screen reader
with a cursor that moves through elements, reading role/label/value via
TTS, keyboard commands (modifier + arrows, rotor for headings/links/form
controls, read all), focus following, announcements for notifications,
web content support through ojjyWeb's tree, braille later.
**Done when.** A blindfolded test: set up a note in Notes and save it
using only the keyboard and speech (scripted test + Jonas trial).
**Tests.** Automation tests asserting spoken output strings.
**Pitfalls.** Latency between key and speech < 100 ms.
**Refs.** WAI-ARIA (role concepts).

### A11Y-02 — Text-to-speech engine
- Phase: P4 · Size: L · Status: todo
- Depends: AUD-03, I18N-01
- Files: lib/libtts/

**Goal.** Original English TTS: text normalization (numbers, dates,
abbreviations), letter-to-sound rules + pronunciation dictionary (openly
licensed data), prosody, formant or diphone synthesis (own voice data
recorded/generated with permission), speech rate/pitch controls; API for
apps ("Speak selection").
**Done when.** Intelligible speech at 2× rate for the screen reader;
`say` command.
**Tests.** Host tests of normalization; listening tests recorded.
**Pitfalls.** Quality expectations — start intelligible, improve later.
**Refs.** Klatt (1980) formant synthesis paper.

### A11Y-03 — Larger text (dynamic type)
- Phase: P4 · Size: M · Status: todo
- Depends: DS-04, UIK-02
- Files: lib/ojjykit/theme.c

**Goal.** System text size setting (7 steps) scaling all text styles;
layouts adapt (UIK-02); menu bar and Dock respect it.
**Done when.** Every built-in app is usable at the largest size without
clipped text (design tour at max size).
**Tests.** Snapshot tests at each size.
**Pitfalls.** —
**Refs.** —

### A11Y-04 — Zoom (magnifier)
- Phase: P4 · Size: M · Status: todo
- Depends: WS-05
- Files: services/windowserver/zoom.c

**Goal.** Full-screen zoom (follow pointer/focus/text caret), lens zoom,
scroll-gesture zoom with modifier, hover text (large tooltip of the item
under the pointer).
**Done when.** Zoom follows the caret while typing.
**Tests.** UI tests.
**Pitfalls.** Smooth scaling quality.
**Refs.** —

### A11Y-05 — Display accommodations
- Phase: P4 · Size: S · Status: todo
- Depends: WS-13, DS-11
- Files: services/windowserver/color.c

**Goal.** Invert colours (smart: don't invert images), colour filters
(deuteranopia/protanopia/tritanopia/greyscale), increase contrast,
reduce transparency and motion (exist — keep), differentiate without
colour, cursor size/colour.
**Done when.** All options available in Settings > Accessibility.
**Tests.** Pixel tests of filters.
**Pitfalls.** —
**Refs.** —

### A11Y-06 — Pointer control
- Phase: P4 · Size: S · Status: todo
- Depends: WS-11
- Files: services/windowserver/pointer_a11y.c

**Goal.** Shake to locate, mouse keys, dwell click, alternate pointer
actions, sticky drag.
**Done when.** Mouse keys move the pointer with the numeric keypad.
**Tests.** UI tests.
**Pitfalls.** —
**Refs.** —

### A11Y-07 — Keyboard accessibility
- Phase: P4 · Size: S · Status: todo
- Depends: DRV-IN-01, UIK-44
- Files: services/windowserver/input/

**Goal.** Sticky keys, slow keys, full keyboard access (navigate every
control incl. menu bar and Dock), keyboard shortcuts overlay (hold Cmd
shows shortcuts like Windows/ChromeOS help).
**Done when.** Options work system-wide.
**Tests.** UI tests.
**Pitfalls.** —
**Refs.** —

### A11Y-08 — On-screen keyboard
- Phase: P4 · Size: M · Status: todo
- Depends: UIK-01, I18N-02
- Files: services/osk/

**Goal.** Accessibility keyboard panel (all layouts, modifiers, dwell
typing) and a touch keyboard that appears for text fields on
touchscreens.
**Done when.** Typing a sentence with only the pointer works.
**Tests.** UI tests.
**Pitfalls.** —
**Refs.** —

### A11Y-09 — Visual alerts and audio accommodations
- Phase: P4 · Size: S · Status: todo
- Depends: AUD-02
- Files: services/audiod/, services/windowserver/

**Goal.** Flash the screen for alerts, mono audio, balance, captions
preferences.
**Done when.** Options apply.
**Tests.** Live tests.
**Pitfalls.** —
**Refs.** —

### A11Y-10 — Caption and subtitle styling
- Phase: P6 · Size: S · Status: todo
- Depends: MED-25
- Files: lib/libmedia/subtitles/style.c

**Goal.** User-chosen caption style (font, size, colour, background,
outline) applied to all players and ojjyWeb video.
**Done when.** Style applies in Videos and ojjyWeb.
**Tests.** Snapshot tests.
**Pitfalls.** —
**Refs.** —

### A11Y-11 — Voice control and dictation (stretch)
- Phase: P10 · Size: XL · Status: todo
- Depends: A11Y-01, AUD-03
- Files: lib/libspeechrec/

**Goal.** Offline speech recognition for dictation and voice commands
("click Save", "open Finder") — requires an original acoustic model
pipeline; split and research before work.
**Done when.** Research note and decision.
**Tests.** —
**Pitfalls.** Very large effort.
**Refs.** —

### A11Y-12 — Accessibility audit tooling
- Phase: P4 · Size: S · Status: todo
- Depends: UIK-05, UIK-50
- Files: tests/ui/a11y_audit.c

**Goal.** Automated checks in UI tests: unlabeled controls, contrast
failures, focus order issues, hit targets too small; runs in the suite.
**Done when.** Audit runs for every app in the design tour.
**Tests.** Itself.
**Pitfalls.** —
**Refs.** WCAG 2.2.

---

## Internationalization

### I18N-01 — Locale data (CLDR subset)
- Phase: P4 · Size: M · Status: todo
- Depends: USR-46
- Files: lib/libojjy/locale/, tools/gencldr.py

**Goal.** Generated data for ~30 locales: language/region names, date
and time patterns, number formats, currencies, units, plural rules,
first day of week, calendars (Gregorian first), collation tailorings.
**Done when.** Formatting APIs (I18N-04) produce CLDR-correct output for
sample locales.
**Tests.** Host tests against CLDR test data.
**Pitfalls.** Size: compress and load per locale.
**Refs.** Unicode CLDR (licence: Unicode, permissive).

### I18N-02 — Keyboard layouts
- Phase: P2 · Size: M · Status: todo
- Depends: DRV-IN-01
- Files: lib/libojjy/keyboard/, /System/Library/Keyboard Layouts/

**Goal.** Layout files (key → character tables per modifier state, dead
keys) for US, US International, UK, German, French, Spanish, Italian,
Korean (2-set, for the IME), Japanese (for the IME), Dvorak, Colemak;
Option-key characters like macOS; Windows/PC keyboard variants (Alt/Win
mapping); layout switcher shortcut.
**Done when.** Typing "ä", "é", "ñ" works with the right layouts; layout
choice per user.
**Tests.** Host tests of layout tables and dead keys.
**Pitfalls.** Physical vs logical key positions (ISO vs ANSI).
**Refs.** —

### I18N-03 — Text input system (marked text, IME framework)
- Phase: P2 · Size: M · Status: todo
- Depends: I18N-02, UIK-01
- Files: lib/ojjykit/textinput/, services/inputmethodd (SVC-13)

**Goal.** Protocol between text fields and input methods: marked
(composing) text with underlines, selection within composition,
candidate lists positioned at the caret, commit, cancel; dead-key
composition through the same path.
**Done when.** A test IME (uppercasing composer) works in all text fields.
**Tests.** UI automation tests.
**Pitfalls.** —
**Refs.** —

### I18N-04 — Formatting APIs
- Phase: P4 · Size: S · Status: todo
- Depends: I18N-01
- Files: lib/libojjy/format/

**Goal.** Dates, times, relative dates ("yesterday"), numbers, currencies,
percentages, units with conversion, lists ("A, B, and C"), byte counts,
durations — locale-aware, used by every app instead of ad-hoc formats.
**Done when.** Finder sizes/dates and menu bar clock use them.
**Tests.** Host tests.
**Pitfalls.** —
**Refs.** CLDR.

### I18N-05 — Right-to-left layout
- Phase: P6 · Size: M · Status: todo
- Depends: TXT-06, UIK-02
- Files: lib/ojjykit/layout/rtl.c

**Goal.** Mirrored layouts for RTL languages (Arabic, Hebrew):
navigation order, symbols that should flip, text alignment natural.
**Done when.** Pseudo-RTL mode shows correct mirroring in every app.
**Tests.** Snapshot tests in RTL.
**Pitfalls.** Don't mirror media controls/clocks.
**Refs.** —

### I18N-06 — Collation and search folding
- Phase: P4 · Size: S · Status: todo
- Depends: I18N-01, TXT-13
- Files: lib/libojjy/collate.c

**Goal.** Unicode Collation Algorithm with locale tailorings for sorting
(Finder, Contacts), case/diacritic-insensitive search matching
(Spotlight, Find).
**Done when.** Passes UCA conformance tests.
**Tests.** Unicode conformance files.
**Pitfalls.** —
**Refs.** UTS #10.

### I18N-07 — Spell checking and text replacement
- Phase: P4 · Size: M · Status: todo
- Depends: UIK-18
- Files: services/spelld/

**Goal.** Original spell checker using Hunspell-format dictionaries
(openly licensed word lists as data), suggestions, learn/ignore words,
autocorrect option, text replacements (SET-27), grammar later.
**Done when.** Misspellings underlined in TextEdit/Notes/Mail with
suggestions.
**Tests.** Host tests.
**Pitfalls.** Dictionary licences recorded.
**Refs.** Hunspell dictionary format documentation.

### I18N-08 — Localization workflow
- Phase: P4 · Size: S · Status: todo
- Depends: UIK-45
- Files: tools/extract-strings.py, docs/l10n.md

**Goal.** Extract UI strings from sources into per-bundle string tables,
detect missing/unused keys, pseudo-localization (accented/longer strings)
for testing, translator guide.
**Done when.** Extraction runs in the suite and reports coverage.
**Tests.** Script tests.
**Pitfalls.** —
**Refs.** —

### I18N-09 — Unicode file names
- Phase: P3 · Size: S · Status: todo
- Depends: FS-13, TXT-13
- Files: kernel/fs/vfs/names.c

**Goal.** UTF-8 file names everywhere; normalization-insensitive lookup
on ojjyFS (store as given, compare normalized), conversions for FAT
(UTF-16) and SMB, emoji names displayed correctly in Finder.
**Done when.** Files named "Café" created on macOS (NFD) open by typing
the NFC name.
**Tests.** Host + live tests.
**Pitfalls.** —
**Refs.** UAX #15.

### I18N-10 — First complete localization
- Phase: P6 · Size: M · Status: todo
- Depends: I18N-08, UIK-45, SET-05
- Files: */Resources/<lang>.lproj/

**Goal.** Translate the whole OS UI into a second language chosen by
Jonas (and keep it complete in later releases).
**Done when.** Switching the system language shows no English strings in
built-in apps.
**Tests.** Coverage report from I18N-08.
**Pitfalls.** —
**Refs.** —

### I18N-11 — Korean input method
- Phase: P6 · Size: S · Status: todo
- Depends: I18N-03
- Files: services/inputmethodd/korean/

**Goal.** Hangul 2-set (Dubeolsik) composition with syllable assembly and
hanja conversion (optional).
**Done when.** Typing Korean text works in all apps.
**Tests.** Host composition tests.
**Pitfalls.** —
**Refs.** Unicode Hangul syllable composition.

### I18N-12 — Japanese input method
- Phase: P6 · Size: M · Status: todo
- Depends: I18N-03
- Files: services/inputmethodd/japanese/

**Goal.** Romaji → kana, kana → kanji conversion with an openly
licensed dictionary, candidate window, learning.
**Done when.** Typing common Japanese sentences works.
**Tests.** Host conversion tests.
**Pitfalls.** Dictionary licence.
**Refs.** —

### I18N-13 — Chinese Pinyin input method
- Phase: P6 · Size: M · Status: todo
- Depends: I18N-03
- Files: services/inputmethodd/pinyin/

**Goal.** Pinyin to Simplified Chinese with frequency-ranked candidates
from openly licensed data.
**Done when.** Typing common phrases works.
**Tests.** Host tests.
**Pitfalls.** —
**Refs.** —
