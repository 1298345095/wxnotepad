#include "native_search_helpers.h"
#include "native_fold_helpers.h"
#include <SciLexer.h>
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
void WhitespaceShortcut(EditorControl* window) {
#ifdef __WXMSW__
    Require(NativeSearch::WindowsShortcut(window, '8', {{VK_CONTROL, 0x80}, {VK_SHIFT, 0x80}}),
            "the native owned window must translate Ctrl+Shift+8");
#elif defined(__WXGTK__)
    auto* widget = window->GetConnectWidget();
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(window->GTKGetDrawingWindow()));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_asterisk;
    event->key.state = GDK_CONTROL_MASK | GDK_SHIFT_MASK;
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
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
class WhitespaceApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-whitespace-" + std::to_string(wxGetProcessId()));
            try {
                auto* editor = frame->Editor();
                const int id = frame->GetMenuBar()->FindMenuItem("View", "Show Whitespace");
                Require(id != wxNOT_FOUND, "View must offer Show Whitespace");
                auto* item = frame->GetMenuBar()->FindItem(id);
                Require(item->IsCheckable() && !item->IsChecked() && editor->Send(Message::GetViewWS) == 0,
                        "a fresh window must hide whitespace and show an unchecked option");
                const std::string source = " \talpha beta\t中文  \r\n\t end \n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(source.c_str()));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSel, 9, 2);
                const auto x = editor->Send(Message::PointXFromPosition, 0, 7);
                const auto tabWidth = editor->Send(Message::GetTabWidth);
                const auto eol = editor->Send(Message::GetEOLMode);
                const auto fontSize = editor->Send(Message::StyleGetSizeFractional, 32);
                const auto tabDraw = editor->Send(Message::GetTabDrawMode);
                NativeSearch::Command(frame, id);
                Require(item->IsChecked() && editor->Send(Message::GetViewWS) == 1,
                        "native menu must show every leading, internal and trailing space and tab");
                Require(NativeSearch::Text(editor) == source && editor->Send(Message::GetAnchor) == 9 &&
                            editor->Send(Message::GetCurrentPos) == 2 && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && editor->Send(Message::GetEOLMode) == eol &&
                            editor->Send(Message::GetTabWidth) == tabWidth &&
                            editor->Send(Message::StyleGetSizeFractional, 32) == fontSize &&
                            editor->Send(Message::GetTabDrawMode) == tabDraw &&
                            editor->Send(Message::PointXFromPosition, 0, 7) == x,
                        "showing whitespace must preserve bytes, selection, save point, history and tab geometry");
                NativeSearch::Command(frame, id);
                Require(!item->IsChecked() && editor->Send(Message::GetViewWS) == 0,
                        "the same native menu must restore ordinary whitespace rendering");
                WhitespaceShortcut(editor);
                Require(item->IsChecked() && editor->Send(Message::GetViewWS) == 1,
                        "Ctrl+Shift+8 must enable the same checked whitespace option");
                WhitespaceShortcut(editor);
                Require(!item->IsChecked() && editor->Send(Message::GetViewWS) == 0,
                        "Ctrl+Shift+8 must disable the same whitespace option");
                editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                Require(editor->Send(Message::CanRedo), "the fixture must offer Redo before the view change");
                NativeSearch::Command(frame, id);
                Require(editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                        "the view change must preserve Redo and the save point");
                editor->Send(Message::Redo);
                Require(NativeSearch::Text(editor) == source + "!", "Redo must retain the original text edit");
                editor->Send(Message::Undo);
                Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify),
                        "Undo must return directly to the original save point");
                for (const auto* language : {"C/C++", "Plain Text", "Python", "JSON", "Automatic"}) {
                    const int languageId = frame->GetMenuBar()->FindMenuItem("Language", language);
                    Require(languageId != wxNOT_FOUND, "the supported language command must exist");
                    NativeSearch::Command(frame, languageId);
                    Require(editor->Send(Message::GetViewWS) == 1 && item->IsChecked(),
                            "whitespace visibility must survive every supported language and scheme transition");
                    if (std::string(language) == "Plain Text")
                        Require(editor->Send(Message::GetLexer) == SCLEX_NULL,
                                "showing whitespace must not enable a lexer for Plain Text");
                }
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("int main() {\n\t return 0;  \n}\n"));
                NativeFold::Paint(editor);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
                Require(!editor->Send(Message::GetLineVisible, 1), "the fixture must contain a collapsed block");
                NativeSearch::Command(frame, id);
                NativeSearch::Command(frame, id);
                Require(!editor->Send(Message::GetLineVisible, 1) && editor->Send(Message::GetViewWS) == 1,
                        "whitespace toggles must preserve collapsed blocks");
                editor->Send(Message::SetSavePoint);
                std::filesystem::create_directory(directory);
                const auto input = directory / "input.txt";
                const auto output = directory / "saved.cpp";
                const std::string diskBytes = std::string("\xEF\xBB\xBF") + source;
                {
                    std::ofstream file(input, std::ios::binary);
                    file << diskBytes;
                }
                Require(frame->Open(Filename(input)).status == OpenStatus::Opened,
                        "the real file operation must open the whitespace fixture");
                Require(editor->Send(Message::GetViewWS) == 1 && frame->DocumentHasBom(),
                        "Open must preserve whitespace visibility and detect the input BOM");
                Require(frame->SaveAs(Filename(output)).status == SaveStatus::Saved && Bytes(output) == diskBytes,
                        "saving with visible whitespace must preserve every disk byte including BOM and mixed EOLs");
                Require(editor->Send(Message::GetViewWS) == 1 && frame->DocumentEncoding() == FileEncoding::Utf8 &&
                            frame->DocumentHasBom() && !editor->Send(Message::GetModify),
                        "Save As and its automatic scheme change must preserve the view and document metadata");
                Require(frame->New() && editor->Send(Message::GetViewWS) == 1 && item->IsChecked(),
                        "New must keep the current window's whitespace preference");
                auto* fresh = new EditorFrame;
                Require(fresh->Editor()->Send(Message::GetViewWS) == 0,
                        "another fresh window must start with ordinary whitespace rendering");
                fresh->Destroy();
                std::puts("PASS: whitespace menu toggles original core markers without changing document or geometry");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            std::filesystem::remove_all(directory);
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
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(WhitespaceApp);
