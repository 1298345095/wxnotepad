# Original style themes

Reference: Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.
`tools/style_profiles.py` imports the original `EDITSTYLE` arrays and
`EditStyleX.h` keys, retaining grouped style IDs and unresolved style strings.
It also retains the exact distributed UTF-16LE Dark INI with its SHA-256 and
license notice. Regenerate with `--source <pinned Notepad4 checkout>`.

`OriginalSettings::fonts` is the active runtime profile. Its existing
`codeStyle`, `textStyle`, and `Scheme(language).defaultStyle/useCode` remain
the font/inheritance interface. `globalStyles` and each scheme's `tokenStyles`
use original INI keys. `GlobalStyleDefinitions()` and
`SyntaxStyleDefinitions(language)` provide stable keys, source defaults and
grouped token IDs for consumers. Enumerate the shared syntax catalog for
scheme selection; dialects resolve through `SyntaxSettingsLanguage`.

`SettingsStore::SwitchStyleTheme` selects `StyleTheme::Default` (0) or
`StyleTheme::Dark` (1). The original `[Styles] StyleTheme` belongs to the main
INI. Default styles belong to that INI, and Dark styles belong to its sibling
`Notepad4 DarkTheme.ini`. A missing user Dark file uses the distributed defaults
without creating a writable copy until styles change. The existing portable
and installed configuration discovery policy supplies the main INI location.

Selecting the current theme does nothing. A dirty style profile is saved before
changing theme, including when ordinary Save Settings On Exit is off. Omitted
`UseDefaultCodeStyle` keys retain the current lexer inheritance choice, as in
the original; other absent style strings use their original source defaults.
Explicitly empty styles remain empty. Saving merges supported fields and
preserves unavailable-language sections and unimplemented keys.
The switch saves style metadata/profile changes only; unrelated dirty view/tool
preferences retain their ordinary save policy. As in the original, a failed
pre-switch save is reported while the requested theme still becomes active.

`ApplySyntaxStyles` reapplies fonts, global display styles and token styles
without resetting the lexer or document styling. The document's bytes,
encoding, selection, save point, history and folds remain owned by the existing
editor/document lifecycle. A scheme change uses `ApplySyntaxLanguage`, which
retains the same active profile. Pixel style sizes follow native DPI and zoom.

The independent Windows capture uses an owned original executable copy on a
named private desktop, direct owned-window messages, and no clipboard input.
`tests/reference_capture/capture_style_themes.py` records both profiles,
repeated selection, dirty inheritance, save-before-switch and restart. Original
records are retained under
`F:/wxnotepad-evidence/style-workflows-implementation-2026-11-23/263/original-02/`.

For a Default code font of 13.5pt/weight 600, text font of 16pt and C/C++
Default `size:+1.5`, the original observes:

| Stage | Default size | Weight | Fold ellipsis size | C/C++ comment foreground |
| --- | ---: | ---: | ---: | --- |
| Customized Default | 15pt | 600 | 11.5pt | `#123456`, italic |
| Dark | 11pt | 400 | 9pt | `#608060` |
| Dark selected again | 11pt | 400 | 9pt | `#608060` |
| Default after dirty Dark inheritance | 17.5pt | 400 | 14pt | `#123456`, italic |
| Dark after restart | 11pt | 400 | 9pt | `#608060` |

The dirty Dark switch writes Dark's `UseDefaultCodeStyle=0` and the old main
`StyleTheme=1` before loading Default. Every in-process snapshot retains the
same document bytes, reversed selection, clean save point and available Redo.

`tests/style_theme_process.py` exercises the actual bundled executable through
the existing process observer. It covers unconfigured startup, all three UI
languages, a missing user Dark file, repeated selection, independent restarts,
custom fractional font profiles, the actual inheritance menu, save-before-switch,
Save Settings On Exit, untouched settings and exact saved UTF-8 BOM/CRLF bytes.
The installed case runs the copied package under a genuinely read-only owned
`/opt` prefix with relocated `share/locale` and isolated writable XDG settings.
Its CTest registrations are `packaged_style_theme_lifecycle` and the Linux
`packaged_style_theme_installed`; both retain a 180-second guard.
