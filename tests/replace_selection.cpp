#include "app/editor_frame.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// Recognize and dismiss only this test process's native rectangular-selection warning.
class RectangleWarning final : public wxTimer {
public:
    RectangleWarning() { Start(100); }
    ~RectangleWarning() override { Stop(); }
    bool observed = false;
    bool correctText = false;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<RectangleWarning*>(data);
                wchar_t title[128]{};
                GetWindowTextW(window, title, 128);
                if (!IsWindowVisible(window) || wxString(title) != "wxnotepad") return TRUE;
                driver.observed = true;
                SendMessageW(window, WM_USER + 102, IDOK, 0); // TDM_CLICK_BUTTON
                driver.Stop();
                return FALSE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget) || !GTK_IS_MESSAGE_DIALOG(widget) ||
                wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) != "wxnotepad")
                continue;
            observed = true;
            gchar* text = nullptr;
            g_object_get(widget, "text", &text, nullptr);
            correctText =
                text && wxString::FromUTF8(text) == "This operation can't be perfomed within a rectangular selection.";
            g_free(text);
            NativeDialogs::Respond(widget, GTK_RESPONSE_OK);
            Stop();
        }
        g_list_free(windows);
#endif
    }
};
class SelectionReplaceApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                NativeSearch::Command(frame, wxID_REPLACE);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                auto* button = wxDynamicCast(wxWindow::FindWindowByName("Replace in selection", frame), wxButton);
                Require(query && replacement && button, "Replace exposes the original In Selection operation");
                auto* dialog = query->GetParent();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one one one"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSel, 4, 7);
                query->SetValue("one");
                replacement->SetValue("two two");
                NativeSearch::Click(dialog, button->GetId());
                Require(NativeSearch::Text(editor) == "one two two one", "only the original selected match grows");
                NativeSearch::Selection(editor, 4, 11, "selection follows the growing replacement");
                NativeSearch::Command(frame, wxID_UNDO);
                Require(NativeSearch::Text(editor) == "one one one" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "one Undo restores the selection replacement");
                std::puts("PASS: original selection scope, growth and one-operation Undo");
                auto* regex = wxDynamicCast(wxWindow::FindWindowByName("Regular expression", frame), wxCheckBox);
                auto* result = wxDynamicCast(wxWindow::FindWindowByName("Replacement result", dialog), wxStaticText);
                Require(regex && result, "selection replacement shares regex options and count feedback");
                const auto example = [&](const char* before, intptr_t anchor, intptr_t caret, const char* pattern,
                                         const char* with, bool regularExpression, const char* expected, size_t count,
                                         intptr_t selectedStart, intptr_t selectedEnd) {
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(before));
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    editor->Send(Message::SetSel, anchor, caret);
                    if (regex->GetValue() != regularExpression) NativeSearch::Toggle(regex, regularExpression);
                    query->SetValue(wxString::FromUTF8(pattern));
                    replacement->SetValue(wxString::FromUTF8(with));
                    NativeSearch::Click(dialog, button->GetId());
                    if (NativeSearch::Text(editor) != expected ||
                        result->GetLabel() != wxString::Format("%zu replacements", count))
                        std::fprintf(stderr, "Pattern %s: [%s] / %s; expected [%s] / %zu\n", pattern,
                                     NativeSearch::Text(editor).c_str(), result->GetLabel().utf8_str().data(), expected,
                                     count);
                    Require(NativeSearch::Text(editor) == expected &&
                                result->GetLabel() == wxString::Format("%zu replacements", count),
                            "selection replacement produces the pinned bytes and count");
                    NativeSearch::Selection(editor, selectedStart, selectedEnd, "selection range is repaired");
                    Require((editor->Send(Message::GetAnchor) > editor->Send(Message::GetCurrentPos)) ==
                                (anchor > caret),
                            "selection replacement preserves selection direction");
                    if (std::string(before) != expected) {
                        NativeSearch::Command(frame, wxID_UNDO);
                        Require(NativeSearch::Text(editor) == before && !editor->Send(Message::GetModify) &&
                                    !editor->Send(Message::CanUndo),
                                "one Undo restores every selected edit");
                        NativeSearch::Command(frame, wxID_REDO);
                        Require(NativeSearch::Text(editor) == expected, "one Redo restores every selected edit");
                    } else {
                        Require(!editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                                "unchanged selection replacements retain history and save point");
                    }
                };
                example("a a a a", 5, 2, "a", "XX", false, "a XX XX a", 2, 4, 7);
                example("中\r\n文 中\r\n文 中\r\n文", 0, 17, "文", "X", false, "中\r\nX 中\r\nX 中\r\n文", 2, 0, 13);
                example("中\r\n文 中\r\n文 中\r\n文", 9, 17, "(中)\\r\\n(文)", "$2:$1", true, "中\r\n文 文:中 中\r\n文",
                        1, 9, 16);
                example("x 中文12 中文34 z", 19, 2, "(中文)([0-9]+)", "$2:$1", true, "x 12:中文 34:中文 z", 2, 11, 21);
                example("aa aa aa", 3, 5, "aa", "", false, "aa  aa", 1, 3, 3);
                example("abZ", 0, 2, "bZ|b", "X", true, "abZ", 0, 0, 2);
                example("abZ", 0, 2, "(?=Z)", "_", true, "ab_Z", 1, 0, 3);
                example("中中Z", 0, 6, "(?=中)", "_", true, "_中_中Z", 2, 1, 8);
                example("a\nb\nz", 0, 4, "^", "", true, "a\nb\nz", 3, 0, 4);
                example("a\nb\nz", 0, 4, "^", ">", true, ">a\n>b\n>z", 3, 1, 7);
                example("one one", 0, 3, "one", "one", false, "one one", 1, 0, 3);
                example("one one", 0, 3, "missing", "X", false, "one one", 0, 0, 3);
                example("one one", 3, 3, "one", "X", false, "one one", 0, 3, 3);
                example("abZ", 2, 0, "(?=Z)", "_", true, "ab_Z", 1, 0, 3);
                example("ab", 1, 1, "(?=b)", "_", true, "a_b", 1, 1, 2);
                std::puts("PASS: multiline/Unicode, growth/shrink, reversed and empty ranges, end boundary and "
                          "zero-width scope");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one one"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::InsertText, 7, reinterpret_cast<intptr_t>("!"));
                NativeSearch::Command(frame, wxID_UNDO);
                editor->Send(Message::SetSel, 3, 0);
                if (!regex->GetValue()) NativeSearch::Toggle(regex, true);
                for (const auto* pattern : {"missing", "(one)", "["}) {
                    query->SetValue(pattern);
                    replacement->SetValue("$&");
                    NativeSearch::Click(dialog, button->GetId());
                    Require(NativeSearch::Text(editor) == "one one" && !editor->Send(Message::GetModify) &&
                                !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                                editor->Send(Message::GetAnchor) == 3 && editor->Send(Message::GetCurrentPos) == 0,
                            "no-match, no-op and invalid selection replacements retain Redo and direction");
                }
                Require(result->GetLabel() == "Invalid regular expression",
                        "invalid selection pattern reports its error");
                query->SetValue("");
                Require(!button->IsEnabled(), "In Selection is disabled when find text is empty");
                query->SetValue("x");
                Require(button->IsEnabled(), "In Selection stays enabled for empty stream selections");
                NativeSearch::Command(frame, wxID_FIND);
                Require(!button->IsShown(), "Find view hides replacement-only operation");
                NativeSearch::Command(frame, wxID_REPLACE);
                Require(button->IsShown(), "Replace view restores In Selection");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("a\nb\nc"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
                editor->Send(Message::SetRectangularSelectionAnchor, 0);
                editor->Send(Message::SetRectangularSelectionCaret, 3);
                query->SetValue("[");
                const wxString previousResult = result->GetLabel();
                {
                    RectangleWarning warning;
                    Require(button->IsEnabled(), "rectangular selection does not disable the original operation");
                    NativeSearch::Click(dialog, button->GetId());
                    Require(warning.observed, "rectangular selection produces a native warning");
#ifdef __WXGTK__
                    Require(warning.correctText, "native warning retains original unsupported-selection text");
#endif
                }
                Require(
                    NativeSearch::Text(editor) == "a\nb\nc" && !editor->Send(Message::CanUndo) &&
                        !editor->Send(Message::GetModify) && editor->Send(Message::SelectionIsRectangle) &&
                        result->GetLabel() == previousResult,
                    "rectangle rejection precedes invalid-pattern feedback and retains document, selection and count");
                editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
                std::puts("PASS: native rectangular-selection warning without edits");

                const auto path = std::filesystem::temp_directory_path() /
                                  ("wxnotepad-selection-" + std::to_string(wxGetProcessId()) + ".txt");
                {
                    std::ofstream file(path, std::ios::binary);
                    file << "\xef\xbb\xbf"
                            "中文12\r\n中文34\n中文56\r";
                }
#ifdef __WXMSW__
                const wxString filename(path.wstring());
#else
                const wxString filename = wxString::FromUTF8(path.string());
#endif
                Require(frame->Open(filename).status == OpenStatus::Opened, "selected disk fixture opens");
                editor->Send(Message::SetSel, 10, 18);
                if (!regex->GetValue()) NativeSearch::Toggle(regex, true);
                query->SetValue(wxString::FromUTF8("(中文)([0-9]+)"));
                replacement->SetValue("$2:$1:$2");
                NativeSearch::Click(dialog, button->GetId());
                NativeSearch::Command(frame, wxID_SAVE);
                {
                    std::ifstream file(path, std::ios::binary);
                    const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
                    Require(bytes == "\xef\xbb\xbf"
                                     "中文12\r\n34:中文:34\n中文56\r",
                            "Save preserves independent BOM, mixed EOL and outside-selection bytes");
                }
                Require(frame->Open(filename).status == OpenStatus::Opened &&
                            NativeSearch::Text(editor) == "中文12\r\n34:中文:34\n中文56\r",
                        "saved selection replacement reopens with exact bytes");
                editor->Send(Message::SetSel, 10, 22);
                query->SetValue(wxString::FromUTF8("([0-9]+):中文:\\1"));
                replacement->SetValue("X");
                NativeSearch::Click(dialog, button->GetId());
                NativeSearch::Command(frame, wxID_SAVE);
                {
                    std::ifstream file(path, std::ios::binary);
                    const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
                    Require(bytes == "\xef\xbb\xbf"
                                     "中文12\r\nX\n中文56\r",
                            "Save writes exact shrinking replacement bytes inside the original range");
                }
                std::filesystem::remove(path);
                std::puts("PASS: native enablement and independent saved UTF-8/BOM/mixed-EOL bytes");
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
wxIMPLEMENT_APP(SelectionReplaceApp);
