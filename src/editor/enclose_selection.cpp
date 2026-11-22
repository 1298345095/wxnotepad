#include "editor/editor_control.h"
#include "editor/win_edit_line_endings.h"
#include <ScintillaTypes.h>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace {
std::string EnclosureBytes(const wxString& text, Scintilla::EndOfLine mode) {
    auto bytes = text.ToStdString(wxConvUTF8);
    ConvertWinEditLineEndings(bytes, mode);
    return bytes;
}
} // namespace

bool EditorControl::EncloseSelection(const wxString& before, const wxString& after) {
    using Scintilla::Message;
    // Adapted from Notepad4 89b8b36 Edit.cpp::EditEncloseSelection.
    if (Send(Message::SelectionIsRectangle)) return false;
    if (before.empty() && after.empty()) return true;
    const auto start = Send(Message::GetSelectionStart);
    const auto end = Send(Message::GetSelectionEnd);
    const auto mode = static_cast<Scintilla::EndOfLine>(Send(Message::GetEOLMode));
    const auto prefix = EnclosureBytes(before, mode);
    const auto suffix = EnclosureBytes(after, mode);
    const auto length = static_cast<intptr_t>(prefix.size());
    Send(Message::BeginUndoAction);
    if (!prefix.empty()) {
        Send(Message::SetTargetStart, start);
        Send(Message::SetTargetEnd, start);
        Send(Message::ReplaceTarget, prefix.size(), reinterpret_cast<intptr_t>(prefix.c_str()));
    }
    if (!suffix.empty()) {
        Send(Message::SetTargetStart, end + length);
        Send(Message::SetTargetEnd, end + length);
        Send(Message::ReplaceTarget, suffix.size(), reinterpret_cast<intptr_t>(suffix.c_str()));
    }
    Send(Message::EndUndoAction);
    if (start == end) {
        Send(Message::SetSel, start + length, start + length);
    } else if (Send(Message::GetCurrentPos) < Send(Message::GetAnchor)) {
        Send(Message::SetSel, end + length, start + length);
    } else {
        Send(Message::SetSel, start + length, end + length);
    }
    return true;
}
