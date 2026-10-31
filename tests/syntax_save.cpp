#include "editor_control.h"
#include "editor_frame.h"
#include <SciLexer.h>
#include <wx/wx.h>
#include <wx/statusbr.h>
#include <wx/timer.h>
#include "native_dialog_helpers.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
wxString Filename(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open(), "saved file must exist");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::string Text(EditorControl* editor) {
    const auto length = static_cast<size_t>(editor->Send(Message::GetLength));
    std::string text(length + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
void Paint(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto visiblePrefix = std::min(editor->Send(Message::GetLength), intptr_t{128});
    // Observe the normal style pass, including asynchronous Wayland frames.
    for (int attempt = 0; attempt < 100; ++attempt) {
        wxYield();
        editor->Refresh();
        editor->Update();
        wxYield();
        if (editor->Send(Message::GetEndStyled) >= visiblePrefix) return;
        wxMilliSleep(10);
    }
}
void Command(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(event), "real editor command must be handled");
    Paint(frame);
}
// Drive only modal widgets owned by this test, without global input or focus.
class SaveChooser final : public wxTimer {
public:
    explicit SaveChooser(const wxString& path = {}) : path_(path) { Start(150); }
    int choosers = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<SaveChooser*>(data);
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                if (IsWindowVisible(window) && (wxString(title) == "Save file" || wxString(title) == "Save As") &&
                    !driver.stage_++) {
                    ++driver.choosers;
                    NativeDialogs::ChooseFile(window, driver.path_);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget) || !GTK_IS_FILE_CHOOSER(widget)) continue;
            if (!stage_++) {
                ++choosers;
                NativeDialogs::ChooseFile(widget, path_);
            } else if (stage_ == 2 && !path_.empty())
                NativeDialogs::Respond(widget, GTK_RESPONSE_ACCEPT);
        }
        g_list_free(windows);
#endif
    }

private:
    wxString path_;
    int stage_ = 0;
};
void SaveCommand(EditorFrame* frame, int id, const wxString& path = {}) {
    SaveChooser chooser(path);
    Command(frame, id);
    chooser.Stop();
    Require(chooser.choosers == 1, "real save command must use its native chooser");
}
void Language(EditorFrame* frame, bool cpp) {
    Paint(frame);
    Require(frame->Editor()->Send(Message::GetStyleIndexAt, 0) == (cpp ? SCE_C_WORD2 : 0),
            "saved extension must select the expected public syntax style");
    Require(frame->GetStatusBar()->GetStatusText().Contains(cpp ? "C/C++" : "Plain Text"),
            "status must follow the effective language");
}
class SyntaxSaveApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-syntax-save-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto cpp = directory / "first.CpP";
                const auto plain = directory / "plain.txt";
                const auto back = directory / "back.cpp";
                auto* editor = frame->Editor();
                const std::string source = "int value; // 中文\r\nint next;\n";
                editor->Send(Message::AddText, source.size(), reinterpret_cast<intptr_t>(source.data()));
                editor->Send(Message::SetSel, 7, 2);
                const auto eol = editor->Send(Message::GetEOLMode);
                SaveCommand(frame, wxID_SAVE, Filename(cpp));
                Language(frame, true);
                Require(frame->DocumentPath() == Filename(cpp) && Bytes(cpp) == source && Text(editor) == source &&
                            !editor->Send(Message::GetModify) && editor->Send(Message::CanUndo) &&
                            editor->Send(Message::GetAnchor) == 7 && editor->Send(Message::GetCurrentPos) == 2 &&
                            editor->Send(Message::GetEOLMode) == eol && frame->DocumentEncoding() == FileEncoding::Utf8,
                        "first Save must preserve text, selection, EOL, encoding and history at its save point");
                Command(frame, wxID_UNDO);
                Require(Text(editor).empty() && editor->Send(Message::GetModify),
                        "one Undo after first Save must undo the original insertion, not syntax");
                Command(frame, wxID_REDO);
                Require(Text(editor) == source && !editor->Send(Message::GetModify),
                        "Redo must restore the saved text and save point");
                std::puts("PASS: native first Save selects C++ without an extra history action");

                // Load BOM/mixed-EOL bytes before the extension transitions.
                const std::string bytes = "\xef\xbb\xbf" + source;
                std::ofstream(cpp, std::ios::binary) << bytes;
                Require(frame->Open(Filename(cpp)).status == OpenStatus::Opened, "BOM fixture must open");
                editor->Send(Message::GotoPos, source.size());
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("x"));
                Command(frame, wxID_UNDO);
                editor->Send(Message::SetSel, 8, 1);
                const auto loadedEol = editor->Send(Message::GetEOLMode);
                SaveCommand(frame, wxID_SAVEAS, Filename(plain));
                Language(frame, false);
                Require(Bytes(plain) == bytes && Bytes(cpp) == bytes && Text(editor) == source &&
                            frame->DocumentHasUtf8Bom() && editor->Send(Message::CanRedo) &&
                            !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::GetAnchor) == 8 && editor->Send(Message::GetCurrentPos) == 1 &&
                            editor->Send(Message::GetEOLMode) == loadedEol,
                        "C++ to Plain Text Save As must preserve BOM/EOL, selection and the pending redo branch");
                SaveCommand(frame, wxID_SAVEAS, Filename(back));
                Language(frame, true);
                Require(Bytes(back) == bytes && editor->Send(Message::CanRedo) && !editor->Send(Message::CanUndo),
                        "Plain Text to C++ Save As must retain exact bytes and redo history");
                Command(frame, wxID_REDO);
                Require(Text(editor) == source + "x" && editor->Send(Message::GetModify),
                        "Save As must not discard pending Redo");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == source && !editor->Send(Message::GetModify),
                        "Undo must return to the new saved path's save point");
                std::puts("PASS: native Save As switches both directions and preserves BOM, mixed EOL and Redo");

                editor->Send(Message::SetSel, 9, 3);
                editor->Send(Message::StyleSetFore, SCE_C_WORD2, 0x123456);
                Command(frame, wxID_SAVE);
                Require(editor->Send(Message::StyleGetFore, SCE_C_WORD2) == 0x123456 &&
                            editor->Send(Message::GetAnchor) == 9 && editor->Send(Message::GetCurrentPos) == 3,
                        "ordinary Save to the same path must not reinstall the scheme or move selection");
                Require(frame->SaveAs(Filename(back)).status == SaveStatus::Saved &&
                            editor->Send(Message::StyleGetFore, SCE_C_WORD2) == 0x123456,
                        "successful Save As to the unchanged path must not reinstall the scheme");
                SaveCommand(frame, wxID_SAVEAS);
                Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                            frame->SaveAs(Filename(directory / "missing" / "failed.txt")).status ==
                                SaveStatus::Failed &&
                            frame->DocumentPath() == Filename(back) && Bytes(back) == bytes && Text(editor) == source &&
                            frame->DocumentHasUtf8Bom() && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::CanRedo) && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::GetAnchor) == 9 && editor->Send(Message::GetCurrentPos) == 3 &&
                            editor->Send(Message::StyleGetFore, SCE_C_WORD2) == 0x123456,
                        "chooser cancellation and write failure must preserve path, scheme and document state");
                Language(frame, true);
                std::puts("PASS: unchanged-path saves, cancelled chooser and failed write preserve scheme and state");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            std::filesystem::remove_all(directory);
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
wxIMPLEMENT_APP(SyntaxSaveApp);
