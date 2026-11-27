#pragma once
#include "syntax/syntax_catalog.h"
#include "syntax/csv_options.h"

class EditorControl;
void ApplySyntaxStyles(EditorControl& editor, SyntaxLanguage language);
void ApplySyntaxLanguage(EditorControl& editor, SyntaxLanguage language,
                         std::optional<SyntaxLanguage> markupDialect = std::nullopt,
                         uint32_t csvOptions = CsvOption::Default);
void ApplyIndentationGuides(EditorControl& editor, SyntaxLanguage language, bool show, bool simple);
void ApplyLongLineColour(EditorControl& editor);
