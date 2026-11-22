#include "app/dialogs/zoom_input.h"
#include <cstdint>
#include "ScintillaTypes.h"
#include <wx/combobox.h>
#include <wx/strconv.h>
#include <cstdlib>

wxComboBox* CreateZoomInput(wxWindow* parent, int zoom) {
    // Original Dialogs.cpp::InitZoomLevelComboBox serves both screen and print zoom.
    auto* input = new wxComboBox(parent, wxID_ANY, wxString::Format("%d%%", zoom));
    input->SetMaxLength(8);
    for (int level : {500, 450, 400, 350, 300, 250, 200, 175, 150, 125, 100, 75, 50, 25})
        input->Append(wxString::Format("%d%%", level));
    return input;
}

std::optional<int> ReadZoomLevel(const wxComboBox& input) {
    const auto text = input.GetValue().ToStdString(wxConvUTF8);
    char* end = nullptr;
    const long level = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || level < Scintilla::MinZoomLevel || level > Scintilla::MaxZoomLevel) return std::nullopt;
    return static_cast<int>(level);
}
