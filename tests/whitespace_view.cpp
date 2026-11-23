#include "native_search_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include <memory>
#include <utility>
#include "native_fold_helpers.h"
#include <SciLexer.h>
#include <algorithm>
#include <cstdio>
#include <print>
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
using TestFiles::Bytes;
using TestFiles::Filename;
class WhitespaceApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        // Mixed-EOL warning choices are covered by mixed_line_ending_open.
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-whitespace");
            try {
                using enum Scintilla::Message;
                auto* editor = frame->Editor();
                const int id = frame->GetMenuBar()->FindMenuItem("View", "Show Whitespace");
                Require(id != wxNOT_FOUND, "View must offer Show Whitespace");
                auto* item = frame->GetMenuBar()->FindItem(id);
                Require(item->IsCheckable() && !item->IsChecked() && editor->Send(GetViewWS) == 0,
                        "a fresh window must hide whitespace and show an unchecked option");
                const std::string source = " \talpha beta\t中文  \r\n\t end \n";
                editor->Send(SetText, 0, reinterpret_cast<intptr_t>(source.c_str()));
                editor->Send(EmptyUndoBuffer);
                editor->Send(SetSavePoint);
                editor->Send(SetSel, 9, 2);
                const auto x = editor->Send(PointXFromPosition, 0, 7);
                const auto tabWidth = editor->Send(GetTabWidth);
                const auto eol = editor->Send(GetEOLMode);
                const auto fontSize =
                    editor->Send(StyleGetSizeFractional,
                                 static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::Default)));
                const auto tabDraw = editor->Send(GetTabDrawMode);
                NativeSearch::Command(frame, id);
                Require(item->IsChecked() && editor->Send(GetViewWS) == 1,
                        "native menu must show every leading, internal and trailing space and tab");
                Require(NativeSearch::Text(editor) == source && editor->Send(GetAnchor) == 9 &&
                            editor->Send(GetCurrentPos) == 2 && !editor->Send(GetModify) && !editor->Send(CanUndo) &&
                            editor->Send(GetEOLMode) == eol && editor->Send(GetTabWidth) == tabWidth &&
                            editor->Send(StyleGetSizeFractional, static_cast<uintptr_t>(std::to_underlying(
                                                                     Scintilla::StylesCommon::Default))) == fontSize &&
                            editor->Send(GetTabDrawMode) == tabDraw && editor->Send(PointXFromPosition, 0, 7) == x,
                        "showing whitespace must preserve bytes, selection, save point, history and tab geometry");
                NativeSearch::Command(frame, id);
                Require(!item->IsChecked() && editor->Send(GetViewWS) == 0,
                        "the same native menu must restore ordinary whitespace rendering");
                WhitespaceShortcut(editor);
                Require(item->IsChecked() && editor->Send(GetViewWS) == 1,
                        "Ctrl+Shift+8 must enable the same checked whitespace option");
                WhitespaceShortcut(editor);
                Require(!item->IsChecked() && editor->Send(GetViewWS) == 0,
                        "Ctrl+Shift+8 must disable the same whitespace option");
                editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Undo);
                Require(editor->Send(CanRedo), "the fixture must offer Redo before the view change");
                NativeSearch::Command(frame, id);
                Require(editor->Send(CanRedo) && !editor->Send(GetModify),
                        "the view change must preserve Redo and the save point");
                editor->Send(Redo);
                Require(NativeSearch::Text(editor) == source + "!", "Redo must retain the original text edit");
                editor->Send(Undo);
                Require(NativeSearch::Text(editor) == source && !editor->Send(GetModify),
                        "Undo must return directly to the original save point");
                for (const auto* language : {"C/C++", "Plain Text", "Python", "JSON", "Automatic"}) {
                    const int languageId = frame->GetMenuBar()->FindMenuItem("Scheme", language);
                    Require(languageId != wxNOT_FOUND, "the supported language command must exist");
                    NativeSearch::Command(frame, languageId);
                    Require(editor->Send(GetViewWS) == 1 && item->IsChecked(),
                            "whitespace visibility must survive every supported language and scheme transition");
                    if (std::string(language) == "Plain Text")
                        Require(editor->Send(GetLexer) == SCLEX_NULL,
                                "showing whitespace must not enable a lexer for Plain Text");
                }
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
                editor->Send(SetText, 0, reinterpret_cast<intptr_t>("int main() {\n\t return 0;  \n}\n"));
                NativeFold::Paint(editor);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "All Levels"));
                Require(!editor->Send(GetLineVisible, 1), "the fixture must contain a collapsed block");
                NativeSearch::Command(frame, id);
                NativeSearch::Command(frame, id);
                Require(!editor->Send(GetLineVisible, 1) && editor->Send(GetViewWS) == 1,
                        "whitespace toggles must preserve collapsed blocks");
                editor->Send(SetSavePoint);
                std::filesystem::create_directory(directory);
                const auto input = directory / "input.txt";
                const auto output = directory / "saved.cpp";
                const std::string diskBytes = std::string("\x{EF}\x{BB}\x{BF}") + source;
                {
                    std::ofstream file(input, std::ios::binary);
                    file << diskBytes;
                }
                Require(frame->Open(Filename(input)).status == OpenStatus::Opened,
                        "the real file operation must open the whitespace fixture");
                Require(editor->Send(GetViewWS) == 1 && frame->DocumentHasBom(),
                        "Open must preserve whitespace visibility and detect the input BOM");
                Require(frame->SaveAs(Filename(output)).status == SaveStatus::Saved && Bytes(output) == diskBytes,
                        "saving with visible whitespace must preserve every disk byte including BOM and mixed EOLs");
                Require(editor->Send(GetViewWS) == 1 && frame->DocumentEncoding() == FileEncoding::Utf8 &&
                            frame->DocumentHasBom() && !editor->Send(GetModify),
                        "Save As and its automatic scheme change must preserve the view and document metadata");
                Require(frame->NewDocument() && editor->Send(GetViewWS) == 1 && item->IsChecked(),
                        "New must keep the current window's whitespace preference");
                auto* fresh = new EditorFrame(settings_.get());
                Require(fresh->Editor()->Send(GetViewWS) == 0,
                        "another fresh window must start with ordinary whitespace rendering");
                fresh->Destroy();
                std::puts("PASS: whitespace menu toggles original core markers without changing document or geometry");
            } catch (const std::exception& error) {
                std::println(stderr, "FAIL: {}", error.what());
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(WhitespaceApp);
