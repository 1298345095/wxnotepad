# Native color CallTips

The reference is Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.
`Styles.cpp::EditShowCallTip` and `EditClickCallTip` supply the scan window,
prefix and replacement rules. The editor uses the pinned core's color channel
conversion helpers from `GraphicUtils.h` and its CallTip drawing state. The
nonactivating wx popup and click notification follow the wxWidgets 3.3.3
`wxSTCCallTip` bridge. Painting goes through the app's native GDI, DirectWrite
or GTK surface; no cached one-scale bitmap or wxSTC library is needed.

A pinned Windows reference run at 144 DPI and DirectWrite exercised 27 dwell
positions in each mode and seven native chooser outcomes in each enabled mode.
It recorded the actual chooser's initial RGB, original selection, document bytes,
modified state and Undo/Redo results. The private desktop does not produce a
natural mouse dwell, so its driver queues the original window's native motion
and dwell timer messages; the reported delay comes from the editor's public
setting. The production regression waits for the actual dwell timer.

The original leaves a color preview open when the mouse departs, text is typed,
or Off is selected while it is open. Escape and another dwell outside the color
range close it. Existing `0x` and `0X` replacement positions follow the original,
including the following delimiter in their replacement range. Acceptance of a
changed color preserves the alpha byte, expands short forms, and writes six
digits when alpha is zero.

The reference's color dwell also dismissed an active completion list. The
approved behavior for this port gives completion and inline IME priority: color
preview waits for a subsequent dwell after they close. Escape still cancels
completion before a CallTip.

Run the independent reference probe on Windows with the pinned executable:

```text
python -X utf8 .github/scripts/ci.py probe --report-dir F:/evidence/calltip-reference-probe --timeout 140 --runs 1 -- python -X utf8 tests/reference_capture/capture_color_calltips.py --original F:/reference/Notepad4.exe --output F:/evidence/calltip-reference
```

Run the native `original_color_calltip` regression through `ci.py test` on
Windows, X11 and Wayland. Windows also registers `native_directwrite_color_calltip`.
Set `WXNOTEPAD_CALLTIP_CAPTURE_DIR` to an absolute output directory to retain
PNG copies of the inline and bottom-edge popup paints at device resolution.
For X11 the additional `original_color_calltip_scale2` test sets `GDK_SCALE=2`.
For a 200% Wayland capture, use a private Weston output configured with `--scale=2`;
the compositor's output scale controls the reported GTK DPI there.
