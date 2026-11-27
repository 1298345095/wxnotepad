# Opening files from the command line

Run `wxnotepad [options] [file]`. A relative file path is resolved from the
caller's working directory. Quote paths containing spaces. File paths can contain
Unicode characters, including supplementary characters.

Windows accepts `/e`, `-e` and `--e` (and the corresponding prefixes for the other
options below). Linux accepts `-e` and `--e`; a leading `/` always belongs to an
absolute path. A standalone `-` or `--` ends option processing: the remaining
arguments form one file path. For example:

```text
wxnotepad "notes 中文.txt"
wxnotepad --e windows-1252 --s json "data.txt"
wxnotepad --g 12,5 --ro "source.cpp"
wxnotepad -- --leading-name.txt
wxnotepad -e windows-1252 -s json -ro -g 2,2 + "first.txt" "second 中文😀.txt"
```

| Option | Meaning |
| --- | --- |
| `e encoding` | Source encoding used to interpret existing file bytes. Original aliases ignore ASCII case, hyphens and underscores: for example `utf-8`, `utf-16le`, `utf-16be`, `windows-1252`, `cp437`, `gb2312`, `gb18030`, `big5`, `ansi` and `oem`. Unsupported encodings produce an error. Automatic detection remains available by omitting this option. |
| `s scheme` | Select a delivered syntax scheme by its configured extension, for example `cpp`, `py` or `json`. Original scheme-name prefixes of at least three characters are also accepted. An unmatched scheme uses normal file detection. |
| `g line[,character[,bytes]]` | Jump to a one-based line and character. An odd third value counts bytes instead of characters; a byte offset inside a Unicode character resolves to its beginning. |
| `G line[,column[,tab-width]]` | Jump using one-based display columns. The optional tab width affects this jump without changing the document's tab settings. |
| `ro` | Enable the editor's read-only mode. Disk permissions remain unchanged. |
| `o` | Request Always On Top for this launch without changing the saved preference. |
| `o0`, `o-`, `oo` | Disable Always On Top for this launch without changing the saved preference. |
| `?` | Show help in the configured interface language. |

`g` and `G` are case sensitive. Zero means the first line; a negative line or a
line beyond the document jumps to the end. A negative column counts characters
without expanding tabs for either option. Omitted columns start at the beginning
of the selected line. Positions beyond a line stop at its end. The cursor and
selection remain empty at the destination, which is unfolded and scrolled into
view.

Always On Top uses the supported native window-stacking interface. Unsupported
GTK backends, including Wayland, retain the stored preference but disable the
operation and show a localized explanation. Manually toggling the command on a
supported desktop clears the temporary startup override and updates the preference.
New Window and Restart load the stored preference without inheriting that override.

Opening a missing path asks whether to create an empty file. Yes creates the file,
No keeps an empty document, and Cancel exits. A read failure reports an error and
keeps an empty document; it does not rewrite the target.

A standalone `+` starts a multi-file list. Each following argument is one path,
including names that resemble options. Quote each path containing spaces. The
first 32 paths are collected; additional paths are ignored, matching the pinned
original. A list with no paths opens the usual empty window. One path follows
ordinary single-file startup and its configured window routing. Two or more
paths launch independent editor processes and the dispatcher exits without an
extra blank window.

Set `[Settings2] MultiFileArg=1` in the selected portable or installed INI to use
multi-file collection without `+` (default `0`). A standalone `-` or `--` still
forces the remaining arguments to form one path, including unquoted spaces.
wxnotepad reads this default before parsing paths. This fixes the pinned
original's startup-order defect: it reads `MultiFileArg` after parsing, leaving
the setting ineffective unless `+` was already present. The working default is
an explicitly approved compatibility correction.

Options before a multi-file list apply to every target. Each dispatched child
receives a final new-window override, so even Reuse Window, Single File Instance,
or an explicit `-rs` does not replace an existing document. Duplicate targets
open independently. These temporary overrides do not change saved window
preferences. Paths resolve from the original caller's working directory, and
arguments pass directly to the process launcher without an additional shell.
Missing-file confirmations, cancellation, directory targets and reading errors
are handled independently in each child; another target's failure leaves the
other documents intact. Internal Restart and New Window arguments retain their
single-document meaning even when `MultiFileArg` is enabled.

The `e`, `s`, `g` and `G` options require a following parameter. A missing
parameter shows a localized error and exits unsuccessfully without opening or
creating a file. This fixes the original parser's defect of interpreting a
missing-parameter option as a pathname. The original adjacent rule is retained:
a nonnumeric `g` or `G` operand makes the remaining command line a single
pathname, including the option prefix. Numeric prefixes still use the original
position parser.

`e ANSI` selects the native source interpretation. ASCII input adopts the
explicit system codec; other bytes retain native ANSI identity on a legacy
native code page. On a UTF-8 native Windows system, the original legacy ANSI
recode rule applies. Native bytes that cannot be recoded remain available for
an unchanged save. This source option is separate from internal restoration of
the current document format.

The named `--startup-*`, `--window-position`, `--read-only` and `--observe`
arguments remain reserved for application relaunch and acceptance observation.
