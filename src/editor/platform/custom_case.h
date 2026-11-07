#pragma once
#include <string>
#include <string_view>

// Original resource IDs passed through Scintilla's CustomCaseMapping message.
namespace CustomCaseCommand {
inline constexpr int CharacterToHex = 40202;
inline constexpr int HexToCharacter = 40203;
inline constexpr int EscapeC = 40204;
inline constexpr int UnescapeC = 40205;
inline constexpr int EscapeHtml = 40206;
inline constexpr int UnescapeHtml = 40207;
inline constexpr int Invert = 40208;
inline constexpr int Sentence = 40209;
inline constexpr int Title = 40210;
} // namespace CustomCaseCommand

std::string MapCustomCase(std::string_view text, int command);

// UTF-16 code units, including on platforms with 32-bit wchar_t. The pinned
// Sort Lines Ignore Case option uses NLS uppercase without linguistic casing.
std::wstring OriginalNlsUppercase(std::wstring_view text);
