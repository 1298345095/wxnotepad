#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include <SciLexer.h>
#include <wx/wx.h>
#include <wx/statusbr.h>
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
    Require(input.is_open(), "large saved file must exist");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void Paint(EditorControl* editor) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        wxYield();
        editor->Refresh();
        editor->Update();
        wxYield();
        if (editor->Send(Message::GetEndStyled) >= 128) return;
        wxMilliSleep(10);
    }
}
class LargeSyntaxApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-syntax-large-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto source = directory / "large.CPP";
                const auto copy = directory / "copy.CPP";
                std::string bytes = "int value;\r\n";
                const std::string comment = "//" + std::string(125, 'x') + "\n";
                while (bytes.size() <= 32U * 1024U * 1024U)
                    bytes += comment;
                std::ofstream(source, std::ios::binary) << bytes;
                Require(frame->Open(Filename(source)).status == OpenStatus::Opened, "large C++ document must open");
                auto* editor = frame->Editor();
                Paint(editor);
                Require(editor->Send(Message::GetLength) == static_cast<intptr_t>(bytes.size()) &&
                            editor->Send(Message::GetLexer) == SCLEX_CPP &&
                            editor->Send(Message::GetStyleIndexAt, 0) == SCE_C_WORD2 &&
                            frame->GetStatusBar()->GetStatusText().Contains("C/C++"),
                        "a supported document above 32 MiB must retain its normal language, lexer and token styles");
                const int plain = frame->GetMenuBar()->FindMenuItem("Language", "Plain Text");
                wxCommandEvent choose(wxEVT_MENU, plain);
                Require(frame->GetEventHandler()->ProcessEvent(choose), "real Plain Text menu must be handled");
                Paint(editor);
                Require(editor->Send(Message::GetLexer) == SCLEX_NULL &&
                            editor->Send(Message::GetStyleIndexAt, 0) == 0 &&
                            editor->Send(Message::GetMarginWidthN, 1) == 0 &&
                            frame->GetStatusBar()->GetStatusText().Contains("Plain Text (override)"),
                        "manual Plain Text above 32 MiB must use the existing no-lexer and cleared-style path");
                Require(frame->Save().status == SaveStatus::Saved && Bytes(source) == bytes &&
                            editor->Send(Message::GetLexer) == SCLEX_NULL && frame->GetMenuBar()->IsChecked(plain),
                        "ordinary large-file Save must retain manual Plain Text and independent exact bytes");
                Require(frame->SaveAs(Filename(copy)).status == SaveStatus::Saved && Bytes(copy) == bytes,
                        "large-file Save As must retain exact bytes");
                Paint(editor);
                Require(editor->Send(Message::GetLexer) == SCLEX_CPP &&
                            editor->Send(Message::GetStyleIndexAt, 0) == SCE_C_WORD2 &&
                            !frame->GetStatusBar()->GetStatusText().Contains("override") &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "large-file naming must restore filename-based lexer selection without adding edit history");
                std::printf(
                    "PASS: %zu-byte C++ Open, manual Plain Text, ordinary Save and Save As with independent bytes\n",
                    bytes.size());
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
wxIMPLEMENT_APP(LargeSyntaxApp);
