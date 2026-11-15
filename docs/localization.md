# UI localization

wxnotepad uses wxWidgets 3.3.3's gettext MO reader. English is compiled into the
shared UI source. Application catalogs and the locked toolkit stock catalogs are
ordinary editable PO files; MO files are generated build output. GNU gettext is a
host build tool and adds no application runtime library dependency.

`po/languages.json` is the release-language list. It supplies original integer
LANGIDs, locale codes and UTF-8 native names. CMake generates
`generated/ui_language_data.h` from that list for runtime selection/menu labels;
the same list drives compilation, installation and artifact collection. English
has no PO/MO requirement. The original compatibility mapping and System Language
preference remain runtime/settings responsibilities.

## Host tools

The manifest requests host-only `gettext[tools]` without default features, under
the existing vcpkg baseline (GNU gettext 0.22.5). CI supplies an explicit host
triplet, verifies the actual tool versions and checks catalogs after dependency
installation. Tools reside under the host installation's
`tools/gettext/bin`; they are neither copied beside the application nor linked
into it. The toolkit and editor core need no rebuild for application PO edits.

Local SDKs need the same host tools. Set `WXNOTEPAD_GETTEXT_BIN` or pass
`--gettext-bin` before a maintenance subcommand. CMake accepts the cache setting
`-DWXNOTEPAD_GETTEXT_BIN=/path/to/host/tools/gettext/bin`; with vcpkg it discovers
that directory in the host triplet automatically, otherwise it searches PATH.
The maintained native validation entry points are in [ci.md](ci.md).

For this development host, the existing Windows directory is
`G:/vcpkg/installed/x64-windows/tools/gettext/bin`; the WSL directory is
`/home/ctrls/wxnotepad-native-rendering-sdk/installed/x64-linux/tools/gettext/bin`.
Use the directory matching the interpreter/build platform.

## Marking messages

Use literal `_()` calls for ordinary displayed English text, `wxPLURAL()` for
counts, and `wxGETTEXT_IN_CONTEXT()` / `wxGETTEXT_IN_CONTEXT_PLURAL()` for meanings
that need distinct translations. Mark static tables with `wxTRANSLATE()` or
`wxTRANSLATE_IN_CONTEXT()` and translate them when displayed after startup
initialization. Standard `wxGetTranslation()` calls are also extracted. Keep
complete parameterized sentences together. printf messages used by
`wxString::Format()` retain c-format checks; use the standard `xgettext:c-format`
comment when the format use cannot be inferred at extraction.

Place a `TRANSLATORS:` comment directly above the marked message to record
parameter meanings, usage and matching original resource identity. Stable command
IDs, `SetName` selectors, settings keys, encoding names, regex syntax, extensions,
lexer properties and real file paths are data. Visible captions and meaningful
accessible names are translated independently.

## Maintenance commands

From the source root, with the correct host gettext directory selected:

```text
python -X utf8 tools/localization.py extract
python -X utf8 tools/localization.py update
python -X utf8 tools/localization.py check
python -X utf8 tools/localization.py compile --output /path/to/generated/locale
```

`extract` regenerates only `po/wxnotepad.pot`; `update` also merges each existing
release application PO with GNU msgmerge. These explicit commands may change
tracked text. Source inputs and messages are sorted, paths are relative,
references retain filenames without line numbers, UTF-8 is explicit and volatile
POT creation timestamps are omitted. msgmerge does not guess fuzzy translations;
new messages require translator review.

`check` rejects extraction drift, mismatched message sets, malformed catalogs,
format errors, and untranslated/fuzzy application release messages. It reports
msgfmt's counts; stock catalogs retain upstream's coverage. Review translation
meaning, terminology, mnemonic placement, shortcuts and layout separately: a
successful parser/compiler does not establish language quality.

`compile` validates formats and builds both domains with GNU msgfmt without
changing PO/POT text. Ordinary CMake builds use that operation, and never run
extraction or merging. `check_localization` is the explicit CMake maintenance
check target. The CLI regression uses actual GNU tools, contextual/plural MO
lookups, missing translations, format failures and source drift. On Linux it
also builds and installs the CMake catalog pipeline and verifies a PO edit
updates deployed resources without relinking the executable.

## Resources and distribution

Development and portable output contains:

```text
locale/<code>/LC_MESSAGES/wxnotepad.mo
locale/<code>/LC_MESSAGES/wxstd.mo
```

CMake compiles into `catalogs/locale` and deploys the same bytes into the
executable's `locale` directory, including incremental builds that only change
PO files. The existing CI bundler copies each required catalog from that deployed
output; missing resources fail collection. CMake installation places identical
catalogs in `${CMAKE_INSTALL_DATADIR}/locale`. Startup receives the absolute
configured `${CMAKE_INSTALL_FULL_DATADIR}/locale` via `WXNOTEPAD_LOCALE_DIR`; set
the install prefix at configure time for a working system installation. Executable
relative lookup remains available for relocated portable output. Resource lookup
is independent of the active document, working directory and writable settings.

The stock PO files are verbatim imports from the locked wxWidgets 3.3.3 source;
[their provenance](../po/wxstd/README.md) records source hashes and notices. The
existing wxWidgets package notice is retained in artifacts. Do not depend on
optional SDK-installed MOs.

To add a release language, add its metadata row and reviewed application and
matching stock PO files. Compile/check the catalogs and run actual packaged
application acceptance on the supported platforms. The shared UI and menu/build
lists require no per-language implementation.
