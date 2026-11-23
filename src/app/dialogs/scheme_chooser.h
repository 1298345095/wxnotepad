#pragma once
#include "syntax/syntax_catalog.h"
#include <optional>
class wxWindow;
struct OriginalSettings;
std::optional<SyntaxLanguage> ChooseSyntaxScheme(wxWindow* parent, SyntaxLanguage current, OriginalSettings& settings,
                                                 bool favorite = false);
