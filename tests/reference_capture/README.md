# Original reference fixture capture

These Windows-only test utilities drive the pinned original Notepad4 executable
at revision `89b8b36e0ee2c23f73be922315f17261ccfbbe1a` to collect independent
expected results for the application regression tests. They use Python's standard
library and Win32 APIs. Supply the original executable and an external evidence
output path as documented in the corresponding observation:

- [Block navigation](../../docs/observations/block-navigation.md)
- [Automatic tag closure](../../docs/observations/auto-close-tags.md)
- [Caret appearance](../../docs/observations/caret-appearance.md)
- [Header insertion](../../docs/observations/header-insertion.md)
- [Manual word completion](../../docs/observations/word-completion.md)
- [Selection preferences](../../docs/observations/selection-preferences.md)
- [TOML/YAML/CMake schemes and operations](../../docs/observations/syntax-config-languages.md)

Raw captures belong in the external evidence directory. Reviewed reference data
belongs in `tests/`. Regenerate it deliberately when comparing original behavior.

Sonar's existing `sonar.tests=tests` and `tests/**/*.py` inclusion classify these
utilities as test code. Application code and maintained production tools remain
subject to the existing 80% new-code coverage gate.
