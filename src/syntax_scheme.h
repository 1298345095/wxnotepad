#pragma once
#include <wx/string.h>
#include <cstdint>

class EditorControl;
enum class SyntaxLanguage : uint8_t { PlainText, Cpp };

SyntaxLanguage LanguageForPath(const wxString& path);
wxString LanguageName(SyntaxLanguage language);
void ApplySyntaxLanguage(EditorControl& editor, SyntaxLanguage language);
