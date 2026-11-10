# Original encoding selection

File > Encoding changes the live document's destination format. The delivered
direct choices retain the original order: ANSI (when its native codec is
delivered), UTF-8, UTF-8 BOM and UTF-16LE BOM. More (F9) offers those formats,
BOM-less UTF-16LE, GBK, Windows-1252 and DOS-437 in their original catalog groups and
names. It selects the current identity and retains it on cancellation. F9
neither reloads nor saves the file; F8 below continues to select the source
codec for a disk reload.

An encoding-only change participates in the title, Save, lifecycle prompts and
file-watching decisions independently of the original core's text save point.
Non-ANSI changes retain text, selection, language, EOL bytes and Undo/Redo;
returning to the original format clears encoding-only modification. Empty
Untitled documents adopt a choice without becoming modified. Named empty files
compare BOM presence; empty-by-editing documents still retain text modification.
Save, first Save, Save As and Save with Original Time write the selected codec
and BOM and commit the original identity only on success. Save Copy writes the
selected format while retaining the current identity and pending modification.

Crossing native ANSI follows pinned `EditSetNewEncoding`/`EditConvertText`:
confirm for nonempty text or an empty buffer with history; cancellation retains
all state. A native conversion clears read-only mode, markers and Undo/Redo,
and invalidates the original encoding for nonempty text. The original core in
this port always owns UTF-8 text, so the native adapter round-trips through the
system codec instead of changing the core code page. UTF-8 native locales
retain the original same-code-page early return. Unsupported native codecs
remain unavailable until their codec tickets are delivered.

## Automatic source detection

The migrated codecs include UTF-8, UTF-16LE, Windows CP936 (GBK), Windows-1252 and DOS-437, with
UTF-8/UTF-16LE BOM metadata retained by Save. `DocumentSession::Open` applies
Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`'s
`EditDetermineEncoding` decision order. Explicit Open As is authoritative.
An optional weak choice is available to reload callers; ordinary same-path Open
does not invent a weak choice. It carries the loaded codec, BOM and native-ANSI
and native-OEM identities, so an empty reload retains the original BOM choice,
a weak native ANSI choice retains the original conversion gate, and OEM stays
distinct from its resolved codec and explicit DOS-437. OEM uses the native OEM
input on Windows and the documented native locale mapping on Linux.

For nonempty files below the original conversion-size limit, automatic Open
checks even-sized UTF-16 BOMs, UTF-8 BOMs and Latin UTF-16 without a BOM before
the ASCII and multibyte branches. ASCII uses weak choice, filename preference,
declaration, ASCII-as-UTF-8 and configured default in that order. A weak UTF-16
choice does not reinterpret ASCII. Valid multibyte UTF-8 wins over weak choices
and declarations. Other multibyte data tries valid weak, filename, declared and
configured legacy codecs, then the native ANSI input. The original binary and
file-variable gates control the final Unicode guess. `SkipUnicodeDetection`
only gates that final guess; it does not disable BOM or Latin UTF-16 detection.

Filename preferences retain `.nfo`/`.diz` CP437, `.bat`/`.cmd` native ANSI,
lowercase `.sh` or `#!/` UTF-8, and `.vhd`/`.vhdl` Windows-1252.
Windows-1252 and DOS-437 preferences are delivered. Other undelivered native
mappings continue to produce an explicit unsupported encoding error.

Declaration keys are case-sensitive `encoding`, `charset`, `coding`,
`fileencoding` and `/*!40101 SET NAMES `. Names use the original catalog aliases
(case-insensitive with hyphens/underscores ignored). The original first/last
512-byte windows, token boundaries, file-variable mask and
`enable-local-variables` gate are retained. A first-window file variable suppresses
the last-window search. UTF-16 declarations are ignored as encoding choices.
UTF-8 BOMs suppress encoding declarations. CSS `@charset "UTF-8"` is not a
recognized declaration in the original parser.

Windows uses `GetACP`, `GetOEMCP` and the original legacy locale inputs when
Windows has a UTF-8 system code page. Linux maps its native locale encoding name
through the same catalog; it has no fixed CP936 fallback. CP936 can be selected
by a declaration or the original `DefaultEncoding=936` field. The final Unicode
guess uses native `IsTextUnicode` on Windows. Its control, illegal-character,
byte-variation and native DBCS predicates are implemented directly on Linux,
including the original NUL-window gate and leading sample limit. No additional
encoding-guessing library is used.

Decoding errors and unsupported selections leave the editor, undo history,
path and loaded format unchanged. Existing strict migrated decoders remain in
use. Cancellation after detection also preserves that state. Saving retains
the selected codec and BOM; the #59 legacy-loss and #60 reopen rules remain in
the same session flow.

File > Reload > With Encoding (F8) offers UTF-8, UTF-16LE, GBK, Windows-1252 and DOS-437 as explicit
source codecs. Its initial selection normalizes loaded BOM variants to their
source codec; a native ANSI identity starts without an explicit source choice.
A native OEM identity also starts without an explicit source choice.
A modified document first receives the original discard warning;
then the encoding picker can be accepted or cancelled. Reloading reads disk
without saving live text. As UTF-8 (Shift+F8) instead uses the ordinary
Save/Discard/Cancel decision before reading the source. Save writes first and
the reload decodes those newly saved bytes, restoring the view captured before
Save cleanup. Both commands retain the existing
same-file scheme, selection, viewport and read-only rules, refresh actual
encoding/BOM metadata, and are disabled for Untitled documents.

The decision/parser/catalog ports retain upstream provenance and
[Notepad4's license](../licenses/Notepad4.txt). Native Unicode predicates were
checked against the Windows API's returned flags, including cases where its
boolean return is false but the original consumes a Unicode flag.

## Remaining codec catalog

Catalog metadata is retained in
`src/document/encoding/original_encoding_catalog.h` so recognized unsupported
choices are reported rather than relabeled. The remaining original codecs are:

| Group | Unmigrated encodings / code pages |
| --- | --- |
| Unicode | UTF-16BE with/without BOM; UTF-7 (65000) |
| Windows | 1250–1251, 1253–1258, 874 |
| Asian | Shift-JIS 932, UHC 949, Big5 950, GB18030 54936, EUC-JP 20932, Johab 1361, EUC-KR 51949, Chinese CNS 20000 |
| ISO | 28591–28600, 28603–28606, Arabic logical 38596, Hebrew logical 38598 |
| KOI8 | 20866, 21866 |
| OEM | 850, 858, 852, 720, 864, 775, 855, 866, 863, 737, 869, 862, 861, 865, 860, 857 |
| Macintosh | 10000, 10029, 10004, 10008, 10002, 10082, 10007, 10006, 10005, 10079, 10001, 10003, 10021, 10081, 10010, 10017 |
| EBCDIC | 37, 1140, 500, 870, 875, 1026 |
| Stateful | HZ 52936; ISO-2022 50220, 50221, 50222, 50225, 50227, 50229 |

ANSI and OEM catalog entries resolve through the native inputs. They are usable
only when the resulting codec is migrated. UTF-32 is outside this pinned
original catalog.

## Single-byte conversion and warnings

Windows-1252 and DOS-437 retain the original numeric settings identities 1252
and 437, aliases and declaration precedence. A sniffed ISO-8859-1 declaration
selects Windows-1252 as in the original. Native OEM retains settings identity 5.
The platform converter supplies the byte repertoire; the five nominally undefined
CP1252 bytes map to C1 controls as on Windows. Literal all-256 original fixtures
verify both complete mappings, including NUL, Euro, NBSP and box drawing.

Non-ANSI destinations retain the original warning at Save, substituting one
question mark per unsupported UTF-16 code unit only after acceptance. Save Copy
and failed/cancelled saves retain the live text, history and current identity.
Native ANSI uses its existing immediate conversion and history reset instead.
The shared original `MsgConv1`, `MsgConv2` and `MsgConv3` suppression keys persist
immediately, including when Cancel/No is chosen or SaveSettings is off. Value 1
accepts without showing the warning; value 2 disables its suppression checkbox.
The existing suppressible wx dialog supplies native buttons and checkboxes for
both search feedback and encoding warnings; ANSI keeps Yes/No and destination
loss keeps OK/Cancel.
