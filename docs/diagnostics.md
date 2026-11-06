# Native diagnostics

Configure with `-DWXNOTEPAD_ENABLE_DIAGNOSTICS=ON` to include native tracing in
an optimized development build. The option defaults to `OFF`; release packaging
uses `OFF`. Keep the source available to produce a diagnostic build later.

Tracing starts disabled. Enable it for one process with both environment
variables:

```text
WXNOTEPAD_DIAGNOSTICS=1
WXNOTEPAD_DIAGNOSTIC_LOG=/absolute/path/native.log
```

Use `stderr` as the destination to write to an existing captured stderr stream.
Set the variables before starting the application or native test. A disabled
run creates no log and skips record arguments and state queries. A compiled-out
build contains neither the writer nor the diagnostic call paths. Destination
errors go to stderr through the C runtime, preserving GUI-log suppression.

The UTF-8 tab-separated records contain monotonic microseconds since logger
initialization, process and native thread IDs, per-process sequence number,
event name and native-gesture identity. The header identifies the application
version/configuration, backend, compiler, wxWidgets and pinned core revision;
GTK builds also record runtime GTK/GLib versions. Pointer identities are scoped
to their process and lifetime.

Records describe input/focus delivery, command IDs, selection endpoints and
lengths, clipboard formats/ownership/transfer lengths, observer request and
acknowledgement, and close confirmation/choice/save/veto/deferred destruction.
Document, clipboard and IME text, paths and dialog labels are excluded. Each
completed record is flushed before returning to the observed boundary. An
abnormal process exit leaves the completed prefix available to the existing
test and crash-evidence collection.

Native test logs are retained under `Testing/Temporary/diagnostics/` alongside
the existing test evidence. Enabled and disabled runs are separate processes;
private X11/Wayland driver records use the same gesture identity as the
receiving application/editor, with a separate `.driver` log. Compare
`driver.request`, `driver.ack`, `application.key-down`/`key-up` and
`editor.key-down`/`key-up` to locate a missing delivery. A driver acknowledgement
records acceptance of the request before input injection, matching the existing
protocol. The receiver still verifies its native release and behavior deadline.

Tracing adds observations to existing native behavior checks. Preserve their
assertions, deadlines, process exit codes and core/stack evidence. An unresolved
crash or unreproduced native failure keeps its existing status; the records help
diagnose a later recurrence.
