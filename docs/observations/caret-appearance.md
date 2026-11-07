# Caret appearance and blink preferences

Implements [issue #154](https://github.com/1298345095/wxnotepad/issues/154) against
Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.

## Original behavior

- `Notepad4.cpp::LoadSettings` interprets `CaretStyle` as decimal digits: hundreds
  enables drawing after a forward selection, tens enables overstrike blocks,
  and the final digit selects insertion block (0) or line width (1–3). Signed
  remainder and final-digit clamping are preserved. Save writes the canonical
  value and omits the default 1.
- `CaretBlinkPeriod` preserves the imported integer. Every negative value uses
  the system period when applied; zero disables blinking. No Blink toggles zero
  to -1 and every other value to zero. Save omits -1.
- `Styles.cpp::Style_UpdateCaret` gives the system accessibility width priority
  when larger than the requested line width. Otherwise only default width 1
  scales with DPI and zoom. Explicit widths 2 and 3 remain pixels. The pinned
  core clamps the applied width to 0–20.
- The original selection menu is checked when the block is drawn inside the
  selection. Its unchecked state sets the core's `BlockAfter` flag.
- `EditView::DrawCarets` retains Unicode code-point, variable-width, selection,
  overstrike-bar, document-boundary and drag-marker geometry. The numeric original
  GDI capture includes the original partial-cell behavior for a combining-mark
  sequence; the fixture observes the base character's endpoint at byte 10.

At the captured 144 DPI and system width 1, default-width results at zoom
50/100/150/200/500 are 1/2/2/3/8 pixels. Explicit widths remain 2 and 3 pixels.
At 96 DPI the corresponding default widths are 1/1/2/2/5.

## Windows drawing and IME

The pinned core draws the visual caret. A separate native caret uses the
original ScintillaWin zero bitmap, preserving the native insertion position and
existing explicit IMM composition/candidate/font geometry. Native focus owns
that bitmap's lifetime. Windowed IME start drops the core's visual caret;
composition end/cancel restores it with the selected style and period.

Preferences are applied through one editor capability. Startup settings,
menus, zoom and DPI use that capability. Language changes, open/reopen, New,
focus and temporary composition retain its preference state. The existing
native font, point size and text rendering settings remain in place.

## GTK cursor-cluster measurement

The X11 and Wayland regression found a measurement adapter error after a
combining sequence. For the original 15-byte fixture `Wi中😀é \r\n`, byte 12
is the trailing space and byte 13 is the line end. Before correction, queried
X at bytes 12 and 13 was 112 for both, while native paint covered 102–112.
The pinned `EditView::DrawBlockCaret` correctly expands a zero-advance caret
backward over shared horizontal space; the incorrect input advances made the
space appear to belong to the preceding combining cluster.

wxGTK 3.3.3's Cairo `GetPartialTextExtents` emits widths for Pango cursor
boundaries, then pads the array to the character count. Interpreting that array
as one independent entry per code point shifts every following advance when a
cursor cluster contains several code points. The GTK adapter now uses the same
Pango `is_cursor_position` boundaries to expand these measured cluster widths
before mapping them to UTF-8 bytes. It retains the existing DC, font, DPI and
measured widths. ASCII and Windows retain their previous mapping, and the work
is linear in the input length.

The regression keeps every original cell-paint assertion and independently
verifies the raw UTF-8 bytes, line end 13, UTF-8 code page and LTR layout. It also
requires positive advance for the trailing space: corrected queried X is 102
at byte 12 and 112 at byte 13, matching native paint.

Reference: wxWidgets `v3.3.3` `src/generic/graphicc.cpp`,
`wxCairoContext::GetPartialTextExtents`; pinned Notepad4
`EditView.cxx::DrawBlockCaret`. Diagnostic evidence is archived as
`154/linux-caret-diagnostic.log` alongside the ticket's execution evidence.
The premise assertions passed before the correction and the trailing-space
advance failed (`154/linux-cluster-red.log`). With the GTK mapping corrected,
the full caret appearance fixture passed on private X11 and Wayland displays
(`154/linux-cluster-green.log`) with all cell assertions retained.

## Verification

- `original_caret_appearance`: native menu activation and checks, queried core
  state, actual widget pixel spans, controlled blink phases, insertion and
  overstrike selection placement, Unicode, line/document boundaries, focus,
  windowed IME start/end, language changes, DPI/zoom, and real file
  open/reopen/New/save/Undo/Redo with independently read bytes.
- `original_caret_preference`: independent application processes, original
  captured packed-value fixtures, custom/system/zero periods, seed import,
  explicit and exit saves, and preserved unrelated INI fields.
- `tools/capture_caret_fixtures.py`: owned original Windows process, native
  command/query paths, portable INI saves and numeric original GDI paint probes.
  Produces `tests/caret_reference.tsv` and a detailed external JSON capture.
- `WXNOTEPAD_CARET_DPI96=1` enables the complementary Windows 96-DPI owned-window
  fixture without changing monitor scaling.

Ticket execution logs and original captures are archived under
`E:/NewNotepad4/wxnotepad-evidence/editor-navigation-controls-2026-11-07/154/`.
The integration run also covers Windows real Microsoft Pinyin composition and
the shared Windows/X11/Wayland regression suites. Linux human IME retains the
existing project deferral.
