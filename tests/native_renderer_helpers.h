#pragma once
#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <stdexcept>

namespace TestRenderer {
inline void Select([[maybe_unused]] EditorControl* editor) {
#ifdef WXNOTEPAD_TEST_DIRECTWRITE
    editor->Send(Scintilla::Message::SetTechnology, static_cast<uintptr_t>(Scintilla::Technology::DirectWrite));
    if (editor->Send(Scintilla::Message::GetTechnology) != static_cast<intptr_t>(Scintilla::Technology::DirectWrite))
        throw std::runtime_error("the DirectWrite regression uses the actual requested renderer");
#elif defined(__WXMSW__)
    editor->Send(Scintilla::Message::SetTechnology, static_cast<uintptr_t>(Scintilla::Technology::Default));
    if (editor->Send(Scintilla::Message::GetTechnology) != static_cast<intptr_t>(Scintilla::Technology::Default))
        throw std::runtime_error("the GDI regression uses the actual requested renderer");
#endif
}
} // namespace TestRenderer
