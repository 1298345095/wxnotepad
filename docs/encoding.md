# Original automatic encoding selection

The migrated codecs remain UTF-8, UTF-16LE and Windows CP936 (GBK), with
UTF-8/UTF-16LE BOM metadata retained by Save. `DocumentSession::Open` applies
Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`'s
`EditDetermineEncoding` decision order. Explicit Open As is authoritative.
An optional weak choice is available to reload callers; ordinary same-path Open
does not invent a weak choice.

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
lowercase `.sh` or `#!/` UTF-8, and `.vhd`/`.vhdl` Windows-1252. A preference for
a codec outside the migrated set can therefore produce an explicit unsupported
encoding error.

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
| Windows | 1250–1258, 874 |
| Asian | Shift-JIS 932, UHC 949, Big5 950, GB18030 54936, EUC-JP 20932, Johab 1361, EUC-KR 51949, Chinese CNS 20000 |
| ISO | 28591–28600, 28603–28606, Arabic logical 38596, Hebrew logical 38598 |
| KOI8 | 20866, 21866 |
| OEM | 437, 850, 858, 852, 720, 864, 775, 855, 866, 863, 737, 869, 862, 861, 865, 860, 857 |
| Macintosh | 10000, 10029, 10004, 10008, 10002, 10082, 10007, 10006, 10005, 10079, 10001, 10003, 10021, 10081, 10010, 10017 |
| EBCDIC | 37, 1140, 500, 870, 875, 1026 |
| Stateful | HZ 52936; ISO-2022 50220, 50221, 50222, 50225, 50227, 50229 |

ANSI and OEM catalog entries resolve through the native inputs. They are usable
only when the resulting codec is migrated. UTF-32 is outside this pinned
original catalog.
