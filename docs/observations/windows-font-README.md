# Windows text clarity and font scale

Base revision: `33cc5b9`; validated on 2026-10-30 (America/Los_Angeles).

## Cause and fix

The executable's default MSVC manifest did not declare DPI awareness. On the
150% display, the actual frame and editor reported 96 DPI and DPI-unaware mode.
Windows enlarged the entire window as a bitmap. The user confirmed that menus,
English and Chinese text all looked blurry.

Windows GUI targets now embed wxWidgets' native resources and Per-Monitor V2
manifest through `src/windows.rc`. MSVC's default generated manifest is disabled
so it does not replace that resource. The actual frame and editor now report
Per-Monitor V2 awareness and **144 DPI** on the same display.

Removing bitmap scaling also exposed a separate font-size mismatch: the editor's
offscreen bitmap still had a scale factor of 1. In wxWidgets 3.3.3,
`wxMemoryDCImpl::SetFont` adjusts point fonts using the selected bitmap's scale
factor. Buffered text was consequently rendered at 96-DPI font size. The user
confirmed that text was clear but much smaller after the manifest-only change.

Offscreen bitmaps now inherit their source DC's content scale factor. This keeps
wxWidgets responsible for converting the unchanged 12-point font to pixels.
No font enlargement multiplier or separate text-rendering backend is added.
The editor also invalidates its cached font metrics on `wxEVT_DPI_CHANGED`.

Measured with the same `Hg 0123456789` sample at 144 DPI:

| Rendering path | Before bitmap scale fix | After |
| --- | ---: | ---: |
| Native window text width | 169 px | 169 px |
| Direct editor surface width | 169 px | 169 px |
| Buffered editor surface width | 117 px | 169 px |
| Direct / buffered line height | 28 / 19 px | 28 / 28 px |

The user confirmed that the final sample's menus, English and Chinese text are
clear and its editor font size is appropriate.

## Regression checks

- `windows_dpi_awareness` launches the actual application, checks both native
  window contexts, and closes the blank document normally. It failed before the
  manifest change and passes with 144-DPI Per-Monitor V2 windows.
- `native_font_metrics` compares the real platform adapter's direct and buffered
  text measurements with a native wx client DC. It failed with 169/117-pixel
  widths and passes with matching 169-pixel widths.
- Windows input/Undo, repaint and IMM geometry also pass: **5/5** targeted checks.
- Native Linux release build passes; isolated X11 and Wayland each pass **3/3**
  font, repaint and input checks. The bitmap metadata change is Windows-specific.

The process check also reproduced a shutdown use-after-free in the opt-in
`--observe` mode. The debugger stack showed a wx timer reading the destroyed
frame. Observation now stops when that frame is destroyed, and queued observation
callbacks check its lifetime. The same process regression verifies normal exit.

Mixed-DPI monitor movement was not part of the human validation in this run.

## Sources and evidence

The wxWidgets 3.3 manifest and high-DPI documentation describe the required
application manifest. The local wxWidgets 3.3.3 source at
`E:/NewNotepad4/wx-v3.3.3` was inspected through its CodeGraph index, particularly
`src/msw/dcmemory.cpp` (`GetPPI`, `SetFont`, and bitmap content scale handling).
The wxWidgets source was not modified.

Original/final manifests, runtime binaries, per-window DPI records, failing and
passing test logs, debugger output, reproduction scripts, source patch and
SHA-256 checksums are preserved separately from earlier evidence:

```text
E:/NewNotepad4/wxnotepad-evidence/windows-font-33cc5b9
```
