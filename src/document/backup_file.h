#pragma once

#include <string_view>
#include <wx/string.h>
#include "ScintillaTypes.h"

namespace DocumentBackups {
wxString Directory();
// Returns the created path, or empty on failure. A failed sibling write never
// falls back: only failure to create it permits the shared backup directory.
wxString Write(const wxString& source, std::string_view bytes, bool automatic, bool nativeDefault,
               Scintilla::EndOfLine lineEnding);
} // namespace DocumentBackups
