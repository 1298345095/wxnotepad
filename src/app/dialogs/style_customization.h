#pragma once
#include "syntax/syntax_catalog.h"
#include <functional>
class wxWindow;
class SettingsStore;
struct OriginalSettings;
void CustomizeSyntaxStyles(wxWindow* parent, SyntaxLanguage current, OriginalSettings& settings,
                           SettingsStore* store, const std::function<void()>& preview);
