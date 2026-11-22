#pragma once

#include <optional>

class wxComboBox;
class wxWindow;

wxComboBox* CreateZoomInput(wxWindow* parent, int zoom);
std::optional<int> ReadZoomLevel(const wxComboBox& input);
