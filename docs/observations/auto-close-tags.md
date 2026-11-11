# Automatic HTML/XML tag closure

Reference: Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`EditAutoC.cpp::EditAutoCloseXMLTag` and `Helpers.h::IsHtmlTagChar/IsHtmlVoidTag`.
The maintained core still inserts the typed character and sends its original
character notification. The adapter reuses that notification's tentative/multiple
selection suppression and the delivered scheme's original operator/generic
metadata. A failed tag attempt remains distinguishable for member-arrow word
completion.

`tests/reference_capture/capture_auto_close_tags.py` drives the original executable
on an inherited private Windows desktop. Pass the executable and an external
output directory. It writes `original.json` there and regenerates
`tests/auto_close_tags_reference.h`; run canonical clang-format after regeneration.
The 400 frozen cases cover Plain Text, C/C++, HTML, XML, JavaScript, JSX,
TypeScript and TSX. Each records text, main and additional selections,
modification state, every Undo/Redo state, and independently read saved bytes.
Cases include malformed/incomplete markup, attributes containing `>`, exact
511-byte look-back boundaries, preprocessor/generic/operator exclusions,
embedded script/style expressions and strings, original void tags, read-only
input and multiple selections. Original behavior can close tags in plain text,
retains the permissive scanner's malformed results, and can require separate
Undo steps for the typed `>` and inserted closer.

`capture_auto_close_tags_encoding.py` separately freezes 24 original saved-file
results: HTML/XML, CRLF/CR/LF, UTF-8 with/without BOM, UTF-16LE with BOM and GBK.
GBK uses the original coding file variable. The regression opens these actual
files, types `>`, checks editor metadata and compares independently read saved
bytes with those frozen captures.

`original_auto_close_tags` tests public editor input and document state. Windows
uses native `WM_CHAR` for the complete matrix. X11 and Wayland additionally drive
a native shifted-period gesture for each language through the existing private
display driver. The shared `original_auto_completion_settings` process test
covers the native tag checkbox, Cancel/OK, live behavior, persisted bit `1`,
process reopen and preservation of unrelated completion/insertion/INI data.
Actual Microsoft Pinyin composition is checked separately; Linux IME is deferred.
