#pragma once
#include <wx/string.h>
class EditorControl;
struct SyntaxDefinition;
namespace WordCompletionOption {
inline constexpr int CloseTags = 1;
inline constexpr int CompleteWord = 2;
inline constexpr int ScanWordsInDocument = 4;
inline constexpr int OnlyWordsInDocument = 8;
inline constexpr int EnglishIMEModeOnly = 16;
inline constexpr int Default = CloseTags | CompleteWord | ScanWordsInDocument;
} // namespace WordCompletionOption
namespace CompletionFillUp {
inline constexpr int Enter = 1;
inline constexpr int Tab = 2;
inline constexpr int Space = 4;
inline constexpr int Punctuation = 8;
inline constexpr int Default = Enter | Tab | Space | Punctuation;
} // namespace CompletionFillUp
namespace WordCompletionLimits {
inline constexpr int MinScanTimeoutMs = 50;
inline constexpr int MinVisibleRows = 8;
inline constexpr int MinWordLength = 1;
inline constexpr int MinNumberLength = 0;
inline constexpr int ScopeMask = 15;
inline constexpr int ScanScopeShift = 4;
inline constexpr int DefaultScopes = 255;
inline constexpr size_t MaxPunctuationCharacters = 31;
} // namespace WordCompletionLimits
// Original EditAutoCompletionConfig fields and Notepad4.cpp::LoadSettings defaults.
struct WordCompletionSettings {
    bool ignoreCase = false;
    bool latexInputMethod = false;
    int completeOption = WordCompletionOption::Default;
    int completeScope = 15;
    int scanWordScope = 15;
    int scanWordsTimeout = 500;
    int visibleItemCount = 16;
    int minWordLength = 1;
    int minNumberLength = 3;
    int fillUpMask = CompletionFillUp::Default;
    wxString fillUpPunctuation = ";,()[]{}\\/";
};
void CompleteWord(EditorControl& editor, const SyntaxDefinition& definition, const WordCompletionSettings& settings,
                  bool autoInsert = true);

namespace Scintilla {
struct NotificationData;
}
void AcceptWordCompletion(EditorControl& editor, const WordCompletionSettings& settings,
                          const Scintilla::NotificationData& notification);
