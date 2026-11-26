#pragma once

#include "editor/font_settings.h"
#include "settings/ini_document.h"

// Original style-file format, independent of ordinary settings and save policy.
// Every operation uses an explicit document/path; import and export never select
// or replace the primary settings document.
namespace StyleProfileIO {
FontSettings Load(const wxString& path, StyleTheme theme, const FontSettings& current);
// Merge only changes since the saved snapshot, preserving untouched style text
// and fields belonging to other versions/languages in the destination.
void WriteChanges(IniDocument& document, const FontSettings& current, const FontSettings& saved);
bool Import(const wxString& path, FontSettings& fonts, SyntaxExtensions& extensions);
bool Export(const wxString& path, const FontSettings& fonts, const SyntaxExtensions& extensions);
} // namespace StyleProfileIO
