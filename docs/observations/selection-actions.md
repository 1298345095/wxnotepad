# Selection search and custom actions

Issue #265 restores the original Google, Bing, Wikipedia and Custom Action 1/2
commands in Tools > Action on Selection. Custom actions retain Ctrl+Shift+1/2.
All five commands require a selection and remain available in read-only documents;
an unconfigured custom action does nothing.

The implementation reads the original `[Settings2]` keys `GoogleSearchUrl`,
`BingSearchUrl`, `WikiSearchUrl`, `CustomAction1` and `CustomAction2` at invocation.
It preserves the original 519 UTF-16-unit template capacity, `%s` substitution,
`%%`, Windows environment expansion, trimming and non-component URL escaping.
The original `wsprintfW` limits the formatted command to 1024 UTF-16 units.
Wikipedia defaults to the Chinese site in both Chinese application languages.
Named documents supply their directory; untitled documents use the executable
directory. Actions launch without saving, prompting to save, or editing the
document, clipboard or Undo history.

Windows reuses the existing native executable/association launcher. Linux uses
the existing native argv and desktop launchers with wxWidgets' DOS argument
splitting for original command templates, preserving literal apostrophes and
dollar signs.

## Independent reference

The reference is Notepad4 revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. `tests/data/selection_actions_original.tsv`
contains the reviewed native boundary capture from the untouched original
selection control and extracted original helpers. The capture tools under
`tests/reference_capture/` retain source/resource hashes and collect actual
original application launches, exact target/argument/directory boundaries, and
the native `wsprintfW` long-query limit. Raw evidence remains outside the repo.

## Native validation

The registered `original_selection_actions` test drives the real menus and
shortcuts and observes independent child processes. It checks exact arguments,
working directories, selection shapes, environment and literal behavior,
settings preservation, error recovery, clipboard formats and complete document
state. Named UTF-8, UTF-16 and GBK cases verify unchanged on-disk bytes and usable
Undo/Redo/save points. Windows also registers a GDI rendering variant.

On Linux the wrapper creates private XDG HTTP/HTTPS associations and checks all
three provider defaults in all three application languages, including the
original long-query limit. Windows provider checks use an explicitly owned
guest association recorder. `tests/selection_actions_packaged.py` verifies the
actual packaged application's translated menu captions, ordering and grouping.
