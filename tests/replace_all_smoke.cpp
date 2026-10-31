#include "editor_control.h"
#include "editor_frame.h"
#include "native_dialog_helpers.h"
#include <wx/wx.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/statusbr.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Command(EditorFrame* frame, int id) {
    auto* item = frame->GetMenuBar()->FindItem(id);
    Require(item, "native menu command must exist");
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(frame->GetHandle()), WM_COMMAND, MAKEWPARAM(id, 0), 0);
#elif defined(__WXGTK__)
    gtk_menu_item_activate(GTK_MENU_ITEM(item->GetMenuItem()));
#endif
    wxYield();
}
void Click(wxWindow* parent, int id) {
    auto* button = wxDynamicCast(parent->FindWindow(id), wxButton);
    Require(button && button->IsShown(), "Replace view must expose Replace All");
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(parent->GetHandle()), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(static_cast<HWND>(button->GetHandle())), BN_CLICKED),
                 reinterpret_cast<LPARAM>(button->GetHandle()));
#elif defined(__WXGTK__)
    gtk_button_clicked(GTK_BUTTON(button->GetHandle()));
#endif
    wxYield();
}
void NativeF3(wxWindow* window) {
#ifdef __WXMSW__
    PostMessageW(static_cast<HWND>(window->GetHandle()), WM_KEYDOWN, VK_F3, 0);
    PostMessageW(static_cast<HWND>(window->GetHandle()), WM_KEYUP, VK_F3, 0);
#elif defined(__WXGTK__)
    auto* widget = GTK_WIDGET(window->GetHandle());
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_F3;
    event->key.state = static_cast<GdkModifierType>(0);
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
std::string Text(EditorControl* editor) {
    const auto length = static_cast<size_t>(editor->Send(Scintilla::Message::GetLength));
    std::string text(length + 1, '\0');
    editor->Send(Scintilla::Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
wxString Filename(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
class ReplaceAllApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                using Scintilla::Message;
                auto* editor = frame->Editor();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one ONE one"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSel, 11, 8);
                Command(frame, wxID_REPLACE);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                Require(query && replacement, "Replace menu must expose query and replacement");
                auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
                query->SetValue("one");
                replacement->SetValue("oneone");
                editor->Send(Message::GotoPos, 0);
                NativeF3(query);
                Require(editor->Send(Message::GetSelectionStart) == 0 && editor->Send(Message::GetSelectionEnd) == 3,
                        "native F3 must search from the modeless Replace view");
                editor->Send(Message::SetSel, 11, 8);
                Click(dialog, wxID_REPLACE_ALL);
                Require(Text(editor) == "oneone oneone oneone",
                        "Replace All must visit original matches once across the full document");
                auto* result = wxDynamicCast(wxWindow::FindWindowByName("Replacement result", dialog), wxStaticText);
                Require(result && result->GetLabel() == "3 replacements", "Replace All must report its exact count");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == "one ONE one" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                        "one Undo must restore all bytes and the original savepoint");
                Command(frame, wxID_REDO);
                Require(Text(editor) == "oneone oneone oneone" && editor->Send(Message::GetModify),
                        "one Redo must restore the complete replacement");
                std::puts("PASS: native full-document Replace All, exact count and grouped Undo/Redo");
                const auto replace = [&](const char* before, const char* find, const char* with, const char* expected,
                                         const char* count) {
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(before));
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    editor->Send(Message::SetSel, editor->Send(Message::GetLength), 0);
                    query->SetValue(wxString::FromUTF8(find));
                    replacement->SetValue(wxString::FromUTF8(with));
                    Click(dialog, wxID_REPLACE_ALL);
                    Require(Text(editor) == expected, "Replace All must produce the explicit literal result");
                    Require(result->GetLabel() == count, "Replace All must count original non-overlapping matches");
                    Command(frame, wxID_UNDO);
                    Require(Text(editor) == before && !editor->Send(Message::GetModify) &&
                                !editor->Send(Message::CanUndo),
                            "one Undo must restore this full replacement");
                    Command(frame, wxID_REDO);
                    Require(Text(editor) == expected && editor->Send(Message::GetModify),
                            "one Redo must restore this full replacement");
                };
                replace("aaaaa", "aa", "X", "XXa", "2 replacements");
                replace("aaaa", "aa", "", "", "2 replacements");
                replace("abcabcabc", "abc", "x", "xxx", "3 replacements");
                replace("[a].*[a].*", "[a].*", "$1\\tail", "$1\\tail$1\\tail", "2 replacements");
                replace("中文😀中文😀", "中文😀", "好", "好好", "2 replacements");
                replace("Straße STRASSE", "STRASSE", "Straße", "Straße Straße", "2 replacements");
                std::puts(
                    "PASS: adjacent/overlapping matches, growth, shrink, deletion, literal punctuation and Unicode");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one one one"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 11);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                Command(frame, wxID_UNDO);
                editor->Send(Message::SetSel, 11, 8);
                const auto unchanged = [&](const char* find, const char* with, const char* count) {
                    query->SetValue(find);
                    replacement->SetValue(with);
                    Click(dialog, wxID_REPLACE_ALL);
                    Require(Text(editor) == "one one one" && !editor->Send(Message::GetModify) &&
                                !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                            "no-op Replace All must preserve savepoint, undo and redo history");
                    Require(editor->Send(Message::GetAnchor) == 11 && editor->Send(Message::GetCurrentPos) == 8,
                            "no-op Replace All must preserve selection direction and byte range");
                    Require(result->GetLabel() == count, "no-op Replace All must report its exact count");
                };
                unchanged("missing", "X", "0 replacements");
                unchanged("one", "one", "3 replacements");
                unchanged("", "X", "0 replacements");
                Require(frame->New(), "New must succeed after a no-op replacement");
                query->SetValue("one");
                Click(dialog, wxID_REPLACE_ALL);
                Require(Text(editor).empty() && !editor->Send(Message::CanUndo) &&
                            result->GetLabel() == "0 replacements",
                        "Replace All must use the live empty document after New");
                std::puts("PASS: zero matches, byte-identical matches, empty query and New preserve editing history");
                const auto path = std::filesystem::temp_directory_path() /
                                  ("wxnotepad-replace-all-" + std::to_string(wxGetProcessId()) + ".txt");
                const unsigned char before[] = {0xff, 0xfe, 'o',  0, 'n', 0, 'e', 0, '\r', 0, '\n', 0, 'o',  0, 'n', 0,
                                                'e',  0,    '\n', 0, 'l', 0, 'a', 0, 's',  0, 't',  0, '\r', 0};
                {
                    std::ofstream file(path, std::ios::binary);
                    file.write(reinterpret_cast<const char*>(before), sizeof before);
                }
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened, "UTF-16LE fixture must open");
                query->SetValue("one");
                replacement->SetValue(wxString::FromUTF8("好"));
                Click(dialog, wxID_REPLACE_ALL);
                Require(Text(editor) == "好\r\n好\nlast\r" && result->GetLabel() == "2 replacements",
                        "Replace All must preserve untouched mixed newline bytes");
                Command(frame, wxID_SAVE);
                const unsigned char expected[] = {0xff, 0xfe, 0x7d, 0x59, '\r', 0,   '\n', 0,   0x7d, 0x59, '\n',
                                                  0,    'l',  0,    'a',  0,    's', 0,    't', 0,    '\r', 0};
                const std::string bytes = [&] {
                    std::ifstream file(path, std::ios::binary);
                    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                }();
                Require(bytes == std::string(reinterpret_cast<const char*>(expected), sizeof expected) &&
                            frame->DocumentEncoding() == FileEncoding::Utf16Le && frame->DocumentHasBom() &&
                            !editor->Send(Message::GetModify),
                        "Save must preserve UTF-16LE, BOM, mixed EOL and exact replaced bytes");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == "one\r\none\nlast\r" && editor->Send(Message::GetModify),
                        "Undo after Save must restore original text away from the new savepoint");
                Command(frame, wxID_REDO);
                Require(Text(editor) == "好\r\n好\nlast\r" && !editor->Send(Message::GetModify),
                        "Redo after Save must restore exact text and the new savepoint");
                std::filesystem::remove(path);
                std::puts("PASS: Save preserves independent UTF-16LE/BOM/mixed-EOL bytes and Undo/Redo savepoints");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
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
wxIMPLEMENT_APP(ReplaceAllApp);
