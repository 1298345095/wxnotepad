# Original URL conversion

Reference: Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`src/Edit.cpp::EditURLEncodeSelection`, `EditURLEncode`, `EditURLDecode`,
and `EditReplaceMainSelection`. The editor buffer remains UTF-8.

`tools/capture_url_fixtures.py` drives an owned original Windows editor process
through its actual Scintilla messages and original command IDs. It reads raw
document bytes and selection/history state, and checks that one Undo restores
the exact input before generating `tests/url_reference_fixtures.tsv`. This
readable table groups byte/state-identical captured outcomes with explicit command
bits; the test still invokes and counts every captured command/input case. The
fixtures were captured on Windows 11 build 26200 using the retained baseline
executable built from the pinned original checkout. The build log is retained
at `E:/NewNotepad4/wxnotepad/build/notepad4-baseline.log`.

The full capture, raw Shlwapi API probes, selection/state captures, native
display logs and red/green runs are retained outside the repository at
`E:/NewNotepad4/wxnotepad-evidence/next-native-text-tools-2026-11-06/134`.

## Observed behavior

- Encoding trims exactly space, BEL, BS, FF, LF, CR, TAB and VT at both ends.
  An entirely trimmed selection leaves bytes, selection and history alone.
  Decoding preserves surrounding whitespace.
- Ordinary encoding uses the Windows URL parser: schemes become lowercase,
  an authority with an empty path gets `/`, known hierarchical schemes
  normalize backslashes, and a query preceding or following a fragment is
  emitted before that fragment. Authority, query and fragment bytes have
  different escaping rules from the path.
- Component encoding percent-encodes all bytes outside RFC 3986's unreserved
  ASCII set, including `%`, `/`, `?` and `#`. Ordinary encoding preserves `%`.
  Both emit uppercase UTF-8 percent escapes where escaping applies.
- Decoding is one pass. Plus signs remain plus signs. Malformed percent escapes
  stay literal; decoded invalid UTF-8 follows the original API's replacement
  rules, including incomplete sequences and invalid second bytes.
- Raw malformed UTF-8 uses the original input-conversion boundary before all
  three commands. Both raw `ED A0 80` and `E0 80 80` consume the invalid second
  continuation byte with its prefix and become two U+FFFD characters: encoding
  emits `%EF%BF%BD%EF%BF%BD`, while decoding emits six UTF-8 bytes. The fixtures
  capture the corresponding selection endpoints and exact Undo/Redo bytes.
- Scintilla's text-mode selection getter maps embedded NUL bytes to spaces.
  A decoded `%00` makes `UrlUnescapeW` return `E_INVALIDARG`; the original
  zero-initialized output allocation and unchanged count then replace the
  selection with `align_up(selectionByteCount + 1, 16) * 3` NUL bytes.
  Captures include byte lengths 14, 15, 16, 30, 31 and 32 and a Unicode input.
- The input getter gathers all selected ranges with document-EOL separators.
  Replacement affects the main selection and preserves its direction;
  setting the result selection collapses the other ranges. Empty input
  returns before the rectangular-selection check. A nonempty rectangle
  produces the original warning and leaves document and selection unchanged.
- Replacing unchanged nonempty text still marks the document modified and
  creates an Undo entry. Such a replacement clears pending Redo. Whitespace-only
  encoding preserves pending Redo. A read-only invocation retains bytes and
  history while setting and clamping result selection endpoints.

The commands retain their original names and Ctrl+Shift+E / Ctrl+Shift+R
accelerators. Selection enablement follows the original selection gate,
including selected read-only text. Document encoding and EOL metadata are
preserved by these editing commands.

## Platform boundary

Windows retains the original Shlwapi calls and flags. The Linux adapter
implements the observed boundary using UTF-8 bytes and the same selection
messages. The installed wxWidgets 3.3.3 URI facility was checked against these
fixtures through its local source: `src/common/uri.cpp::AppendEscaped` emits
lowercase hex, `ParseQuery` escapes query spaces, and `Unescape` returns through
`wxString::FromUTF8` after constructing a NUL-terminated byte buffer. These
behaviors differ from the original uppercase escapes, untouched query bytes,
invalid UTF-8 replacement and decoded-NUL allocation results. A direct wxURI
substitution would therefore change captured editing results.
