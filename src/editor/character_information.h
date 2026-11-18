#pragma once
#include <string>

class EditorControl;

// Returns false for the original unsupported rectangular selection.
bool ShowHexCode(EditorControl& editor, bool nativeDefaultEncoding);
std::string CharacterInformation(EditorControl& editor);
