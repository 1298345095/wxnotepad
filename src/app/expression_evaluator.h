#pragma once
#include <wx/string.h>

struct ExpressionEvaluation {
    wxString text;
    wxString error;
    size_t line = 0;
    size_t column = 0;
    size_t sourceOffset = 0;
};

// Fresh JavaScript state for each command; positions exclude the Math wrapper.
ExpressionEvaluation EvaluateExpression(const wxString& expression, size_t resultCharacters);
