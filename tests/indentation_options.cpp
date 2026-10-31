#include "native_search_helpers.h"
#include "native_fold_helpers.h"
#include "file_helpers.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {
using Scintilla::Message;
using TestFiles::Bytes;
using TestFiles::Filename;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int Option(EditorFrame* frame, const wxString& label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    Require(id != wxNOT_FOUND, "Edit must offer the indentation option");
    return id;
}
void Tab(EditorControl* editor) {
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYDOWN, VK_TAB, 0);
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYUP, VK_TAB, 0);
#elif defined(__WXGTK__)
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_Tab;
    event->key.state = static_cast<GdkModifierType>(0);
    gtk_widget_event(editor->GetConnectWidget(), event);
    gdk_event_free(event);
#endif
    wxYield();
}
void CheckOptions(EditorFrame* frame) {
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    auto* editor = frame->Editor();
    for (const auto* label : {"Tabs", "Spaces"}) {
        auto* item = frame->GetMenuBar()->FindItem(Option(frame, label));
        Require(item->IsRadio() &&
                    item->IsChecked() == ((editor->Send(Message::GetUseTabs) != 0) == (wxString(label) == "Tabs")),
                "Tabs and Spaces must exclusively reflect the core's effective policy");
    }
    for (const int width : {2, 4, 8}) {
        auto* item = frame->GetMenuBar()->FindItem(Option(frame, wxString::Format("%d Columns", width)));
        Require(item->IsRadio() && item->IsChecked() == (editor->Send(Message::GetTabWidth) == width),
                "width choices must exclusively reflect the core's effective tab stops");
    }
}
class IndentationApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto* evidence = std::getenv("WXNOTEPAD_INDENTATION_EVIDENCE");
            const auto directory =
                (evidence ? std::filesystem::path(evidence) : std::filesystem::temp_directory_path()) /
                ("wxnotepad-indentation-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directories(directory);
                auto* editor = frame->Editor();
                Require(editor->Send(Message::GetUseTabs) == 1 && editor->Send(Message::GetTabWidth) == 8 &&
                            editor->Send(Message::GetIndent) == 0,
                        "a fresh window must preserve original core startup indentation");
                CheckOptions(frame);
                NativeSearch::Command(frame, Option(frame, "Spaces"));
                Require(editor->Send(Message::GetUseTabs) == 0, "Spaces must change the live editor policy");
                CheckOptions(frame);
                NativeSearch::Command(frame, Option(frame, "Tabs"));
                Require(editor->Send(Message::GetUseTabs) == 1, "Tabs must change the live editor policy");
                CheckOptions(frame);
                std::puts("PASS: indentation menu reflects and changes the original core policy");
                for (const int width : {2, 4, 8}) {
                    NativeSearch::Command(frame, Option(frame, wxString::Format("%d Columns", width)));
                    Require(editor->Send(Message::GetTabWidth) == width && editor->Send(Message::GetIndent) == width,
                            "chosen width must control both live tab stops and indentation steps");
                    for (const bool tabs : {true, false}) {
                        NativeSearch::Command(frame, Option(frame, tabs ? "Tabs" : "Spaces"));
                        CheckOptions(frame);
                        editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(" x\nabc"));
                        editor->Send(Message::GotoPos, 1);
                        Tab(editor);
                        const std::string leading = tabs ? "\tx\nabc" : std::string(width, ' ') + "x\nabc";
                        Require(NativeSearch::Text(editor) == leading,
                                "native Tab in leading whitespace must follow the selected policy and stop");
                        editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
                        Tab(editor);
                        const std::string expected = leading + (tabs ? "\t" : width == 8 ? "     " : " ");
                        Require(NativeSearch::Text(editor) == expected,
                                "native Tab after text must insert a tab or spaces to the next selected stop");
                        const auto path = directory / (std::to_string(width) + (tabs ? "-tabs.txt" : "-spaces.txt"));
                        Require(frame->SaveAs(Filename(path)).status == SaveStatus::Saved && Bytes(path) == expected,
                                "independent saved bytes must retain literal tabs or the expected number of spaces");
                    }
                }
                std::puts("PASS: all six choices drive native Tab input and exact saved bytes");
                frame->SetClientSize(420, 260);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Word Wrap"));
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Show Whitespace"));
                NativeSearch::Command(frame, Option(frame, "2 Columns"));
                std::string wrapped;
                for (int index = 0; index < 120; ++index)
                    wrapped += "x\t";
                wrapped += "end";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(wrapped.c_str()));
                editor->Send(Message::SetSel, 3, static_cast<intptr_t>(wrapped.size()));
                NativeSearch::Command(frame, Option(frame, "8 Columns"));
                const auto y = editor->Send(Message::PointYFromPosition, 0, static_cast<intptr_t>(wrapped.size()));
                Require(editor->Send(Message::GetAnchor) == 3 &&
                            editor->Send(Message::GetCurrentPos) == static_cast<intptr_t>(wrapped.size()) && y >= 0 &&
                            y + editor->Send(Message::TextHeight, 0) <= editor->GetClientSize().y,
                        "changing width must retain logical selection and reveal the caret after rewrapping");
                std::puts("PASS: width changes rewrap tabs and keep the unchanged caret visible");
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 3, static_cast<intptr_t>(wrapped.size()));
                const auto font = editor->Send(Message::StyleGetSizeFractional, 32);
                const auto eol = editor->Send(Message::GetEOLMode);
                for (const auto* label : {"Spaces", "2 Columns", "Tabs", "4 Columns", "8 Columns"}) {
                    NativeSearch::Command(frame, Option(frame, label));
                    Require(NativeSearch::Text(editor) == wrapped && !editor->Send(Message::GetModify) &&
                                !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                                editor->Send(Message::GetAnchor) == 3 &&
                                editor->Send(Message::GetCurrentPos) == static_cast<intptr_t>(wrapped.size()) &&
                                editor->Send(Message::StyleGetSizeFractional, 32) == font &&
                                editor->Send(Message::GetEOLMode) == eol && editor->LineNumbersVisible() &&
                                editor->Send(Message::GetViewWS) == 1 && editor->Send(Message::GetWrapMode) != 0,
                            "options alone must preserve text, save point, pending Redo, logical selection, font and "
                            "views");
                }
                editor->Send(Message::Redo);
                Require(NativeSearch::Text(editor) == wrapped + "!", "pending Redo must retain the original edit");
                editor->Send(Message::Undo);
                Require(!editor->Send(Message::GetModify), "Undo after option changes must return to the save point");
                std::puts("PASS: options preserve text, pending Redo, save point, selection, font and views");

                NativeSearch::Command(frame, Option(frame, "Spaces"));
                NativeSearch::Command(frame, Option(frame, "4 Columns"));
                const auto input = directory / "bom.txt";
                const auto output = directory / "saved.py";
                const std::string disk = std::string("\xEF\xBB\xBF") + " \talpha 中文\r\n";
                {
                    std::ofstream file(input, std::ios::binary);
                    file << disk;
                }
                Require(frame->Open(Filename(input)).status == OpenStatus::Opened && frame->DocumentHasBom(),
                        "the real Open operation must detect the input BOM");
                for (const auto* language : {"C/C++", "Python", "JSON", "Plain Text", "Automatic"}) {
                    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", language));
                    Require(editor->Send(Message::GetUseTabs) == 0 && editor->Send(Message::GetTabWidth) == 4 &&
                                editor->Send(Message::GetIndent) == 4,
                            "Open and language changes must retain the window's chosen indentation");
                    CheckOptions(frame);
                }
                NativeSearch::Command(frame, Option(frame, "2 Columns"));
                NativeSearch::Command(frame, Option(frame, "4 Columns"));
                Require(frame->DocumentEncoding() == FileEncoding::Utf8 && frame->DocumentHasBom() &&
                            !editor->Send(Message::GetModify) &&
                            frame->SaveAs(Filename(output)).status == SaveStatus::Saved && Bytes(output) == disk,
                        "option changes and Save As must preserve encoding, BOM and existing whitespace bytes");
                Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled &&
                            frame->Open(Filename(directory / "missing.txt")).status == OpenStatus::Failed &&
                            frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                            frame->SaveAs(Filename(directory / "missing" / "failed.txt")).status ==
                                SaveStatus::Failed &&
                            editor->Send(Message::GetUseTabs) == 0 && editor->Send(Message::GetTabWidth) == 4 &&
                            frame->DocumentHasBom() && Bytes(output) == disk,
                        "cancelled and failed file operations must retain indentation and the document");
                Require(frame->New() && editor->Send(Message::GetUseTabs) == 0 &&
                            editor->Send(Message::GetTabWidth) == 4 && editor->Send(Message::GetIndent) == 4,
                        "successful New must retain this window's indentation choice");
                auto* fresh = new EditorFrame;
                Require(fresh->Editor()->Send(Message::GetUseTabs) == 1 &&
                            fresh->Editor()->Send(Message::GetTabWidth) == 8 &&
                            fresh->Editor()->Send(Message::GetIndent) == 0,
                        "a separate window must retain the original startup defaults");
                fresh->Destroy();
                std::puts(
                    "PASS: indentation survives file and language operations without changing BOM or existing bytes");

                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("a\nb\n"));
                editor->Send(Message::SetSel, 0, 3);
                Tab(editor);
                Require(NativeSearch::Text(editor) == "    a\n    b\n",
                        "existing native block Tab must read the selected indentation width and spaces policy");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("int main() {\n\treturn 0;\n}\n"));
                NativeFold::Paint(editor);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
                Require(!editor->Send(Message::GetLineVisible, 1), "the fixture must contain a collapsed block");
                NativeSearch::Command(frame, Option(frame, "8 Columns"));
                NativeSearch::Command(frame, Option(frame, "Tabs"));
                Require(!editor->Send(Message::GetLineVisible, 1),
                        "indentation options must preserve collapsed blocks");
                std::puts("PASS: existing block Tab uses live settings and options preserve code folds");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            if (!evidence) std::filesystem::remove_all(directory);
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(IndentationApp);
