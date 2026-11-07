#include "app/editor_frame.h"
#include "steady_stopwatch.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/ffile.h>
#include <cstdio>
#include <stdexcept>
#include <memory>
#include <algorithm>
#include <string>
#ifdef __WXMSW__
#include <commctrl.h>
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool Focused(wxWindow* window) {
    TestClock::StopWatch clock;
    while (wxWindow::FindFocus() != window && clock.Time() < 2000) {
        wxMilliSleep(10);
        wxYield();
    }
    return wxWindow::FindFocus() == window;
}
void ClipboardGesture(EditorControl* editor) {
    wxString path;
    if (!wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &path)) return;
    auto* frame = wxDynamicCast(wxGetTopLevelParent(editor), wxFrame);
    const auto title = frame->GetTitle();
    const auto focusTitle = wxString::Format("wxnotepad clipboard focus: editor [%lu]", wxGetProcessId());
    frame->SetTitle(focusTitle);
    {
        wxFFile request(path, "wb");
        request.Write("editor");
    }
    frame->Raise();
    editor->SetFocus();
    TestClock::StopWatch activation;
    while ((!frame->IsActive() || !editor->HasFocus()) && activation.Time() < 3000) {
        frame->SetTitle(focusTitle);
        wxYield();
        wxMilliSleep(10);
    }
    {
        wxFFile request(path, "wb");
    }
    Require(frame->IsActive() && editor->HasFocus(), "private clipboard publisher must receive native focus");
    frame->SetTitle(title);
    bool received = false;
    wxEvtHandler nativeEvents;
    nativeEvents.Bind(wxEVT_KEY_UP, [&received](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_SHIFT) received = true;
        event.Skip();
    });
    editor->PushEventHandler(&nativeEvents);
    {
        wxFFile request(path, "wb");
        request.Write("gesture");
    }
    TestClock::StopWatch clock;
    while (!received && clock.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, "private native gesture must precede Wayland clipboard publication");
}
void SwitchDialog(wxWindow* link) {
    Require(link, "original Find/Replace toggle link must exist");
#ifdef __WXMSW__
    NMHDR notification{};
    notification.hwndFrom = static_cast<HWND>(link->GetHandle());
    notification.idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(notification.hwndFrom));
    notification.code = NM_CLICK;
    SendMessageW(static_cast<HWND>(link->GetParent()->GetHandle()), WM_NOTIFY, notification.idFrom,
                 reinterpret_cast<LPARAM>(&notification));
#elif defined(__WXGTK__)
    gtk_button_clicked(GTK_BUTTON(link->GetHandle()));
#endif
    wxYield();
}
class SeedingApp final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        wxString selectionOnly;
        selectionOnly_ = wxGetEnv("WXNOTEPAD_SEEDING_SELECTION_ONLY", &selectionOnly) && selectionOnly == "1";
#endif
        if (selectionOnly_) {
            settings_ = std::make_unique<SettingsStore>();
            settings_->values.selectOption = 11; // Disable original clipboard seeding only in this local run.
            settings_->values.wordWrap = false;
            std::puts("SKIP: shared Windows clipboard cases; native selection/focus/history cases remain enabled");
        }
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                NativeSearch::AcceptFeedback feedback;
                using Scintilla::Message;
                auto* editor = frame->Editor();
                editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
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
                NativeSearch::Selection(editor, 0, 17, "seeding must retain the original selected UTF-8 range");
                Require(NativeSearch::Text(editor) == "中文\\path\r\nnext", "seeding must retain document bytes");
                Require(!editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "seeding must retain save point and history");
                std::puts("PASS: selected Unicode multiline query, replacement focus and unchanged document");
                auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
                auto* modeLink = wxWindow::FindWindowByName("Switch Find/Replace", frame);
                query->SetValue("toggle draft");
                replacement->SetValue("kept replacement");
                SwitchDialog(modeLink);
                Require(dialog->GetTitle() == "Find" && query->GetValue() == "toggle draft" && Focused(query),
                        "explicit toggle link must retain draft query without importing the selected text");
                SwitchDialog(modeLink);
                Require(dialog->GetTitle() == "Replace" && query->GetValue() == "toggle draft" &&
                            replacement->GetValue() == "kept replacement" && Focused(query),
                        "explicit toggle to Replace must retain text and focus query despite editor selection");
                std::puts("PASS: explicit toggle suppresses selection and clipboard seeding and focuses query");
                query->SetValue("stored");
                NativeSearch::Click(dialog, wxID_FORWARD);
                query->SetValue("discarded draft");
                NativeSearch::Click(dialog, wxID_CANCEL);
                editor->Send(Message::SetSel, 0, 0);
                if (!selectionOnly_) {
                    ClipboardGesture(editor);
                    Require(wxTheClipboard->Open(), "test must own native clipboard");
                    // wxMSW Clear() only removes data published by this process.
                    // Own the fixture first so an earlier CI process cannot seed the dialog.
                    Require(wxTheClipboard->SetData(new wxTextDataObject("owned query reset")),
                            "test must publish its clipboard reset fixture");
                    wxTheClipboard->Clear();
                    wxTheClipboard->Close();
                }
                NativeSearch::Command(frame, wxID_REPLACE);
                Require(query->GetValue() == "stored" && Focused(query),
                        "closing and reopening must fall back to the last submitted query, discarding unsent draft");
                std::puts("PASS: closed-dialog draft and stored-query lifecycle");
                NativeSearch::Click(dialog, wxID_CANCEL);
                if (!selectionOnly_) {
                    ClipboardGesture(editor);
                    Require(wxTheClipboard->Open(), "test must publish native Unicode clipboard text");
                    Require(wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8("剪贴\\path\ntext"))),
                            "native clipboard must accept the Unicode fixture");
                    wxTheClipboard->Close();
                    TestClock::StopWatch publication;
                    while (publication.Time() < 250) {
                        wxYield();
                        wxMilliSleep(10);
                    }
                    NativeSearch::Command(frame, wxID_FIND);
                    Require(query->GetValue() == wxString::FromUTF8("剪贴\\\\path\\ntext") && Focused(query),
                            "a newly opened Find must seed clipboard controls and focus query");
                } else {
                    editor->Send(Message::SetSel, 0, 6);
                    NativeSearch::Command(frame, wxID_FIND);
                    Require(query->GetValue() == wxString::FromUTF8("中文") && Focused(query),
                            "a newly opened Find must seed selected text and focus query");
                }
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
                NativeSearch::Click(dialog, wxID_CANCEL);
                const std::string special = "a[.]\\b";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(special.c_str()));
                editor->Send(Message::SetSel, 0, special.size());
                NativeSearch::Command(frame, wxID_REPLACE);
                Require(query->GetValue() == "a[.]\\b" && !transform->GetValue(),
                        "selection punctuation and a plain backslash must remain unchanged");
                NativeSearch::Click(dialog, wxID_CANCEL);
                const std::string boundary = std::string(1024 * 1024 - 3, 'x') + "中";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(boundary.c_str()));
                editor->Send(Message::SetSel, 0, boundary.size());
                NativeSearch::Command(frame, wxID_REPLACE);
                const auto boundaryQuery = query->GetValue();
                const auto boundaryUtf8 = boundaryQuery.utf8_str();
                const std::string queryBytes(boundaryUtf8.data(), boundaryUtf8.length());
                const auto difference =
                    std::mismatch(boundary.cbegin(), boundary.cend(), queryBytes.cbegin(), queryBytes.cend());
                const auto firstDifference = static_cast<size_t>(std::distance(boundary.cbegin(), difference.first));
                const bool utf8Suffix = boundaryQuery.EndsWith(wxString::FromUTF8("中"));
                std::fprintf(stderr,
                             "Boundary selection bytes=%lld query bytes=%zu exact=%d first_difference=%zu "
                             "suffix_utf8=%d regex=%d transform=%d selection_only=%d\n",
                             static_cast<long long>(editor->Send(Message::GetSelText)), queryBytes.size(),
                             queryBytes == boundary, firstDifference, utf8Suffix, regex->GetValue(),
                             transform->GetValue(), selectionOnly_);
                Require(queryBytes.size() == 1024 * 1024 && utf8Suffix && queryBytes == boundary,
                        "selection limit must include exactly one MiB of UTF-8 bytes");
                NativeSearch::Click(dialog, wxID_CANCEL);
                const std::string tooLong = boundary + "!";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(tooLong.c_str()));
                editor->Send(Message::SetSel, 0, tooLong.size());
                if (!selectionOnly_) {
                    ClipboardGesture(editor);
                    Require(wxTheClipboard->Open(), "test must publish selection-limit fallback");
                    wxTheClipboard->SetData(new wxTextDataObject("clipboard fallback"));
                    wxTheClipboard->Close();
                }
                NativeSearch::Command(frame, wxID_REPLACE);
                Require(query->GetValue() == (selectionOnly_ ? "stored" : "clipboard fallback") && Focused(query),
                        "over-limit selection must use the enabled original fallback and focus query");
                Require(NativeSearch::Text(editor) == tooLong, "limit checks must retain original editor bytes");
                std::puts("PASS: literal punctuation/backslash and exact UTF-8 selection-length boundary");
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
    std::unique_ptr<SettingsStore> settings_;
    bool selectionOnly_ = false;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SeedingApp);
