#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "tab_settings_helpers.h"
#include <wx/app.h>
#include <wx/ffile.h>
#include <wx/menu.h>
#include <wx/stopwatch.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Text(EditorControl* editor) {
    std::string text(static_cast<size_t>(editor->Send(Message::GetLength)) + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
void Menu(EditorFrame* frame, const wxString& label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    Require(id != wxNOT_FOUND, "native Edit menu exposes indentation command");
    NativeSearch::Command(frame, id);
}
void NativeTab(EditorControl* editor, bool shift = false) {
#ifdef __WXMSW__
    BYTE previous[256];
    Require(GetKeyboardState(previous), "read native test thread keyboard state");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    modified[VK_SHIFT] = shift ? 0x80 : 0;
    Require(SetKeyboardState(modified), "set native test thread Shift state");
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYDOWN, VK_TAB, 0);
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYUP, VK_TAB, 0);
    SetKeyboardState(previous);
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys require private display helper");
    bool received = false;
    wxEvtHandler observer;
    observer.Bind(wxEVT_KEY_UP, [&received](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_TAB) received = true;
        event.Skip();
    });
    editor->PushEventHandler(&observer);
    {
        wxFFile file(focusFile, "wb");
        file.Write(shift ? "key:shift+Tab" : "key:Tab");
    }
    wxStopWatch wait;
    while (!received && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, "actual private-display Tab key must reach editor");
#endif
    wxYield();
}
void Paint(EditorControl* editor) {
    editor->Refresh(false);
    editor->Update();
    wxYield();
}
#ifdef __WXGTK__
void DialogTab(GtkWidget* dialog, bool shift) {
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "dialog keys require private display helper");
    {
        wxFFile file(focusFile, "wb");
        file.Write("focus:" + wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(dialog))));
    }
    wxStopWatch focusWait;
    while (focusWait.Time() < 3000) {
        wxString pending;
        {
            wxFFile file(focusFile, "rb");
            file.ReadAll(&pending);
        }
        if (pending.empty()) break;
        wxYield();
        wxMilliSleep(10);
    }
    auto* previous = gtk_window_get_focus(GTK_WINDOW(dialog));
    Require(previous, "activated native dialog has keyboard focus");
    {
        wxFFile file(focusFile, "wb");
        file.Write(shift ? "key:shift+Tab" : "key:Tab");
    }
    wxStopWatch wait;
    while (gtk_window_get_focus(GTK_WINDOW(dialog)) == previous && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(gtk_window_get_focus(GTK_WINDOW(dialog)) != previous, "Tab retains native dialog focus traversal");
}
class DialogTabs final : public wxTimer {
public:
    explicit DialogTabs(wxString title) : title_(std::move(title)) { Start(100); }
    bool checked = false;
    std::string error;
    void Notify() override {
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* dialog = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(dialog) ||
                wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(dialog))) != title_)
                continue;
            Stop();
            try {
                wxWindow* line = nullptr;
                wxWindow* column = nullptr;
                if (title_ == "Go To Line") {
                    for (auto* window : wxTopLevelWindows) {
                        if (window->GetHandle() != dialog) continue;
                        line = wxWindow::FindWindowByName("Go to line number", window);
                        column = wxWindow::FindWindowByName("Go to column number", window);
                    }
                    Require(line && column, "Go To Line exposes its original line and column inputs");
                    line->SetFocus();
                    wxYield();
                }
                DialogTab(dialog, false);
                if (column)
                    Require(gtk_window_get_focus(GTK_WINDOW(dialog)) == column->GetHandle(),
                            "Tab moves from Go To Line to its Column input");
                DialogTab(dialog, true);
                if (line)
                    Require(gtk_window_get_focus(GTK_WINDOW(dialog)) == line->GetHandle(),
                            "Shift+Tab returns from Column to Line");
                checked = true;
            } catch (const std::exception& failure) {
                error = failure.what();
            }
            if (GTK_IS_DIALOG(dialog)) {
                NativeDialogs::Respond(dialog, GTK_RESPONSE_CANCEL);
            } else {
                for (auto* window : wxTopLevelWindows)
                    if (window->GetHandle() == dialog) NativeSearch::Click(window, wxID_CANCEL);
            }
            break;
        }
        g_list_free(windows);
    }

private:
    wxString title_;
};
void DialogTraversal(EditorFrame* frame) {
    const auto before = Text(frame->Editor());
    Menu(frame, "Find...");
    bool found = false;
    for (auto* window : wxTopLevelWindows) {
        if (window->GetLabel() != "Find" || !window->IsShown()) continue;
        DialogTab(GTK_WIDGET(window->GetHandle()), false);
        DialogTab(GTK_WIDGET(window->GetHandle()), true);
        NativeSearch::Click(window, wxID_CANCEL);
        found = true;
    }
    Require(found, "application Find dialog was shown");
    DialogTabs goTo("Go To Line");
    Menu(frame, "Go To Line...");
    goTo.Stop();
    if (!goTo.error.empty()) std::fprintf(stderr, "Go To Line: %s\n", goTo.error.c_str());
    Require(goTo.checked && goTo.error.empty(), "Go To Line retains native Tab/Shift+Tab focus traversal");
    frame->Editor()->Send(Message::SetSavePoint);
    DialogTabs open("Open file");
    NativeSearch::Command(frame, wxID_OPEN);
    open.Stop();
    Require(open.checked && open.error.empty(), "file dialog retains native Tab/Shift+Tab focus traversal");
    Require(Text(frame->Editor()) == before, "dialog Tab keys do not edit the document");
    std::puts("PASS: actual Tab/Shift+Tab traverse Find, Go To Line and native file dialogs");
}
#endif
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory =
        std::filesystem::temp_directory_path() / ("wxnotepad-block-indent-" + std::to_string(wxGetProcessId()));
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string after;
    };
    const Fixture fixtures[] = {
        {"utf8-bom", FileEncoding::Utf8,
         "\xef\xbb\xbf"
         "中文\r\nB\nlast",
         "\xef\xbb\xbf\t中文\r\n\tB\n\tlast"},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0\xce\xc4\rB\r\nlast\n", "\t\xd6\xd0\xce\xc4\r\tB\r\n\tlast\n"},
        {"utf16-bom", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\x87\x65\x0d\0\x0a\0\x42\0", 12),
         std::string("\xff\xfe\x09\0\x2d\x4e\x87\x65\x0d\0\x0a\0\x09\0\x42\0", 16)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        {
            std::ofstream file(path, std::ios::binary);
            file.write(fixture.before.data(), static_cast<std::streamsize>(fixture.before.size()));
        }
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(wxString::FromUTF8(path.string()), fixture.encoding).status == OpenStatus::Opened,
                "byte fixture opens through application file operation");
        editor->Send(Message::SelectAll);
        Menu(frame, "Indent");
        Require(frame->Save().status == SaveStatus::Saved && Bytes(path) == fixture.after,
                "Save preserves encoding, BOM, mixed EOL bytes and final-newline presence");
        wxString evidence;
        if (wxGetEnv("WXNOTEPAD_BLOCK_EVIDENCE_DIR", &evidence)) {
            const auto target = std::filesystem::path(evidence.ToStdString()) / (std::string(fixture.name) + ".saved");
            std::filesystem::copy_file(path, target, std::filesystem::copy_options::overwrite_existing);
        }
        editor->Send(Message::Undo);
        Require(frame->Save().status == SaveStatus::Saved && Bytes(path) == fixture.before,
                "single Undo and Save restore original independently read file bytes");
        editor->Send(Message::Redo);
        Require(frame->Save().status == SaveStatus::Saved && Bytes(path) == fixture.after,
                "single Redo and Save restore indented independently read file bytes");
        std::printf("PASS: %s independent saved bytes, one-step Undo/Redo\n", fixture.name);
    }
    std::filesystem::remove_all(directory);
}
class BlockIndentationApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                frame->Raise();
                editor->SetFocus();
                wxMilliSleep(200);
                wxYield();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("A\r\nB\r\nlast"));
                editor->Send(Message::SetSel, 0, 6);
                Menu(frame, "Indent");
                Require(Text(editor) == "\tA\r\n\tB\r\nlast", "Indent excludes a forward endpoint at column zero");
                std::puts("PASS: Indent native menu and forward logical-line selection");
                editor->Send(Message::SetSel, 0, 0);
                Menu(frame, "Indent");
                Require(Text(editor) == "\t\tA\r\n\tB\r\nlast",
                        "Indent at column zero changes the current logical line");
                Menu(frame, "Unindent");
                Require(Text(editor) == "\tA\r\n\tB\r\nlast", "Unindent acts on the current logical line");
                std::puts("PASS: current-line commands at column zero");
                editor->Send(Message::SetSel, 2, 2);
                NativeTab(editor, true);
                Require(Text(editor) == "A\r\n\tB\r\nlast", "Shift+Tab inside text unindents its logical line");
                std::puts("PASS: actual native Shift+Tab unindents current line");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("A\r\nB\r\nlast"));
                editor->Send(Message::SetSel, 6, 0);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                NativeTab(editor);
                Require(Text(editor) == "\tA\r\n\tB\r\nlast" && editor->Send(Message::GetAnchor) == 8 &&
                            editor->Send(Message::GetCurrentPos) == 0,
                        "native Tab preserves reverse selection and excludes column-zero endpoint");
                editor->Send(Message::Undo);
                Require(Text(editor) == "A\r\nB\r\nlast" && !editor->Send(Message::CanUndo) &&
                            !editor->Send(Message::GetModify),
                        "one Undo restores exact bytes and save point");
                editor->Send(Message::Redo);
                Require(Text(editor) == "\tA\r\n\tB\r\nlast" && editor->Send(Message::GetModify),
                        "one Redo restores complete block indentation");
                editor->Send(Message::SetSel, 8, 0);
                NativeTab(editor, true);
                Require(Text(editor) == "A\r\nB\r\nlast", "Shift+Tab unindents all selected logical lines");
                std::puts("PASS: actual native Tab/Shift+Tab, reverse endpoint, one-step Undo/Redo and save point");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("AB"));
                editor->Send(Message::SetSel, 1, 1);
                NativeTab(editor);
                Require(Text(editor) == "A\tB", "ordinary Tab retains core insertion inside a line");
                editor->Send(Message::SetSel, 0, 1);
                NativeTab(editor);
                Require(Text(editor) == "\t\tB", "single-line Tab retains core selection replacement");
                std::puts("PASS: normal Tab insertion and single-line replacement");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("A\nB"));
                editor->Send(Message::SetSel, 0, 0);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                Menu(frame, "Indent");
                editor->Send(Message::Undo);
                Menu(frame, "Unindent");
                Require(Text(editor) == "A\nB" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                        "no-op Unindent leaves save point, undo buffer and pending redo intact");
                editor->Send(Message::Redo);
                Require(Text(editor) == "\tA\nB", "pending redo survives no-op Unindent");
                std::puts("PASS: no-op Unindent preserves pending redo and clean state");
                editor->Send(Message::SetReadOnly, 1);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                for (const auto* label : {"Indent", "Unindent"})
                    Require(!frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", label)),
                            "read-only core disables block menus");
                editor->IndentLines(true);
                NativeTab(editor, true);
                Require(Text(editor) == "\tA\nB", "read-only block commands preserve text");
                editor->Send(Message::SetReadOnly, 0);
                std::puts("PASS: core read-only state protects menu and keyboard commands");
                editor->Send(Message::SetTabWidth, 4);
                editor->Send(Message::SetIndent, 2);
                editor->Send(Message::SetUseTabs, 0);
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("\t A\r\n  中文\n\n  Z"));
                editor->Send(Message::SelectAll);
                Menu(frame, "Indent");
                Require(Text(editor) == "       A\r\n    中文\n\n    Z",
                        "effective spaces and indent width preserve core mixed-indent and blank-line behavior");
                editor->Send(Message::SelectAll);
                Menu(frame, "Unindent");
                Require(Text(editor) == "     A\r\n  中文\n\n  Z", "Unindent removes one effective indentation step");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(" A\n"));
                editor->Send(Message::GotoPos, 1);
                Menu(frame, "Unindent");
                Require(Text(editor) == "A\n", "partial indentation unindents to zero using the core");
                std::puts("PASS: effective settings, mixed whitespace, blank lines, CJK and partial indent");
                editor->Send(Message::SetTabWidth, 8);
                editor->Send(Message::SetIndent, 0);
                editor->Send(Message::SetUseTabs, 1);
                const std::string longLine = "first " + std::string(300, 'x');
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>((longLine + "\nlast").c_str()));
                editor->ToggleWordWrap();
                frame->SetClientSize(wxSize(500, 300));
                Paint(editor);
                Require(editor->Send(Message::WrapCount, 0) > 1, "fixture spans wrapped display rows");
                editor->Send(Message::SelectAll);
                NativeTab(editor);
                Require(Text(editor) == "\t" + longLine + "\n\tlast", "wrapped rows indent as two logical lines");
                editor->ToggleWordWrap();
                const std::string folded = "void f() {\n  call();\n}\nafter();\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(folded.c_str()));
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                Paint(editor);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
                Require(!editor->Send(Message::GetLineVisible, 1), "fixture includes folded logical text");
                editor->Send(Message::SetSel, 0, editor->Send(Message::PositionFromLine, 3));
                Menu(frame, "Indent");
                Require(Text(editor) == "\tvoid f() {\n\t  call();\n\t}\nafter();\n",
                        "selected folded content indents with its visible header");
                const auto caret = editor->Send(Message::GetCurrentPos);
                const auto y = editor->Send(Message::PointYFromPosition, 0, caret);
                Require(caret <= editor->Send(Message::GetLength) && y >= 0 && y < editor->GetClientSize().y,
                        "block command keeps a valid visible caret");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Expand All"));
                std::puts("PASS: wrapped rows, folded logical content and valid visible caret");
                SearchSession search(*editor);
                SearchRequest request;
                request.query = "call";
                request.replacement = "replacement";
                request.options.regularExpression = true;
                search.Configure(request);
                editor->Send(Message::GotoPos, 0);
                Require(search.Find(SearchDirection::Forward).status == FindStatus::Found,
                        "search fixture finds a match");
                Menu(frame, "Indent");
                const auto beforeReplace = Text(editor);
                Require(!search.ReplaceOne().changed && Text(editor) == beforeReplace,
                        "indentation invalidates the previous regex match before replacement");
                std::puts("PASS: search revalidates matches after indentation");
                SavedBytes(frame);
                TabDialog::File(frame, 2, 2, true);
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文\nB"));
                editor->Send(Message::SelectAll);
                Menu(frame, "Indent");
                Require(Text(editor) == "  中文\n  B", "block menu uses the user-selected spaces and width");
                NativeTab(editor, true);
                Require(Text(editor) == "中文\nB", "Shift+Tab uses the user-selected indentation width");
                TabDialog::File(frame, 8, 8, false);
                std::puts("PASS: block commands honor native indentation settings menu choices");
#ifdef __WXGTK__
                DialogTraversal(frame);
#endif
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
wxIMPLEMENT_APP(BlockIndentationApp);
