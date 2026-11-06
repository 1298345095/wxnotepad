#pragma once
#include <string>
#include <string_view>

// Original resource IDs passed through Scintilla's CustomCaseMapping message.
std::string MapCustomCase(std::string_view text, int command);

// UTF-16 code units, including on platforms with 32-bit wchar_t. The pinned
// Sort Lines Ignore Case option uses NLS uppercase without linguistic casing.
std::wstring OriginalNlsUppercase(std::wstring_view text);
