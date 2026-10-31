#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
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
void Add(EditorFrame* frame, const std::string& text) {
    auto* editor = frame->Editor();
    editor->Send(Scintilla::Message::GotoPos, editor->Send(Scintilla::Message::GetLength));
    editor->Send(Scintilla::Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
}
class SaveApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-save-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto path = directory / "existing.txt";
                {
                    std::ofstream(path, std::ios::binary) << "\xef\xbb\xbf"
                                                             "one\r\ntwo\nthree\r";
                }
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened, "source must open");
                Add(frame, "中文😀");
                wxUpdateUIEvent state(wxID_SAVE);
                frame->GetEventHandler()->ProcessEvent(state);
                Require(state.GetEnabled(), "Save menu must be enabled for an existing path");
                wxCommandEvent save(wxEVT_MENU, wxID_SAVE);
                frame->GetEventHandler()->ProcessEvent(save);
                Require(Bytes(path) == "\xef\xbb\xbf"
                                       "one\r\ntwo\nthree\r中文😀",
                        "Save menu must write current text with BOM and actual EOL");
                Require(frame->DocumentPath() == Filename(path) &&
                            !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Save must retain identity and establish a save point");
                std::puts("PASS: existing-path Save menu preserves BOM/EOL and identity");
                Add(frame, "!");
                const auto copy = directory / u8"另存为.txt";
                Require(frame->SaveAs(Filename(copy)).status == SaveStatus::Saved,
                        "Save As to a new path must succeed");
                Require(Bytes(copy) == "\xef\xbb\xbf"
                                       "one\r\ntwo\nthree\r中文😀!",
                        "Save As must preserve BOM and mixed EOL bytes");
                Require(Bytes(path) == "\xef\xbb\xbf"
                                       "one\r\ntwo\nthree\r中文😀",
                        "Save As must leave original file unchanged");
                Require(frame->DocumentPath() == Filename(copy) && frame->DocumentHasUtf8Bom() &&
                            !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Save As must update identity and establish a save point");
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Undo across Save As save point must be dirty");
                frame->Editor()->Send(Scintilla::Message::Redo);
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Redo to Save As save point must be clean");
                Add(frame, "?");
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify), "Undo to save point must be clean");
                frame->Editor()->Send(Scintilla::Message::Redo);
                Require(frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Redo away from save point must be dirty");
                Require(frame->Save().status == SaveStatus::Saved && Bytes(copy) == "\xef\xbb\xbf"
                                                                                    "one\r\ntwo\nthree\r中文😀!?",
                        "next Save must use the Save As identity");
                std::puts("PASS: Save As new path, original isolation and Undo/Redo save points");
                Add(frame, "pending");
                const auto identity = frame->DocumentPath();
                const auto unchanged = [&] {
                    Require(frame->DocumentPath() == identity && frame->DocumentHasUtf8Bom(),
                            "cancel/failure must retain identity and BOM");
                    Require(frame->Editor()->Send(Scintilla::Message::GetModify) &&
                                frame->Editor()->Send(Scintilla::Message::CanUndo),
                            "cancel/failure must retain dirty state and history");
                    Require(Bytes(copy) == "\xef\xbb\xbf"
                                           "one\r\ntwo\nthree\r中文😀!?",
                            "cancel/failure must retain source disk bytes");
                };
                Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled,
                        "cancelled Save As must return Cancelled");
                unchanged();
                Require(frame->SaveAs(Filename(directory / "missing" / "target.txt")).status == SaveStatus::Failed,
                        "Save As with missing parent must fail");
                unchanged();
                const auto blocked = directory / "blocked.txt";
                std::filesystem::create_directory(blocked);
                {
                    std::ofstream(blocked / "sentinel", std::ios::binary) << "keep";
                }
                Require(frame->SaveAs(Filename(blocked)).status == SaveStatus::Failed &&
                            Bytes(blocked / "sentinel") == "keep",
                        "failed replacement must preserve target");
                unchanged();
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify),
                        "cancel/failure must preserve original save point");
                frame->Editor()->Send(Scintilla::Message::Redo);
                Require(frame->Editor()->Send(Scintilla::Message::GetModify),
                        "cancel/failure must preserve pending text in Redo");
                Require(frame->SaveAs(Filename(copy)).status == SaveStatus::Saved &&
                            Bytes(copy) == "\xef\xbb\xbf"
                                           "one\r\ntwo\nthree\r中文😀!?pending",
                        "approved same-path Save As must preserve full pending text");
                for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
                    Require(entry.path().filename().string().find(".wxnotepad-") == std::string::npos,
                            "failed saves must clean temporary siblings");
                std::puts("PASS: Save As cancellation and creation/replacement failures preserve session and disk");
                const std::string samples[] = {"", "\xef\xbb\xbf", "ASCII\r\n",
                                               "\xef\xbb\xbf"
                                               "中文😀\n",
                                               "one\r\ntwo\nthree\r"};
                for (size_t index = 0; index < std::size(samples); ++index) {
                    const auto input = directory / ("input-" + std::to_string(index) + ".txt");
                    const auto output = directory / ("output-" + std::to_string(index) + ".txt");
                    {
                        std::ofstream file(input, std::ios::binary);
                        file.write(samples[index].data(), samples[index].size());
                    }
                    auto* sample = new EditorFrame;
                    Require(sample->Open(Filename(input)).status == OpenStatus::Opened, "format sample must open");
                    Require(sample->SaveAs(Filename(output)).status == SaveStatus::Saved,
                            "format sample Save As must succeed");
                    Require(Bytes(output) == samples[index] && Bytes(input) == samples[index],
                            "Save As must preserve empty/BOM/no-BOM/Unicode/EOL samples exactly");
                    sample->Destroy();
                }
                std::puts("PASS: five empty/BOM/no-BOM/Unicode/EOL Save As byte roundtrips");
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
    int OnExit() override { return result_; }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SaveApp);
