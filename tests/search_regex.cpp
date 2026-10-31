#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/statusbr.h>
#include <wx/textctrl.h>
#include "native_dialog_helpers.h"
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Command(EditorFrame* frame, int id) {
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(frame->GetHandle()), WM_COMMAND, MAKEWPARAM(id, 0), 0);
#elif defined(__WXGTK__)
    auto* item = frame->GetMenuBar()->FindItem(id);
    Require(item, "native menu command must exist");
    gtk_menu_item_activate(GTK_MENU_ITEM(item->GetMenuItem()));
#endif
    wxYield();
}
void Toggle(wxCheckBox* option, bool checked) {
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(option->GetHandle());
    SendMessageW(handle, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(static_cast<HWND>(option->GetParent()->GetHandle()), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(handle), BN_CLICKED), reinterpret_cast<LPARAM>(handle));
#elif defined(__WXGTK__)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(option->GetHandle()), checked);
#endif
    wxYield();
}
void Click(wxWindow* dialog, int id) {
    auto* button = wxDynamicCast(dialog->FindWindow(id), wxButton);
    Require(button, "native dialog button must exist");
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(button->GetHandle());
    SendMessageW(static_cast<HWND>(dialog->GetHandle()), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(handle), BN_CLICKED),
                 reinterpret_cast<LPARAM>(handle));
#elif defined(__WXGTK__)
    gtk_button_clicked(GTK_BUTTON(button->GetHandle()));
#endif
    wxYield();
}
void Selection(EditorControl* editor, intptr_t start, intptr_t end, const char* message) {
    const auto actualStart = editor->Send(Scintilla::Message::GetSelectionStart);
    const auto actualEnd = editor->Send(Scintilla::Message::GetSelectionEnd);
    if (actualStart != start || actualEnd != end)
        std::fprintf(stderr, "Range: %lld..%lld; expected %lld..%lld\n", static_cast<long long>(actualStart),
                     static_cast<long long>(actualEnd), static_cast<long long>(start), static_cast<long long>(end));
    Require(actualStart == start && actualEnd == end, message);
}
class RegexApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                using Scintilla::Message;
                auto* editor = frame->Editor();
                Command(frame, wxID_FIND);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* regex = wxDynamicCast(wxWindow::FindWindowByName("Regular expression", frame), wxCheckBox);
                Require(query && regex, "Find must expose the native Regular expression option");
                Require(!regex->GetValue(), "Regular expression must initially be off");
                auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
                Toggle(regex, true);
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文12 中文34"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue(wxString::FromUTF8("(中文)([0-9]+)"));
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 8, "Boost capture groups must select the first Chinese match");
                Command(frame, wxID_FORWARD);
                Selection(editor, 9, 17, "Boost capture groups must select the next Chinese match");
                std::puts("PASS: native regex option and frozen Chinese capture ranges");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中\n文 中\n文"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue(wxString::FromUTF8("中\\n文"));
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 7, "explicit cross-line regex must use the frozen forward range");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 8, 15, "Next must find the second cross-line regex");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 0, 7, "Previous must navigate to the same cross-line match as Next");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 8, 15, "Previous must wrap to the final cross-line match");
                Require(frame->GetStatusBar()->GetStatusText() == "Search wrapped", "Previous must report wrapping");
                std::puts("PASS: cross-line Previous/Next and backward wrap");
                query->SetValue("[");
                editor->Send(Message::SetSel, 15, 8);
                Click(dialog, wxID_FORWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "Invalid regular expression",
                        "invalid regex must be distinct from an exhausted search");
                Require(editor->Send(Message::GetAnchor) == 15 && editor->Send(Message::GetCurrentPos) == 8,
                        "invalid regex must preserve a reversed selection");
                Click(dialog, wxID_BACKWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "Invalid regular expression",
                        "Previous must also report an invalid expression");
                query->SetValue(wxString::FromUTF8("(?<=中)\\n文"));
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 3, 7, "correcting an invalid expression must immediately recover with lookbehind");
                std::puts("PASS: distinct invalid expression, preserved selection and correction/retry");
                auto* wrap = wxDynamicCast(wxWindow::FindWindowByName("Wrap around", frame), wxCheckBox);
                Require(wrap, "Find must expose Wrap around");
                Toggle(wrap, false);
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文ab"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue("a*");
                const std::pair<intptr_t, intptr_t> zeroRanges[] = {{0, 0}, {3, 3}, {6, 7}, {7, 7}, {8, 8}};
                for (const auto& [start, end] : zeroRanges) {
                    Click(dialog, wxID_FORWARD);
                    Selection(editor, start, end, "repeated zero-length Next must follow frozen UTF-8 ranges");
                }
                Click(dialog, wxID_FORWARD);
                Selection(editor, 8, 8, "exhausted zero-length Next must preserve the end caret");
                Require(frame->GetStatusBar()->GetStatusText() == "No match found",
                        "end caret must exhaust without wrap");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 7, 7, "direction change after an empty match must advance to the previous match");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 6, 7, "Previous must retain the consuming match adjacent to an empty match");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 3, 3, "Previous must progress over a complete Chinese character");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 0, 0, "Previous must include the zero-length match at document start");
                Click(dialog, wxID_BACKWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "No match found",
                        "start caret must exhaust without wrap");
                Toggle(wrap, true);
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 8, 8, "empty-match backward wrap must select the final boundary once");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 0, "empty-match forward wrap must select the first boundary once");
                std::puts("PASS: frozen empty and consuming ranges, direction changes, UTF-8 and bounded wrap");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnExit() override { return result_; }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(RegexApp);
