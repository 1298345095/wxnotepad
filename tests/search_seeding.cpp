#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool Focused(wxWindow* window) {
    wxStopWatch clock;
    while (wxWindow::FindFocus() != window && clock.Time() < 2000) {
        wxMilliSleep(10);
        wxYield();
    }
    return wxWindow::FindFocus() == window;
}
class SeedingApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                using Scintilla::Message;
                auto* editor = frame->Editor();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文\\path\r\nnext"));
                editor->Send(Message::SetSel, 0, editor->Send(Message::GetLength));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                NativeSearch::Command(frame, wxID_REPLACE);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                Require(query && query->GetValue() == wxString::FromUTF8("中文\\\\path\\r\\nnext"),
                        "Replace must seed and escape the actual Unicode multiline selection");
                Require(Focused(replacement), "initial Replace with selected text must focus replacement");
                Require(NativeSearch::Text(editor) == "中文\\path\r\nnext", "seeding must retain document bytes");
                Require(!editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "seeding must retain save point and history");
                std::puts("PASS: selected Unicode multiline query, replacement focus and unchanged document");
                auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
                query->SetValue("stored");
                NativeSearch::Click(dialog, wxID_FORWARD);
                query->SetValue("discarded draft");
                NativeSearch::Click(dialog, wxID_CANCEL);
                editor->Send(Message::SetSel, 0, 0);
                Require(wxTheClipboard->Open(), "test must own native clipboard");
                wxTheClipboard->Clear();
                wxTheClipboard->Close();
                NativeSearch::Command(frame, wxID_REPLACE);
                Require(query->GetValue() == "stored" && Focused(query),
                        "closing and reopening must fall back to the last submitted query, discarding unsent draft");
                std::puts("PASS: closed-dialog draft and stored-query lifecycle");
                NativeSearch::Click(dialog, wxID_CANCEL);
                Require(wxTheClipboard->Open(), "test must publish native Unicode clipboard text");
                wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8("剪贴\\path\ntext")));
                wxTheClipboard->Close();
                NativeSearch::Command(frame, wxID_FIND);
                Require(query->GetValue() == wxString::FromUTF8("剪贴\\\\path\\ntext") && Focused(query),
                        "a newly opened Find must seed clipboard controls and focus query");
                query->SetValue("existing draft");
                editor->Send(Message::SetSel, 0, 6);
                NativeSearch::Command(frame, wxID_FIND);
                Require(query->GetValue() == "existing draft" && Focused(query),
                        "reopening the existing Find must retain its query control without reading clipboard");
                NativeSearch::Command(frame, wxID_REPLACE);
                Require(query->GetValue() == wxString::FromUTF8("中文") && Focused(replacement),
                        "Ctrl+H dialog switch must seed selection and focus replacement");
                auto* regex = wxDynamicCast(wxWindow::FindWindowByName("Regular expression", frame), wxCheckBox);
                auto* transform = wxDynamicCast(wxWindow::FindWindowByName("Transform backslashes", frame), wxCheckBox);
                Require(transform && !transform->GetValue(), "plain selection must clear transform backslashes");
                NativeSearch::Toggle(regex, true);
                query->SetValue("stored");
                NativeSearch::Click(dialog, wxID_FORWARD);
                NativeSearch::Click(dialog, wxID_CANCEL);
                editor->Send(Message::SetSel, 0, editor->Send(Message::GetLength));
                NativeSearch::Command(frame, wxID_REPLACE);
                Require(query->GetValue() == wxString::FromUTF8("中文\\\\path\\r\\nnext") && regex->GetValue() &&
                            !transform->GetValue(),
                        "regex seeding must retain regex punctuation and escaping without enabling transform");
                std::puts("PASS: clipboard, existing-dialog reopen, Ctrl+H switch and regex seeding rules");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SeedingApp);
