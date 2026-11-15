# Locked wxWidgets stock catalogs

These PO files are verbatim copies from wxWidgets 3.3.3, the application's locked
SDK version, distributed under the wxWidgets package license. The original
copyright and translator notices remain in each PO. The package license is
included in [licenses/wxWidgets.txt](../../licenses/wxWidgets.txt), and the artifact
bundler retains that notice.

Canonical source tree: `E:/NewNotepad4/wx-v3.3.3/locale/` (unpacked wxWidgets 3.3.3).
Upstream release: https://github.com/wxWidgets/wxWidgets/releases/tag/v3.3.3

| File | SHA-256 of imported bytes |
| --- | --- |
| zh_CN.po | cceb0599b42428f02e438ba0c7ad4257695ac7783b00421d4c66c9a748864742 |
| zh_TW.po | e999f6c2562e999102336ce9114004eb2a8e807ae0b0461116e9fd23f2cff0be |

Compile these sources with the application's host msgfmt step as `wxstd.mo`.
Do not depend on optional SDK-installed locale files or merge application messages
into these stock catalogs. Updating the locked toolkit requires reviewing these
sources and notices together.
