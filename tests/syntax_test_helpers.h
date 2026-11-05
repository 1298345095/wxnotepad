#pragma once

#include "editor_test_helpers.h"
#include <wx/wx.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace TestSyntax {
inline void Paint(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto prefix = std::min(editor->Send(Scintilla::Message::GetLength), intptr_t{128});
    // Observe the normal visible style pass, including asynchronous Wayland frames.
    for (int attempt = 0; attempt < 100; ++attempt) {
        wxYield();
        editor->Refresh();
        editor->Update();
        wxYield();
        if (editor->Send(Scintilla::Message::GetEndStyled) >= prefix) return;
        wxMilliSleep(10);
    }
}
inline void Command(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    if (!frame->GetEventHandler()->ProcessEvent(event))
        throw TestEditor::Failure("real editor command must be handled");
    Paint(frame);
}
inline void Style(EditorControl* editor, size_t position, int expected) {
    const auto actual = editor->Send(Scintilla::Message::GetStyleIndexAt, position);
    if (actual != expected) {
        std::fprintf(stderr, "position=%zu expected_style=%d actual_style=%zd\n", position, expected, actual);
        throw TestEditor::Failure("syntax style at known UTF-8 byte position differs from pinned lexer");
    }
}
} // namespace TestSyntax
