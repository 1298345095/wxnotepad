#pragma once
#include "syntax/syntax_catalog.h"

class EditorControl;
void ApplySyntaxLanguage(EditorControl& editor, SyntaxLanguage language);
void ApplyIndentationGuides(EditorControl& editor, SyntaxLanguage language, bool show, bool simple);
void ApplyLongLineColour(EditorControl& editor);
