# Command-line startup implementation checkpoint

Issue #276 remains open at this checkpoint. The remaining parser-error policy
requires the user's decision; this checkpoint does not unblock #277.

Ordinary single-file paths and the valid `e`, `s`, `g`, `G`, `ro`, and `?`
options reach the real application's document loader. Source decoding is
independent of the internal current-encoding restoration argument. Native ANSI
and OEM identities remain distinct from their codecs. Internal relaunch callers
retain their existing defaults and lifecycle.

`tests/startup_g_reference.json` and `tests/startup_display_reference.json`
contain 36 independent original-process observations. Their source is Notepad4
revision `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, executable SHA-256
`1425c9a4b5ab2ea1fa3ba088f38dd987b763b7b8feff23dde6243f7855e86a78`.
The capture entry point is `tests/reference_capture/capture_startup_options.py`.
Tests compare caret bytes, empty selections, viewport offsets and saved bytes;
they do not call a private parser.

The startup viewport regression found that Wayland initially reported an editor
height of 389 pixels before configuring the final 300-pixel viewport. A distant
caret could remain below the visible area. Initial jumps now retain caret
visibility through native size events until the first paint. Existing interactive
navigation and subsequent resize behavior retain their previous paths.

Before merging the integration tip, all nine new Windows cases passed, all ten
private X11 and Wayland cases passed, and the six existing Windows New Window,
current-file child and Go To/line-number regressions passed. Private X11 kept
the existing maximized-window skip. These results cover the valid-option
checkpoint; combined verification still follows integration.

## Pending parser-error decision

The original parser falls back to a single pathname when a known option lacks
an operand or `g`/`G` has no numeric prefix. That can produce a prompt for a
pathname such as a drive-root `e` file. The proposed explicit localized error
and unsuccessful exit would change that original behavior. It has not been
approved or implemented in this checkpoint. No test creates such a root path.

The existing parse-failure paths remain provisional for these cases. They must
be resolved and tested before #276 can be reported complete. No original test
assertion, deadline, parallelism setting or quality gate was removed or weakened.
