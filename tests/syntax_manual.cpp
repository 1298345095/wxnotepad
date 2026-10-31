#include "editor/editor_control.h"
#include "app/editor_frame.h"
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
void Paint(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto prefix = std::min(editor->Send(Message::GetLength), intptr_t{128});
    for (int attempt = 0; attempt < 100; ++attempt) {
        wxYield();
        editor->Refresh();
        editor->Update();
        wxYield();
        if (editor->Send(Message::GetEndStyled) >= prefix) return;
        wxMilliSleep(10);
    }
}
void Command(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(event), "native menu command must be handled");
    Paint(frame);
}
void Choose(EditorFrame* frame, const wxString& label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Language", label);
    Require(id != wxNOT_FOUND, "Language menu must offer Automatic, Plain Text and C/C++");
    Command(frame, id);
    for (const auto* choice : {"Automatic", "Plain Text", "C/C++"}) {
        auto* item = frame->GetMenuBar()->FindItem(frame->GetMenuBar()->FindMenuItem("Language", choice));
        Require(item && item->IsRadio() && item->IsChecked() == (label == choice),
                "native language choices must be mutually exclusive and show the active choice");
    }
}
void Language(EditorFrame* frame, bool cpp, bool explicitChoice) {
    Paint(frame);
    Require(frame->Editor()->Send(Message::GetStyleIndexAt, 0) == (cpp ? SCE_C_WORD2 : 0),
            "language choice must immediately change existing text styles");
    const auto status = frame->GetStatusBar()->GetStatusText();
    Require(status.Contains(cpp ? "C/C++" : "Plain Text") && status.Contains("UTF-8") &&
                status.Contains("override") == explicitChoice,
            "status must retain encoding and identify effective language and explicit override");
}
// Operate only this test process's owned native modal widgets.
class NativeDriver final : public wxTimer {
public:
    explicit NativeDriver(wxString destination = {}, int choice = wxID_CANCEL)
        : destination_(std::move(destination)), choice_(choice) {
        Start(100);
    }
    int choosers = 0;
    int prompts = 0;
    int errors = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<NativeDriver*>(data);
                if (!IsWindowVisible(window)) return TRUE;
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                const wxString name(title);
                if (name == "Save changes?") {
                    ++driver.prompts;
                    SendMessageW(window, WM_USER + 102,
                                 driver.choice_ == wxID_YES  ? IDYES
                                 : driver.choice_ == wxID_NO ? IDNO
                                                             : IDCANCEL,
                                 0);
                } else if (name == "Could not save file") {
                    ++driver.errors;
                    SendMessageW(window, WM_USER + 102, IDOK, 0);
                } else if ((name == "Save file" || name == "Save As" || name == "Open file") && !driver.stage_++) {
                    ++driver.choosers;
                    NativeDialogs::ChooseFile(window, driver.destination_);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget)) continue;
            if (GTK_IS_FILE_CHOOSER(widget)) {
                if (!stage_++) {
                    ++choosers;
                    NativeDialogs::ChooseFile(widget, destination_);
                } else if (stage_ == 2 && !destination_.empty())
                    NativeDialogs::Respond(widget, GTK_RESPONSE_ACCEPT);
            } else if (GTK_IS_MESSAGE_DIALOG(widget)) {
                const auto name = wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget)));
                if (name == "Save changes?") {
                    ++prompts;
                    NativeDialogs::Respond(widget, choice_ == wxID_YES  ? GTK_RESPONSE_YES
                                                   : choice_ == wxID_NO ? GTK_RESPONSE_NO
                                                                        : GTK_RESPONSE_CANCEL);
                } else if (name == "Could not save file") {
                    ++errors;
                    NativeDialogs::Respond(widget, GTK_RESPONSE_OK);
                }
            }
        }
        g_list_free(windows);
#endif
    }

private:
    wxString destination_;
    int choice_;
    int stage_ = 0;
};
void FileCommand(EditorFrame* frame, int command, const wxString& destination = {}) {
    NativeDriver driver(destination);
    Command(frame, command);
    driver.Stop();
    Require(driver.choosers == 1, "file command must use its native chooser");
}
class ManualLanguageApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory = std::filesystem::temp_directory_path() /
                                   ("wxnotepad-syntax-manual-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                auto* editor = frame->Editor();
                const auto menuCount = frame->GetMenuBar()->GetMenuCount();
                const std::string source = "int value; // 中文\r\n";
                editor->Send(Message::AddText, source.size(), reinterpret_cast<intptr_t>(source.data()));
                editor->Send(Message::SetSel, 8, 2);
                Choose(frame, "C/C++");
                Language(frame, true, true);
                Require(editor->Send(Message::GetModify) && editor->Send(Message::GetAnchor) == 8 &&
                            editor->Send(Message::GetCurrentPos) == 2,
                        "manual choice must retain dirty state and reversed selection");
                Choose(frame, "Plain Text");
                Language(frame, false, true);
                Require(editor->Send(Message::StyleGetFore, SCE_C_WORD2) == 0 &&
                            editor->Send(Message::StyleGetBold, SCE_C_WORD2) == 0,
                        "Plain Text must clear obsolete C++ style attributes");
                Choose(frame, "Automatic");
                Language(frame, false, false);
                Choose(frame, "C/C++");
                Choose(frame, "C/C++");
                Require(frame->GetMenuBar()->GetMenuCount() == menuCount, "repeated choices must not accumulate menus");
                Command(frame, wxID_UNDO);
                Require(editor->Send(Message::GetLength) == 0 && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::CanRedo),
                        "language choices must not add edits to Undo or discard Redo");
                Choose(frame, "Plain Text");
                Choose(frame, "C/C++");
                Command(frame, wxID_REDO);
                Language(frame, true, true);
                Require(editor->Send(Message::GetLength) == static_cast<intptr_t>(source.size()),
                        "Redo must restore text after view-only language choices");
                std::puts("PASS: unnamed document native choices preserve selection, history and explicit status");
                const auto plain = directory / "misnamed.txt";
                const auto cpp = directory / "named.cpp";
                FileCommand(frame, wxID_SAVE);
                Require(frame->SaveFirst(Filename(directory / "missing" / "first.py")).status == SaveStatus::Failed &&
                            frame->DocumentPath().empty() && editor->Send(Message::GetModify),
                        "cancelled and failed first Save must preserve the unnamed dirty document");
                Language(frame, true, true);
                FileCommand(frame, wxID_SAVE, Filename(plain));
                Language(frame, true, true);
                Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Language", "C/C++")),
                        "successful first Save must retain selected current C++ type despite .txt");
                Require(Bytes(plain) == source && !editor->Send(Message::GetModify),
                        "first Save with override must retain exact text and its save point");
                Choose(frame, "C/C++");
                Command(frame, wxID_SAVE);
                Language(frame, true, true);
                Choose(frame, "Plain Text");
                FileCommand(frame, wxID_SAVEAS, Filename(cpp));
                Language(frame, false, true);
                Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Language", "Plain Text")),
                        "named Plain Text default type must force Plain Text despite .cpp");
                Require(Bytes(cpp) == source && editor->Send(Message::CanUndo),
                        "Save As with explicit Plain Text must retain exact bytes and Undo");
                Choose(frame, "Plain Text");
                FileCommand(frame, wxID_SAVEAS);
                Require(frame->SaveAs(Filename(directory / "missing" / "failed.cpp")).status == SaveStatus::Failed &&
                            frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                            NativeDialogs::PathEquals(frame->DocumentPath(), Filename(cpp)),
                        "failed or cancelled Save As must retain the current document path");
                Language(frame, false, true);
                Choose(frame, "Automatic");
                Language(frame, true, false);
                std::puts("PASS: naming honors the current type; ordinary Save and failed/cancelled Save As retain "
                          "overrides");
                Choose(frame, "Plain Text");
                editor->Send(Message::GotoPos, source.size());
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("x"));
                {
                    NativeDriver driver;
                    Command(frame, wxID_NEW);
                    driver.Stop();
                    Require(driver.prompts == 1, "cancelled New must use the native discard prompt");
                }
                Language(frame, false, true);
                FileCommand(frame, wxID_OPEN);
                Language(frame, false, true);
                Require(frame->Open(Filename(directory / "missing.cpp")).status == OpenStatus::Failed &&
                            frame->Open(std::nullopt).status == OpenStatus::Cancelled,
                        "failed and cancelled Open must retain the current document");
                {
                    NativeDriver driver(Filename(plain));
                    Command(frame, wxID_OPEN);
                    driver.Stop();
                    Require(driver.choosers == 1 && driver.prompts == 1,
                            "Open rejected at the dirty prompt must use native chooser and confirmation");
                }
                Language(frame, false, true);
                // An existing directory at the current path makes the real Save fail.
                std::filesystem::remove(cpp);
                std::filesystem::create_directory(cpp);
                {
                    NativeDriver driver({}, wxID_YES);
                    Command(frame, wxID_NEW);
                    driver.Stop();
                    Require(driver.prompts == 1 && driver.errors == 1,
                            "failed New must report the failed real Save after its native confirmation");
                }
                Language(frame, false, true);
                Require(frame->DocumentPath() == Filename(cpp) && editor->Send(Message::GetModify) &&
                            editor->Send(Message::GetLength) == static_cast<intptr_t>(source.size() + 1),
                        "cancelled or failed New/Open must retain path, text and dirty state");
                {
                    NativeDriver driver({}, wxID_NO);
                    Command(frame, wxID_NEW);
                    driver.Stop();
                    Require(driver.prompts == 1, "successful New must support native Discard");
                }
                Require(frame->DocumentPath().empty() && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo),
                        "successful New must retain established fresh-document semantics");
                Language(frame, false, false);
                Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Language", "Automatic")),
                        "successful New must check Automatic in the native menu");
                Choose(frame, "C/C++");
                FileCommand(frame, wxID_OPEN, Filename(plain));
                Language(frame, false, false);
                Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Language", "Automatic")),
                        "successful Open must check Automatic in the native menu");
                Choose(frame, "C/C++");
                Language(frame, true, true);
                FileCommand(frame, wxID_OPEN);
                Language(frame, true, true);
                auto* fresh = new EditorFrame;
                Require(fresh->GetStatusBar()->GetStatusText().Contains("Plain Text") &&
                            !fresh->GetStatusBar()->GetStatusText().Contains("override"),
                        "a fresh editor session must start in Automatic without inheriting an override");
                fresh->Destroy();
                std::puts("PASS: successful New/Open reset override; failed/cancelled operations retain it");
                result_ = 0;
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
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(ManualLanguageApp);
