# Original-core source and portability patch

Upstream: https://github.com/zufuliu/notepad4

Pinned commit: `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.

`tools/build.py` archives only `scintilla` and `License.txt` at that commit, checks
the archive against `source.sha256`, extracts into the build directory, normalizes
CRLF to LF in the existing patch targets, then checks/applies
`original-core.patch`. Its stamp records the archive, patch and preparation-script
digests; preparation changes rebuild that generated source. Original checkout
files are never modified. The archive includes the original vendored Boost regex;
no separate Boost package is needed.

The patch has these narrowly scoped purposes:

- `VectorISA.h` and `UniqueString.h`: supply GCC x64 intrinsics and explicit
  `<cstring>` inclusion. `src/platform/intrinsics_port.h` retains the original
  32-bit bit-operation semantics even though Linux `long` is 64-bit.
- `ParallelSupport.h`: keep the native implementation on Windows; select the
  Linux heap, shared mutex, asynchronous-task and deadline equivalents from
  `src/platform/parallel_port.h` elsewhere.
- `Document.cxx`: use wx conversion in place of the Windows conversion function
  on Linux, and pass native 32-bit `wchar_t` characters to the vendored regex
  matcher. The adapter currently exposes UTF-8 only. Forward Boost searches use
  the existing six-argument `regex_search` overload with a document-start base:
  the target start limits candidate matches while lookbehind and buffer anchors
  retain document context. Without it, `aa|(?<=aa)b` skips `b` when Next restarts
  after `aa`. The application also uses forward core searches for Previous,
  because the pinned backward path searches individual lines. These adaptations
  are covered through real Find commands in `tests/search_regex.cpp`.
- Vendored Boost regex traits: preserve the original matcher and formatter, use
  the existing Windows traits on Windows, and derive Linux wide-character traits
  from the same masks using `std::locale`.
- `LexerModule.cxx`: register only the delivered Plain Text, C/C++, Python and JSON lexers.

The C/C++ lexer is the unmodified `scintilla/lexers/LexCPP.cxx` in this same
archive. `src/cpp_scheme_data.h` contains its 13 lexer-consumed keyword lists and
the expanded style groups from `src/EditLexers/stlCPP.cpp` at the same pinned
commit. Completion-only lists 13–15 are excluded. The upstream NoAutoComp
attributes on lists 2, 3, 11 and 12 do not reach the lexer; no consumed list has
PreSorted or MakeLower attributes. C/C++ uses the upstream `lexer.lang=0` dialect.
Application-only lexer attributes (completion, indentation and encoding defaults)
are not imported. Existing document and native font settings remain in effect.

Regenerate the checked-in data offline with
`python tools/update_cpp_scheme.py --source /path/to/notepad4`. The command reads
the pinned Git object without changing the source checkout and records the
original file's SHA-256 in the generated header. Builds use that checked-in
header and the verified source archive, so no additional download is needed.
The selected data carries the original BSD 3-Clause attribution in
`licenses/Notepad4.txt`.

JSON uses the unmodified `scintilla/lexers/LexJSON.cxx` from that archive.
`src/json_scheme_data.h` contains the keyword list and expanded default style
groups from pinned `src/EditLexers/stlJSON.cpp`, including the list's
`KeywordAttr_PreSorted` attribute. Regenerate it offline with
`python tools/update_json_scheme.py --source /path/to/notepad4`; the header
records the source SHA-256 and uses the same license notice. JSON selects only
`.json` automatically. The lexer also colors JSON5 constructs; no parser or
schema validation is added, and document indentation/encoding defaults stay in
effect.

The wx surface and portability primitives reuse and adapt the exercised code in
the separate prototype (`codex/prototype-original-core`, `2f44bb9`). The formal
app, control boundary, build preparation and GUI tests were written on the
main-based implementation branch. The prototype app, file/encoding/search probes,
input/IME host and measurement code are not merged into this host.

The platform adapter currently implements basic LTR rendering, native window
invalidation and font metrics. Complex shaping, completion/calltip/popup UI,
accessibility, drag-and-drop, full DPI behavior and input/IME are not established
by this slice. Unused abstract platform operations remain minimal adapters until
their ticket needs the behavior.
