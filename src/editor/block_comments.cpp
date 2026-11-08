#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <ScintillaStructures.h>
#include <cstring>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
bool EditorControl::ToggleBlockComments(const char* open, const char* close, int commentStyleLast) {
    using Scintilla::Message;
    // Adapted from Notepad4 89b8b36 EditAutoC.cpp::EditUncommentBlock and
    // EditToggleCommentBlock. Use the current styles, including line/doc styles;
    // the original command does not force pending highlighting to run.
    const auto commentAt = [this, commentStyleLast](intptr_t position) {
        const auto style = Send(Message::GetStyleIndexAt, position);
        return style > 0 && style <= commentStyleLast;
    };
    const auto selectionStart = Send(Message::GetSelectionStart);
    if (!commentAt(selectionStart)) return EncloseSelection(wxString::FromUTF8(open), wxString::FromUTF8(close));
    const auto selectionEnd = Send(Message::GetSelectionEnd);
    auto end = selectionStart;
    do {
        ++end;
    } while (commentAt(end));
    if (end < selectionEnd) return EncloseSelection(wxString::FromUTF8(open), wxString::FromUTF8(close));
    auto start = selectionStart;
    do {
        --start;
    } while (commentAt(start));
    Scintilla::TextToFindFull closing{
        {selectionStart, end}, close, static_cast<Scintilla::Position>(std::strlen(close)), {0, 0}};
    end = Send(Message::FindTextFull, 0, reinterpret_cast<intptr_t>(&closing));
    if (end < 0) return EncloseSelection(wxString::FromUTF8(open), wxString::FromUTF8(close));
    Scintilla::TextToFindFull opening{
        {selectionEnd, start + 1}, open, static_cast<Scintilla::Position>(std::strlen(open)), {0, 0}};
    start = Send(Message::FindTextFull, 0, reinterpret_cast<intptr_t>(&opening));
    if (start >= 0 && opening.chrgText.cpMax <= end) {
        Send(Message::BeginUndoAction);
        Send(Message::DeleteRange, end, closing.chrgText.cpMax - end);
        Send(Message::DeleteRange, start, opening.chrgText.cpMax - start);
        Send(Message::EndUndoAction);
        return true;
    }
    return EncloseSelection(wxString::FromUTF8(open), wxString::FromUTF8(close));
}
