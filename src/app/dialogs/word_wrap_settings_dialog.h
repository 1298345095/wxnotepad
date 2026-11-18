#pragma once

class wxWindow;
struct OriginalSettings;

// Commits only after native OK; the frame supplies the live shared preferences.
bool ShowWordWrapSettingsDialog(wxWindow* parent, OriginalSettings& settings);
