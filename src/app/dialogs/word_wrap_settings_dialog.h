#pragma once

class wxWindow;
struct WordWrapOptions;

// Commits only after native OK; the caller applies the accepted choices.
bool ShowWordWrapSettingsDialog(wxWindow* parent, WordWrapOptions& options);
