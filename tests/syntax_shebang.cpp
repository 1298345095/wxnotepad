#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "file_helpers.h"
#include <SciLexer.h>
#include <wx/wx.h>
#include <wx/statusbr.h>
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
std::string Text(EditorControl* editor) {
    std::string text(static_cast<size_t>(editor->Send(Message::GetLength)) + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
void Choose(EditorFrame* frame, const wxString& name) {
    const int id = frame->GetMenuBar()->FindMenuItem("Language", name);
    Require(id != wxNOT_FOUND, "language menu item must exist");
    wxCommandEvent event(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(event), "language command must be handled");
    Paint(frame);
    Require(frame->GetMenuBar()->FindItem(id)->IsChecked(), "language choice must be checked");
}
void Language(EditorFrame* frame, SyntaxLanguage expected) {
    Paint(frame);
    Require(frame->Editor()->Send(Message::GetLexer) == SyntaxFor(expected).lexer,
            "real lexer must match the original filename/interpreter precedence");
    Require(frame->GetStatusBar()->GetStatusText().Contains(" | " + LanguageName(expected) + " | "),
            "status must match automatic language without an explicit override");
    const int id = frame->GetMenuBar()->FindMenuItem("Language", "Automatic");
    Require(id != wxNOT_FOUND && frame->GetMenuBar()->FindItem(id)->IsChecked(),
            "Open must check Automatic rather than a manual Python choice");
}
class ShebangApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto folder =
                std::filesystem::temp_directory_path() / ("wxnotepad-shebang-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(folder);
                const auto path = folder / "script";
                const std::string bytes = "#!/usr/bin/python3\nreturn 42\n";
                std::ofstream(path, std::ios::binary) << bytes;
                Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "fixture must open");
                Paint(frame);
                Require(frame->Editor()->Send(Message::GetLexer) == SCLEX_PYTHON,
                        "extensionless Python interpreter must select Python");
                Require(frame->Editor()->Send(Message::GetStyleIndexAt, bytes.find("return")) == SCE_PY_WORD,
                        "detected Python must install the real token style");
                Require(frame->GetStatusBar()->GetStatusText() == "UTF-8 | Python | EOL: LF",
                        "status must report detected Python without an override");
                Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == bytes,
                        "detection and save must preserve exact file bytes");
                std::puts("PASS: extensionless Python detection, real lexer, status and disk bytes");
                struct Fixture {
                    const char* name;
                    std::string line;
                    SyntaxLanguage language;
                };
                const auto python = SyntaxLanguage::Python;
                const auto plain = SyntaxLanguage::PlainText;
                size_t sequence = 0;
                for (const auto& fixture : {
                         Fixture{"direct", "#!/usr/bin/python3 -u", python},
                         {"env", "#!/usr/bin/env python3.12", python},
                         {"windows", "#! C:\\Python\\PYTHON.EXE -u", python},
                         {"tabs", "#!\t/usr/bin/env\tpython", python},
                         {"winpty", "#!/bin/winpty.exe C:\\Python\\python.exe", python},
                         {"envprefix", "#!/bin/environment python3", python},
                         {"short", "#!py.exe", python},
                         {"ipy", "#!/bin/IPY64.exe", python},
                         {"pypy", "#!/bin/pypy3", python},
                         {"pyother", "#!/bin/pyanything", python},
                         {"arguments", "#!/bin/python --anything", python},
                         {"envoptions", "#!/bin/env -S python3", plain},
                         {"envcase", "#!/bin/ENV python3", plain},
                         {"envvariable", "#!/bin/env VERSION=3 python3", plain},
                         {"quotedpath", "#!\"/bin/python\"", python},
                         {"quotedname", "#!\"python\"", plain},
                         {"leading", " #!/bin/python3", plain},
                         {"secondline", "\n#!/bin/python3", plain},
                         {"missingbang", "#/bin/python", plain},
                         {"empty", "#!", plain},
                         {"other", "#!/bin/perl", plain},
                         {"latepython", "#!/bin/sh python3", plain},
                         {"script.unknown", "#!/bin/python", python},
                         {"script.cgi", "#!/bin/python", python},
                         {"script.fcgi", "#!/bin/python", python},
                         {"script.txt", "#!/bin/python", plain},
                         {"script.js", "#!/bin/python", plain},
                         {"script.sh", "#!/bin/python", plain},
                         {"script.ini", "#!/bin/python", plain},
                         {"my.ini", "#!/bin/python", python},
                         {"script.cfg", "#!/bin/python", python},
                         {"script.json", "#!/bin/python", SyntaxLanguage::Json},
                         {"script.cpp", "#!/bin/python", SyntaxLanguage::Cpp},
                         {"script.py", "#!/bin/sh", python},
                         {".py", "#!/bin/sh", python},
                         {"Readme.custom", "#!/bin/python", plain},
                         {"Makefile", "#!/bin/python", plain},
                         {".bashcustom", "#!/bin/python", plain},
                         {".profile", "#!/bin/python", plain},
                         {".bashrc.cfg", "#!/bin/python", python},
                         {"withinlimit", "#!" + std::string(251, ' ') + "py", python},
                         {"beyondlimit", "#!" + std::string(252, ' ') + "py", plain},
                     }) {
                    // A distinct directory keeps Open on its different-path branch.
                    const auto directory = folder / std::to_string(++sequence);
                    std::filesystem::create_directory(directory);
                    const auto candidate = directory / fixture.name;
                    const auto content = fixture.line + "\r\nreturn 42 # 中文\n";
                    std::ofstream(candidate, std::ios::binary) << content;
                    Require(frame->Open(TestFiles::Filename(candidate)).status == OpenStatus::Opened,
                            "interpreter fixture must open");
                    Language(frame, fixture.language);
                    auto* editor = frame->Editor();
                    Require(!editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                                !editor->Send(Message::CanRedo) && Text(editor) == content,
                            "automatic detection must retain loaded bytes and clean history");
                    if (fixture.language == python)
                        Require(editor->Send(Message::GetStyleIndexAt, content.find("return")) == SCE_PY_WORD,
                                "Python fixtures must receive actual keyword styling");
                    Choose(frame, "Plain Text");
                    Choose(frame, "Automatic");
                    Language(frame, fixture.language);
                    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(candidate) == content,
                            "Automatic selection and Save must preserve all fixture bytes");
                    std::printf("PASS: %s\n", fixture.name);
                }
                const auto bomPath = folder / "bom-script";
                const std::string bomBytes = "\xef\xbb\xbf#!/bin/python\r\nreturn 42\n";
                std::ofstream(bomPath, std::ios::binary) << bomBytes;
                Require(frame->Open(TestFiles::Filename(bomPath)).status == OpenStatus::Opened,
                        "BOM fixture must open");
                Language(frame, python);
                Require(frame->DocumentHasUtf8Bom() && frame->Save().status == SaveStatus::Saved &&
                            TestFiles::Bytes(bomPath) == bomBytes,
                        "sniffing operates on decoded text without changing BOM or mixed EOL bytes");
                auto* editor = frame->Editor();
                Choose(frame, "C/C++");
                Require(editor->Send(Message::GetLexer) == SCLEX_CPP &&
                            frame->GetStatusBar()->GetStatusText().Contains("C/C++ (override)"),
                        "explicit language must take precedence over Python interpreter");
                editor->Send(Message::SetSel, 0, 0);
                editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("x"));
                editor->Send(Message::Undo);
                Require(editor->Send(Message::CanRedo), "fixture must have pending redo");
                editor->Send(Message::SetSel, 3, 12);
                Choose(frame, "Automatic");
                Language(frame, python);
                Require(editor->Send(Message::CanRedo) && !editor->Send(Message::CanUndo) &&
                            !editor->Send(Message::GetModify) && editor->Send(Message::GetAnchor) == 3 &&
                            editor->Send(Message::GetCurrentPos) == 12,
                        "Automatic must preserve pending redo, clean state and selection");
                editor->Send(Message::Redo);
                Require(editor->Send(Message::GetLexer) == SCLEX_PYTHON,
                        "ordinary edits must not introduce live first-line detection");
                const auto modifiedText = Text(editor);
                Choose(frame, "Automatic");
                Language(frame, plain);
                Require(Text(editor) == modifiedText && editor->Send(Message::CanUndo) &&
                            editor->Send(Message::GetModify),
                        "explicit Automatic rechecks current text without changing bytes or history");
                editor->Send(Message::Undo);
                Choose(frame, "Automatic");
                Language(frame, python);
                Require(frame->SaveAs(TestFiles::Filename(folder / "saved-script"), {}, SaveType::AllFiles).status ==
                                SaveStatus::Saved &&
                            TestFiles::Bytes(folder / "saved-script") == bomBytes,
                        "All Files save must use original automatic detection without changing BOM bytes");
                std::puts("PASS: BOM, explicit choices, Automatic recheck, selection, undo/redo and Save As");
                const auto utf16Path = folder / "utf16-script";
                const std::string decoded = "#!/bin/python\r\nreturn 42\n";
                std::string utf16 = "\xff\xfe";
                for (const auto ch : decoded) {
                    utf16 += ch;
                    utf16 += '\0';
                }
                std::ofstream(utf16Path, std::ios::binary) << utf16;
                Require(frame->Open(TestFiles::Filename(utf16Path)).status == OpenStatus::Opened,
                        "UTF-16 interpreter fixture must open");
                Language(frame, python);
                Require(Text(editor) == decoded && frame->DocumentEncoding() == FileEncoding::Utf16Le &&
                            frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(utf16Path) == utf16,
                        "detection must use decoded text and preserve original UTF-16 bytes");
                std::ofstream(utf16Path, std::ios::binary) << "#!/bin/sh\nreturn 42\n";
                Require(frame->Open(TestFiles::Filename(utf16Path)).status == OpenStatus::Opened,
                        "same-path interpreter fixture must reopen");
                Language(frame, python);
                Choose(frame, "Automatic");
                Language(frame, plain);
                std::puts("PASS: decoded UTF-16 and same-path effective-language retention");
                std::filesystem::remove_all(folder);
                frame->Destroy();
                ExitMainLoop();
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
                std::filesystem::remove_all(folder);
                frame->Destroy();
                ExitMainLoop();
            }
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(ShebangApp);
