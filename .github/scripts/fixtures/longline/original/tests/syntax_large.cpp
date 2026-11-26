#include "diagnostics/print.h"
#include "status_helpers.h"
#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "settings/settings_store.h"
#include <memory>
#include <chrono>
#include <ctime>
#include <exception>
#include <utility>
#include "file_helpers.h"
#include <SciLexer.h>
#include <wx/wx.h>
#include <wx/statusbr.h>
#include <cstdio>
#include <print>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using Scintilla::Message;
using TestFiles::Bytes;
using TestFiles::Filename;
// Diagnostic-only phase timing. std::clock measures this process's CPU,
// including worker threads; steady_clock measures elapsed wall time.
class ProbePhase final {
public:
    explicit ProbePhase(const char* name) noexcept
        : name_(name), sequence_(++NextSequence()), wall_(std::chrono::steady_clock::now()), cpu_(CpuMicros()),
          exceptions_(std::uncaught_exceptions()) {
        std::fprintf(stderr, "[SYNTAX-PROBE] event=begin sequence=%lld phase=%s clock_us=%lld cpu_us=%lld\n", sequence_,
                     name_, WallMicros(wall_), cpu_);
        std::fflush(stderr);
    }
    ~ProbePhase() noexcept { Finish(); }
    void Finish() noexcept {
        if (finished_) return;
        finished_ = true;
        const auto wall = std::chrono::steady_clock::now();
        const auto cpu = CpuMicros();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(wall - wall_).count();
        std::fprintf(
            stderr,
            "[SYNTAX-PROBE] event=end sequence=%lld phase=%s clock_us=%lld wall_us=%lld cpu_us=%lld exception=%d\n",
            sequence_, name_, WallMicros(wall), static_cast<long long>(elapsed), cpu < 0 || cpu_ < 0 ? -1 : cpu - cpu_,
            std::uncaught_exceptions() > exceptions_);
        std::fflush(stderr);
    }

private:
    static long long& NextSequence() noexcept {
        static long long sequence = 0;
        return sequence;
    }
    static long long WallMicros(std::chrono::steady_clock::time_point point) noexcept {
        return static_cast<long long>(
            std::chrono::duration_cast<std::chrono::microseconds>(point.time_since_epoch()).count());
    }
    static long long CpuMicros() noexcept {
        const auto value = std::clock();
        return value == std::clock_t{-1}
                   ? -1
                   : static_cast<long long>(static_cast<long double>(value) * 1000000 / CLOCKS_PER_SEC);
    }
    const char* name_;
    long long sequence_;
    std::chrono::steady_clock::time_point wall_;
    long long cpu_;
    int exceptions_;
    bool finished_ = false;
};

template <typename Action> decltype(auto) Measure(const char* name, Action&& action) {
    const ProbePhase phase(name);
    return std::forward<Action>(action)();
}

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
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
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        // Mixed-EOL warning choices are covered by mixed_line_ending_open.
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-syntax-large");
            try {
                using enum Scintilla::Message;
                ProbePhase prepare("Fixture.prepare_and_write");
                std::filesystem::create_directory(directory);
                const auto source = directory / "large.CPP";
                const auto copy = directory / "copy.CPP";
                std::string bytes = "int value;\r\n";
                const std::string comment = "//" + std::string(125, 'x') + "\n";
                while (bytes.size() <= size_t{32} * 1024U * 1024U)
                    bytes += comment;
                std::ofstream(source, std::ios::binary) << bytes;
                prepare.Finish();
                Require(Measure("Open", [&] { return frame->Open(Filename(source)); }).status == OpenStatus::Opened,
                        "large C++ document must open");
                auto* editor = frame->Editor();
                Measure("Paint.after_open", [&] { Paint(editor); });
                Require(editor->Send(GetLength) == static_cast<intptr_t>(bytes.size()) &&
                            editor->Send(GetLexer) == SCLEX_CPP && editor->Send(GetStyleIndexAt, 0) == SCE_C_WORD2 &&
                            StatusMetadata(frame).Contains("C/C++"),
                        "a supported document above 32 MiB must retain its normal language, lexer and token styles");
                const int plain = frame->GetMenuBar()->FindMenuItem("Scheme", "Plain Text");
                wxCommandEvent choose(wxEVT_MENU, plain);
                Require(Measure("PlainText.menu", [&] { return frame->GetEventHandler()->ProcessEvent(choose); }),
                        "real Plain Text menu must be handled");
                Measure("Paint.after_plain", [&] { Paint(editor); });
                Require(editor->Send(GetLexer) == SCLEX_NULL && editor->Send(GetStyleIndexAt, 0) == 0 &&
                            editor->Send(GetMarginWidthN, 2) > 0 &&
                            StatusMetadata(frame).Contains("Plain Text (override)"),
                        "manual Plain Text above 32 MiB must use the existing no-lexer and cleared-style path");
                Require(Measure("Save", [&] { return frame->Save(); }).status == SaveStatus::Saved &&
                            Measure("Readback.after_save", [&] { return Bytes(source); }) == bytes &&
                            editor->Send(GetLexer) == SCLEX_NULL && frame->GetMenuBar()->IsChecked(plain),
                        "ordinary large-file Save must retain manual Plain Text and independent exact bytes");
                Require(Measure("SaveAs.current_scheme", [&] { return frame->SaveAs(Filename(copy)); }).status ==
                                SaveStatus::Saved &&
                            Measure("Readback.current_scheme", [&] { return Bytes(copy); }) == bytes &&
                            editor->Send(GetLexer) == SCLEX_NULL,
                        "default large-file Save As must retain the current Plain Text scheme and exact bytes");
                Require(Measure("SaveAs.all_files",
                                [&] {
                                    return frame->SaveAs(Filename(copy), {}, SaveType::AllFiles);
                                }).status == SaveStatus::Saved &&
                            Measure("Readback.all_files", [&] { return Bytes(copy); }) == bytes,
                        "large-file Save As must retain exact bytes");
                Measure("Paint.after_all_files", [&] { Paint(editor); });
                Require(editor->Send(GetLexer) == SCLEX_CPP && editor->Send(GetStyleIndexAt, 0) == SCE_C_WORD2 &&
                            !StatusMetadata(frame).Contains("override") && !editor->Send(GetModify) &&
                            !editor->Send(CanUndo),
                        "large-file naming must restore filename-based lexer selection without adding edit history");
                std::println(
                    "PASS: {:d}-byte C++ Open, manual Plain Text, ordinary Save and Save As with independent bytes",
                    bytes.size());
                result_ = 0;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(LargeSyntaxApp);
