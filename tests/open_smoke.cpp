#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include <wx/wx.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
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
std::string Text(EditorFrame* frame) {
    const auto length = static_cast<size_t>(frame->Editor()->Send(Scintilla::Message::GetLength));
    std::string text(length + 1, '\0');
    frame->Editor()->Send(Scintilla::Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
class OpenApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-open-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto path = directory / u8"中文.txt";
                {
                    std::ofstream(path, std::ios::binary) << "Hello 中文\r\nsecond\nthird\rlast";
                }
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened, "Open must load UTF-8");
                Require(Text(frame) == "Hello 中文\r\nsecond\nthird\rlast",
                        "Open must retain Unicode and mixed EOL bytes");
                Require(frame->DocumentPath() == Filename(path), "Open must record the source path");
                Require(!frame->DocumentHasUtf8Bom(), "no-BOM source must record no BOM");
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Open must establish a clean save point");
                std::puts("PASS: UTF-8 Open retains path, text and actual EOLs");
                const auto bomPath = directory / "bom.txt";
                {
                    std::ofstream(bomPath, std::ios::binary) << "\xef\xbb\xbf中文😀\r\nnext\n";
                }
                Require(frame->Open(Filename(bomPath)).status == OpenStatus::Opened, "BOM UTF-8 must open");
                Require(Text(frame) == "中文😀\r\nnext\n" && frame->DocumentHasUtf8Bom(),
                        "BOM must be metadata, not editor text");
                Require(!frame->Editor()->Send(Scintilla::Message::CanUndo),
                        "Open must isolate previous document Undo");
                Require(frame->Save().status == SaveStatus::Saved, "opened BOM document must save");
                {
                    std::ifstream input(bomPath, std::ios::binary);
                    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                    Require(bytes == "\xef\xbb\xbf\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80\r\nnext\n",
                            "save must preserve BOM and exact UTF-8 bytes");
                }
                for (const bool bom : {false, true}) {
                    const auto emptyPath = directory / (bom ? "empty-bom.txt" : "empty.txt");
                    {
                        std::ofstream(emptyPath, std::ios::binary) << (bom ? "\xef\xbb\xbf" : "");
                    }
                    Require(frame->Open(Filename(emptyPath)).status == OpenStatus::Opened, "empty UTF-8 must open");
                    Require(Text(frame).empty() && frame->DocumentHasUtf8Bom() == bom,
                            "empty file must retain BOM metadata");
                }
                std::puts("PASS: UTF-8 BOM metadata, empty files and isolated Undo");
                frame->Editor()->Send(Scintilla::Message::AddText, 4, reinterpret_cast<intptr_t>("keep"));
                const auto oldPath = frame->DocumentPath();
                const auto unchanged = [&] {
                    Require(Text(frame) == "keep" && frame->DocumentPath() == oldPath && frame->DocumentHasUtf8Bom(),
                            "failed/cancelled Open must retain current content and format");
                    Require(frame->Editor()->Send(Scintilla::Message::GetModify) &&
                                frame->Editor()->Send(Scintilla::Message::CanUndo),
                            "failed/cancelled Open must retain dirty state and Undo");
                };
                Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled,
                        "cancelled Open must return Cancelled");
                unchanged();
                Require(frame->Open(Filename(directory / "missing.txt")).status == OpenStatus::Failed,
                        "missing source must fail");
                unchanged();
                Require(frame->Open(Filename(directory)).status == OpenStatus::Failed, "directory source must fail");
                unchanged();
                const std::vector<std::string> invalid = {"\x80",
                                                          "\xc0\xaf",
                                                          "\xe0\x80\xaf",
                                                          "\xed\xa0\x80",
                                                          "\xf0\x80\x80\xaf",
                                                          "\xf4\x90\x80\x80",
                                                          "\xf5\x80\x80\x80",
                                                          "\xc2",
                                                          "\xe4\xb8",
                                                          "\xf0\x9f\x98",
                                                          "\xff\xfe"};
                for (const auto& suffix : invalid) {
                    const auto invalidPath = directory / "invalid.txt";
                    {
                        std::ofstream output(invalidPath, std::ios::binary);
                        output << "valid prefix\r\n" << suffix;
                    }
                    const auto result = frame->Open(Filename(invalidPath));
                    Require(result.status == OpenStatus::Failed && result.error.Contains("UTF-8"),
                            "malformed UTF-8 must report an explicit decoding error");
                    unchanged();
                }
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(Text(frame).empty() && !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "failed Open must retain the prior save point");
                std::puts("PASS: cancellation/read/strict UTF-8 failures retain document and history");
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
    int OnExit() override { return result_; }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(OpenApp);
