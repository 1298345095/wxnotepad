#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "settings/settings_store.h"
#include <SciLexer.h>
#include <array>
#include <filesystem>

namespace {
using Scintilla::Message;
constexpr int ResizePasses = 80;
constexpr int ControlLines = 32;
constexpr int RepeatedLines = 128;
constexpr size_t ParallelLayoutBlockBytes = 4096;
constexpr std::string_view Row = "name = \"words 中文😀 with spaces\" # comment 中文\n";

void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}

void Prepare(EditorFrame* frame, const std::filesystem::path& path) {
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "open the TOML resize fixture");
    auto* editor = frame->Editor();
    Require(editor->Send(Message::GetLexer) == SCLEX_TOML, "the real TOML lexer is active");
    // Different native fonts make a shared measurement DC race observable in
    // layout results, as well as through the original heap-corruption crash.
    editor->Send(Message::StyleSetSize, SCE_TOML_STRING_SQ, 12);
    editor->Send(Message::StyleSetSize, SCE_TOML_STRING_DQ, 12);
    editor->Send(Message::StyleSetSize, SCE_TOML_COMMENT, 8);
    editor->SetLineNumbersVisible(false);
    editor->Send(Message::Colourise, 0, -1);
    NativePaint::Complete(editor);
}

void Resize(EditorFrame* frame, int width) {
    auto* editor = frame->Editor();
    // Invalidate cached widths at their supported default capacity so each
    // resize measures the real styled text again.
    editor->Send(Message::SetPositionCache, editor->Send(Message::GetPositionCache));
    frame->SetClientSize(frame->FromDIP(wxSize(width, 360)));
    frame->Layout();
    NativePaint::Complete(frame->Editor());
}

void RepeatedLineLayout(EditorFrame* frame, const std::filesystem::path& directory) {
    const auto small = directory / "small.toml";
    const auto large = directory / "large.toml";
    std::string control;
    for (int line = 0; line < ControlLines; ++line)
        control += Row;
    TestFiles::WriteBytes(small, control);
    std::string text;
    for (int line = 0; line < RepeatedLines; ++line)
        text += Row;
    Require(control.size() < ParallelLayoutBlockBytes && text.size() > ParallelLayoutBlockBytes,
            "control and regression fixtures straddle the parallel layout threshold");
    TestFiles::WriteBytes(large, text);
    // The control has enough lines to show the same native scrollbar, but
    // remains below the core's parallel block-layout threshold.
    constexpr std::array Widths{180, 320, 540, 760};
    std::array<intptr_t, Widths.size()> expected{};
    Prepare(frame, small);
    for (size_t index = 0; index < Widths.size(); ++index) {
        Resize(frame, Widths[index]);
        expected[index] = frame->Editor()->Send(Message::WrapCount, 0);
    }
    Prepare(frame, large);
    auto* editor = frame->Editor();
    editor->Send(Message::InsertText, text.size(), reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    for (int pass = 0; pass < ResizePasses; ++pass) {
        const auto index = static_cast<size_t>(pass) % Widths.size();
        Resize(frame, Widths[index]);
        for (const auto line : {0, RepeatedLines / 2, RepeatedLines - 1}) {
            editor->Send(Message::GotoLine, line);
            NativePaint::Complete(editor);
            const auto actual = editor->Send(Message::WrapCount, line);
            if (actual != expected[index])
                std::fprintf(stderr, "pass=%d line=%d width=%d expected=%zd actual=%zd\n", pass, line, Widths[index],
                             expected[index], actual);
            Require(actual == expected[index], "repeated styled lines wrap exactly like the small control");
        }
    }
    Require(TestEditor::Text(editor) == text && !frame->DocumentIsModified() && editor->Send(Message::CanRedo),
            "rapid wrapped resizing preserves text, the save point and redo");
    const auto saved = directory / "saved.toml";
    Require(frame->SaveAs(TestFiles::Filename(saved)).status == SaveStatus::Saved && TestFiles::Bytes(saved) == text,
            "resized TOML saves byte-identical content");
}

void LongLineLayout(EditorFrame* frame, const std::filesystem::path& directory) {
    std::string text = "name = [";
    for (int item = 0; item < RepeatedLines; ++item)
        text += "\"words 中文😀 with spaces\", ";
    text += "0]\n";
    Require(text.size() > ParallelLayoutBlockBytes, "the long styled line exceeds the parallel layout threshold");
    const auto path = directory / "long-line.toml";
    TestFiles::WriteBytes(path, text);
    Prepare(frame, path);
    for (int pass = 0; pass < ResizePasses; ++pass) {
        Resize(frame, pass % 2 ? 540 : 180);
        Require(frame->Editor()->Send(Message::WrapCount, 0) > 1, "the long styled line remains word wrapped");
    }
    Require(TestEditor::Text(frame) == text && !frame->DocumentIsModified(),
            "resizing a long wrapped line preserves the original document");
}

class WrapResizeApp final : public wxApp {
public:
    bool OnInit() override {
        SetExitOnFrameDelete(false);
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = true;
        settings_->values.fileWatching.mode = FileWatchingMode::None;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-wrap-resize");
            try {
                Require(presented, "the native resize view must be presented");
                std::filesystem::create_directory(directory);
                RepeatedLineLayout(frame, directory);
                LongLineLayout(frame, directory);
                std::puts("PASS: styled multi-line and long-line wrapped resize, metrics, history and disk bytes");
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(WrapResizeApp);
