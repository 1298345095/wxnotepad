#pragma once

#include "editor/indentation_settings.h"
class wxWindow;
bool ShowTabSettingsDialog(wxWindow* parent, OriginalSettings& preferences, SyntaxLanguage language,
                           IndentationSettings& indentation);
