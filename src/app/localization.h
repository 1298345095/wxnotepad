#pragma once

#include <cstdint>
#include <wx/string.h>

class wxWindow;

namespace AppLocalization {
// Returns the canonical release choice for an imported original LANGID.
uint16_t MenuPreference(uint16_t preference);
void Initialize(uint16_t preference);
void SetAccessibleName(wxWindow& window, const wxString& name);
} // namespace AppLocalization
