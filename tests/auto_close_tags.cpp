#include "native_key_helpers.h"
#include "syntax/syntax_scheme.h"
#include "document/document_session.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <initializer_list>
#include "auto_close_tags_reference.h"
#include "auto_close_tags_encoding_reference.h"
#include <cstdio>
#include <stdexcept>
namespace {
using Scintilla::Message;
void Character(EditorFrame* frame, int ch, bool native) {
    auto* editor = frame->Editor();
#ifdef __WXMSW__
    (void)native;
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_CHAR, ch, 0);
    return;
#else
    if (native) {
        NativeKeys::Shortcut(frame, '.', false, false, true);
        return;
    }
#endif
    wxKeyEvent event(wxEVT_CHAR);
    event.m_keyCode = ch;
    event.m_uniChar = ch;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void State(EditorControl* editor, const AutoCloseTagsReference::State& state) {
    if (NativeSearch::Text(editor) != state.text) {
        std::fprintf(stderr, "actual='%s' expected='%s'\n", NativeSearch::Text(editor).c_str(), state.text);
        throw std::runtime_error("character notification must reproduce original text");
    }
    if (editor->Send(Message::GetAnchor) != state.anchor || editor->Send(Message::GetCurrentPos) != state.caret)
        throw std::runtime_error("original anchor and caret direction");
    if (editor->Send(Message::GetSelections) != static_cast<intptr_t>(state.selections.size()))
        throw std::runtime_error("original selection count");
    size_t index = 0;
    for (const auto& selection : state.selections) {
        if (editor->Send(Message::GetSelectionNAnchor, index) != selection.anchor ||
            editor->Send(Message::GetSelectionNCaret, index) != selection.caret)
            throw std::runtime_error("original additional selection positions");
        ++index;
    }
    if (bool(editor->Send(Message::GetModify)) != state.modified ||
        bool(editor->Send(Message::CanUndo)) != state.canUndo || bool(editor->Send(Message::CanRedo)) != state.canRedo)
        throw std::runtime_error("original modification and history state");
}
std::string BytesFromHex(std::string_view hex) {
    std::string bytes;
    for (size_t position = 0; position < hex.size(); position += 2)
        bytes += static_cast<char>(std::stoi(std::string(hex.substr(position, 2)), nullptr, 16));
    return bytes;
}
void EncodedFiles(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const auto& item : AutoCloseTagsEncodingReference::cases) {
        const auto path =
            TestFiles::TemporaryDirectory(item.language == SyntaxLanguage::Html ? "tags.html" : "tags.xml");
        const auto before = BytesFromHex(item.beforeBytes);
        TestFiles::WriteBytes(path, before);
        DocumentSession document(*editor);
        if (document.Open(TestFiles::Filename(path), [] { return true; }, item.encoding).status != OpenStatus::Opened)
            throw std::runtime_error("encoded tag fixture opens");
        ApplySyntaxLanguage(*editor, item.language);
        editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
        editor->Send(Message::Colourise, 0, -1);
        Character(frame, '>', false);
        State(editor, item.after);
        if (document.Encoding() != item.encoding || document.HasBom() != item.bom ||
            editor->Send(Message::GetEOLMode) != item.eol)
            throw std::runtime_error("tag closure preserves encoding, BOM and EOL metadata");
        if (document.Save().status != SaveStatus::Saved || TestFiles::Bytes(path) != BytesFromHex(item.saved))
            throw std::runtime_error("tag closure saves exact original encoded bytes");
    }
}
class AutoCloseTagsApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
                try {
                    if (!presented) throw std::runtime_error("owned editor must be presented for native typing");
                    auto* editor = frame->Editor();
                    unsigned index = 0;
                    for (const auto& item : AutoCloseTagsReference::cases) {
                        std::printf("case %u language=%d ch=%d\n", index++, static_cast<int>(item.language),
                                    item.character);
                        editor->Send(Message::SetReadOnly, 0);
                        ApplySyntaxLanguage(*editor, item.language);
                        editor->Send(Message::SetUndoSelectionHistory, 0);
                        editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(item.before));
                        editor->Send(Message::SetSel, item.anchor, item.position);
                        if (item.extraCaret >= 0)
                            editor->Send(Message::AddSelection, item.extraCaret, item.extraAnchor);
                        editor->Send(Message::SetOvertype, item.overtype);
                        editor->Send(Message::EmptyUndoBuffer);
                        editor->Send(Message::SetSavePoint);
                        editor->Send(Message::SetUndoSelectionHistory, 3);
                        editor->Send(Message::Colourise, 0, -1);
                        editor->Send(Message::SetReadOnly, item.readOnly);
                        Character(frame, item.character, std::string_view(item.before) == "<div" && !item.readOnly);
                        State(editor, item.after);
                        for (const auto& state : item.undo) {
                            editor->Send(Message::Undo);
                            State(editor, state);
                        }
                        for (const auto& state : item.redo) {
                            editor->Send(Message::Redo);
                            State(editor, state);
                        }
                        DocumentSession document(*editor);
                        const auto path = TestFiles::TemporaryDirectory("auto-close-tags.txt");
                        if (document.SaveCopy(TestFiles::Filename(path)).status != SaveStatus::Saved ||
                            TestFiles::Bytes(path) != item.saved)
                            throw std::runtime_error("saved bytes reproduce independently captured original file");
                    }
                    editor->Send(Message::SetReadOnly, 0);
                    EncodedFiles(frame);
                    std::puts("PASS: original character notifications, styles, read-only state and Undo/Redo");
                    result_ = 0;
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "FAIL: %s\n", error.what());
                }
                frame->Destroy();
                ExitMainLoop();
            });
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(AutoCloseTagsApp);
