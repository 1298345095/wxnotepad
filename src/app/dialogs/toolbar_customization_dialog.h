#pragma once

#include <wx/string.h>
#include <functional>
#include <span>
#include <vector>

class wxWindow;

struct ToolbarButtonChoice {
    int identity;
    wxString label;
};

// Choices use original Notepad4 identities; zero is the reusable separator.
// Changes apply immediately through the frame's existing toolbar operation.
void ShowToolbarCustomizationDialog(wxWindow* parent, std::span<const ToolbarButtonChoice> choices,
                                    std::vector<int>& buttons, std::span<const int> defaults,
                                    const std::function<void()>& apply);
