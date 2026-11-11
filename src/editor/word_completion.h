#pragma once
#include <wx/string.h>
class EditorControl;
struct SyntaxDefinition;
// Original EditAutoCompletionConfig fields and Notepad4.cpp::LoadSettings defaults.
struct WordCompletionSettings {
    bool ignoreCase = false;
    bool latexInputMethod = false;
    int completeOption = 7;
    int completeScope = 15;
    int scanWordScope = 15;
    int scanWordsTimeout = 500;
    int visibleItemCount = 16;
    int minWordLength = 1;
    int minNumberLength = 3;
    int fillUpMask = 15;
    wxString fillUpPunctuation = ";,()[]{}\\/";
};
void CompleteWord(EditorControl& editor, const SyntaxDefinition& definition,
                  const WordCompletionSettings& settings, bool autoInsert = true);

namespace Scintilla { struct NotificationData; }
void AcceptWordCompletion(EditorControl& editor, const WordCompletionSettings& settings, const Scintilla::NotificationData& notification);
