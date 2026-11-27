# Command-line startup evidence

Ordinary single-file paths and `e`, `s`, `g`, `G`, `ro`, and `?` options use the
shared document loader and navigation entry points. Source decoding remains
independent of internal current-encoding restoration. Internal relaunch callers
retain their existing defaults and lifecycle.

`tests/startup_g_reference.json` and `tests/startup_display_reference.json`
contain 36 independent original-process observations. The source is Notepad4
revision `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, executable SHA-256
`1425c9a4b5ab2ea1fa3ba088f38dd987b763b7b8feff23dde6243f7855e86a78`.
The capture entry point is `tests/reference_capture/capture_startup_options.py`.
Tests compare caret bytes, empty selections, viewport offsets and saved bytes.

`tests/startup_files_reference.json` records another 18 original processes for
the delivered source aliases, native ANSI/OEM, read-only mode and missing-file
Yes/No/Cancel. Its entry point is
`tests/reference_capture/capture_startup_files.py`. On the original CP936 host,
ASCII `/e ANSI` adopts explicit GBK, while GBK bytes and an invalid lead byte
retain native ANSI identity and unchanged saved bytes. A separate six-process
probe confirmed that selecting JSON does not change this rule. Public ANSI
source loading shares the existing native-UTF8/legacy recode decision with
Quick Reload; Quick Reload and internal current-format contracts are unchanged.

The startup viewport regression found that Wayland initially reported an editor
height of 389 pixels before configuring the final 300-pixel viewport. A distant
caret could remain below the visible area. Initial jumps now retain caret
visibility through native size events until the first paint. Interactive
navigation and subsequent resizing keep their existing behavior.

## Approved original defect fix

The original parser reinterprets missing `e`, `s`, `g` and `G` operands as a
pathname. On Windows a slash prefix can produce a drive-root pathname. The user
explicitly approved correcting this defect: a missing operand now shows a
localized error and exits nonzero without opening or creating a file. Real
packaged-process tests cover every supported prefix in English, Simplified
Chinese and Traditional Chinese, check the actual dialog and unsuccessful exit,
and verify no document state or file is created. No root-path test file is used.

Nonnumeric `g`/`G` operands retain the original remaining-pathname fallback;
tests open owned relative files containing the option prefix. Decimal-prefix
parsing and all valid position rules remain unchanged. No existing assertion,
timeout, parallelism setting or quality gate was removed or weakened.

## Validation

The final ticket matrix includes ordinary paths, all ten delivered codec
aliases, independent original positions, read-only and visible distant caret,
three-language help and errors, missing-file choices, source/current/native
identities and installed Linux resources. Existing New Window, current-file
child, Go To and line-number regressions run alongside these cases. Windows
passed all 16 targeted cases; Wayland passed all 17; X11 passed 16 with the
existing maximized-window skip. Native clang-tidy passed the canonical blocking
checks for all seven changed translation units. Canonical formatting, Python
syntax and localization extraction/coverage checks passed. Full logs are
retained in the external implementation evidence for #276; the parent run
performs the combined full regression suite after integration.
