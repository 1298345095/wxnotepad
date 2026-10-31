# Literal search options

The Match case and Whole word controls start off, retain independent values in the
current application session, and apply on the next command. Find and Replace share
the retained dialog query and `SearchOptions`; `EditorFrame::SearchFlags()` maps the
options to the pinned original core's `MatchCase` and `WholeWord` flags.

The adapter initializes the original `CharClassify` Unicode tables before creating
any core document, as the native Notepad4 host does. ASCII letters, digits and `_`
share the word class. Accented letters use the Unicode word class. Chinese uses
the distinct CJK word class: transitions to Latin letters, digits or `_` are word
boundaries, while adjacent Chinese characters are in the same class. These are
character-class boundaries, not linguistic word segmentation. Case-insensitive
search keeps the original `CaseFolderUnicode`, including folding `Straße` to
`STRASSE` with the actual matched UTF-8 byte length.

`tests/search_options_smoke.cpp` drives native checkbox notifications, native menu
and dialog commands, dialog F3, Enter, and the application's F3 event. The following
expected ranges are fixed examples, expressed as half-open UTF-8 byte offsets.

| Document | Query and options | Expected ranges |
| --- | --- | --- |
| `ONE One one` | `one`, both off | `0..3` |
| `ONE One one` | `one`, Match case on after `0..3` | `8..11` |
| `one_one one1 1one one one-one` | `one`, both on after `0..3` | `18..21`, `22..25`, `26..29` |
| `éCOLE école école2 _école école.` | `ÉCOLE`, Whole word on | `0..6`, `7..13`, `30..36` |
| `éCOLE école école2 _école école.` | `école`, both on | `7..13` |
| `中文中文 A中文B 中文_ 中文1 中文。 中文` | `中文`, both on | `14..20`, `22..28`, `30..36`, `38..44`, `48..54` |
| `Straße STRASSE Straße2` | `STRASSE`, Whole word on | `0..7` |
| `Straße STRASSE Straße2` | `STRASSE`, both on after `0..7` | `8..15` |
| `Straße STRASSE Straße2` | `Straße`, only Match case on after `8..15` | `16..23` |
| `Straße2 Straße` | `Straße`, both retained after closing Find and New | `9..16` |

No-match checks preserve text, selection direction, save point and redo history.
The controls remain active when Find is hidden or the current document changes.
Replace revalidates selected text with these same flags: `ONE` is rejected for
`one` with Match case on, and the `one` fragment in `one_one` is rejected with
Whole word on. Turning Whole word off makes that fragment immediately eligible
for replacement.
