# Selected paths, links and containing folders

The reference is Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`:
`EditGetStringAroundCaret`, `EditOpenSelectionCheckFile`, `EditOpenSelection`,
`Style_CanOpenFile`, and `OpenContainingFolder`. Their notices remain in
`licenses/Notepad4.txt`.

A selection stops at its first CR, LF or tab. Without a selection, the original
regular-expression delimiters, hotspot span and Markdown URL extraction apply.
A filename containing spaces needs an explicit selection or a hotspot. Leading
and trailing original punctuation is trimmed; unrecognized targets are a no-op.
Local anchors search `name=` before `id=` and select the complete matching
attribute. The isolated original Windows probe confirms the selected and
caret-local `#target` cases both select bytes 33–46 of the frozen fixture.

The raw filesystem name is checked before interpreting a location suffix. A
relative name already present in the process working directory keeps that
original preference; otherwise the current document directory is tried. Leading
Windows `%VARIABLE%`, or Linux `$VARIABLE`/`~`, use native filesystem notation.
URI percent escapes are retained. Colon and parenthesized line/column suffixes
are committed only if the resulting filesystem target exists. Existing full
names and URI ports retain their original classification.
Existing names and location-suffixed files are resolved before stripping
punctuation from a possible URI scheme. This preserves selected relative names
such as `目标, selected file.txt:2:4` instead of truncating them to `file.txt:2:4`.

Windows checks raw candidates with `GetFileAttributesW`, as the original does,
and uses native `GetFullPathNameW` only after finding an existing target.
wxWidgets 3.3.3 `GetFullPath` normalizes DOS volumes: passing an unclassified relative `filename:line:column`
can interpret `filename` as a volume and stall. A standalone native SDK probe
reproduced this ordering problem. Document-relative combinations and native
variable expansion therefore stay as strings until existence is established.

Recognized text filenames reuse the delivered syntax catalog. Extensionless and
dot filenames, CGI/FCGI and targets carrying a valid location retain the original
editor-opening rules. They start a separate process with ordinary
`--startup-file` and one-based `--startup-caret line,column` arguments. Existing
startup loading and the original core's `FindColumn` handle tabs and Unicode.
The source document, selection, dirty state and undo/redo history are retained.

Windows containing-folder actions request the item through
`SHOpenFolderAndSelectItems`. Linux uses
`org.freedesktop.FileManager1.ShowItems`; its actual desktop controls whether
item selection is supported. If the request is unavailable or rejected, the
parent directory opens through its native association. Directory targets use
the native file manager directly. Linux window placement belongs to its
compositor.

Hosted Windows Server controls exposed a cold-view delivery problem: both the
verbatim pinned helper and documented single-item API returned without a visible
selection, and a direct Explorer `/select,"path"` startup also lost selection.
The same single native selection request succeeds after the parent view is
ready. Windows therefore opens the parent first and observes its native
ReadyState/Busy/folder identity using a GUI-STA timer before sending that one
selection request. There is no selection retry, background worker or blocking
readiness loop. Separate explicit requests remain pending independently.
The frame owns the timer and COM state; closing it cancels pending requests and
queued failure reports. A successful asynchronous return means accepted, while
later failure retains the native association fallback and surviving-owner error.

`original_open_targets` exercises native menu commands, independently loaded
child files and positions, controlled local desktop handlers, containing-folder
requests, no-op shapes and failed executable startup. Its Linux file-manager
peer runs on the private session bus and also rejects a request to exercise the
association fallback. Windows registers a fresh per-run protocol and unsupported
extension for the owned recorder, checks that both names were newly created,
and removes those exact keys after the assertions. A process-local Classes
override is insufficient for the actual shell dispatcher: independent native
probes resolve its command through `AssocQueryString` but the real shell still
reports file not found. Existing protocols and protected `UserChoice` keys are
untouched by the ordinary host test. Explorer observations require a newly created HWND at the exact
owned temporary folder and inspect the selected item's full path before closing
only that window. Sequential cases wait for each HWND to finish closing before
the next request. The original four cases remain, with additional checks for a
Unicode/comma/space target, two requests queued together while callbacks remain
responsive, and owner destruction before a pending selection. The cancellation
case observes no selected item while the other frame's native loop continues.
The ordinary and diagnostic cases retain their original acknowledgement and
observation deadlines.

Both Linux backends record the standard explicit URL, protocol-relative, www
and email cases through their private desktop handlers. Windows exercises the
same common extraction path and native shell boundary with its fresh protocol,
including ports, percent escapes, parentheses and Markdown, plus the fresh
unsupported file extension. The explicit Windows `--desktop-standards ROOT`
fixture mode also runs every standard case when the three native protocol
commands already equal the exact owned recorder command; it refuses personal
handlers. This mode supports the separately authorized Windows VM supplement.

`original_open_target_startup` bundles the real application and checks English,
Simplified Chinese and Traditional Chinese captions, menu availability, a real
independent production process at line 2/column 6, independently saved bytes and
unchanged source state. Neither fixture uses a personal browser/mail association,
a network request or clipboard publication.
