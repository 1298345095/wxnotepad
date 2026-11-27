# Windows font quality

Settings → Advanced Settings → Font Quality follows the pinned Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a` setting and native mappings:

| Menu | `FontQuality` | GDI `LOGFONT.lfQuality` | Direct2D text antialias mode |
| --- | ---: | --- | --- |
| Default | 0 | `DEFAULT_QUALITY` | `DEFAULT` |
| None | 1 | `NONANTIALIASED_QUALITY` | `ALIASED` |
| Standard | 2 | `ANTIALIASED_QUALITY` | `GRAYSCALE` |
| ClearType | 3 | `CLEARTYPE_QUALITY` | `CLEARTYPE` |

None draws aliased text. Default delegates to native rendering defaults.
ClearType is the application default. Imported integers are clamped to 0–3;
an unreadable integer uses the default. The default value is omitted when saving.
The ordinary Save Settings On Exit preference and Save Settings Now apply to
this setting, including changes made while exit saving is disabled.

GDI drawing fonts and independent worker measurement fonts share a native font
description. Workers retain their private HDC and HFONT resources. Direct2D
uses the original font and surface implementations. Completion lists and the
IME composition font receive the corresponding native font quality; system IME
candidate windows keep their own native presentation.

Linux continues to use GTK/Pango/Cairo font settings, hides these Windows menus,
and preserves the stored Windows `FontQuality` field when saving the portable
configuration.

The focused checks are `original_font_quality_menu` and
`original_font_quality_lifecycle`, with native font/resource observations and
the retained original DirectWrite raster oracle. The optional
`windows_font_quality_pinyin_*` cases exercise real Microsoft Pinyin preedit;
they require `WXNOTEPAD_TEST_PINYIN=1` and an installed input method. A skipped
Pinyin case supplies no preedit evidence. `tests/reference_capture/capture_font_quality.py`
records controlled original-app rasters. Its `--dpi96` mode is complementary
DPI-unaware process emulation; it does not establish mixed-monitor behavior.
