#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "settings/settings_store.h"
#include <array>

namespace {
using Scintilla::Message;
constexpr size_t FixtureBytes = 1048597;
constexpr int ViewportHeight = 598;
constexpr int ReferenceDpi = 144;
constexpr int ReferencePointSizeFractional = 1200;
constexpr size_t MinimumFixtureBytes = size_t{1024} * 1024;
constexpr int StyleCount = 256;
// Frozen from unmodified Notepad4 89b8b36: original-directwrite-comparison,
// measure-02/results.json, first original process, all thirteen observations.
struct Geometry {
    int width;
    intptr_t wrap;
    intptr_t firstVisible;
};
#ifdef WXNOTEPAD_TEST_GDI
// Captured independently from the pinned original GDI renderer; native metrics
// differ from DirectWrite. See the #199 original-gdi-resize/results.json evidence.
constexpr auto Technology = Scintilla::Technology::Default;
constexpr int LineHeight = 28;
constexpr int ScreenLines = 21;
constexpr std::array Reference{Geometry{992, 14147, 14126},  Geometry{872, 15798, 14126},  Geometry{1172, 11512, 11492},
                               Geometry{872, 15798, 11492},  Geometry{1172, 11512, 11492}, Geometry{872, 15798, 11492},
                               Geometry{1172, 11512, 11492}, Geometry{872, 15798, 11492},  Geometry{1172, 11512, 11492},
                               Geometry{872, 15798, 11492},  Geometry{1172, 11512, 11492}, Geometry{872, 15798, 11492},
                               Geometry{1172, 11512, 11492}};
#else
constexpr auto Technology = Scintilla::Technology::DirectWrite;
constexpr int LineHeight = 30;
constexpr int ScreenLines = 19;
constexpr std::array Reference{Geometry{992, 14661, 14642},  Geometry{872, 16144, 14642},  Geometry{1172, 11523, 11505},
                               Geometry{872, 16144, 11505},  Geometry{1172, 11523, 11505}, Geometry{872, 16144, 11505},
                               Geometry{1172, 11523, 11505}, Geometry{872, 16144, 11505},  Geometry{1172, 11523, 11505},
                               Geometry{872, 16144, 11505},  Geometry{1172, 11523, 11505}, Geometry{872, 16144, 11505},
                               Geometry{1172, 11523, 11505}};
#endif
void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}
void Resize(EditorFrame* frame, int width, bool setup) {
    auto* editor = frame->Editor();
    const wxSize desired(width, ViewportHeight);
    for (int attempt = 0; attempt < (setup ? 3 : 1); ++attempt) {
        frame->SetSize(frame->GetSize() + desired - editor->GetClientSize());
        frame->Layout();
        NativePaint::Complete(editor);
        if (editor->GetClientSize() == desired) return;
    }
    Require(false, "the editor reaches the exact original physical viewport");
}
void Check(EditorControl* editor, const Geometry& reference) {
    const auto wrap = editor->Send(Message::WrapCount, 0);
    const auto first = editor->Send(Message::GetFirstVisibleLine);
    if (wrap != reference.wrap || first != reference.firstVisible)
        std::fprintf(stderr, "width=%d wrap=%zd first=%zd expected=%zd,%zd\n", reference.width, wrap, first,
                     reference.wrap, reference.firstVisible);
    Require(wrap == reference.wrap && first == reference.firstVisible,
            "resize wrap and physical viewport match the pinned original observations");
    Require(editor->Send(Message::TextHeight, 0) == LineHeight && editor->Send(Message::LinesOnScreen) == ScreenLines,
            "the shared original font scene has identical line height and screen lines");
    Require(editor->Send(Message::GetCurrentPos) == FixtureBytes - 1 &&
                editor->Send(Message::GetAnchor) == FixtureBytes - 1,
            "native resizing preserves the original selection");
}
class NativeResizeApp final : public wxApp {
    SettingsStore settings_;
    int result_ = 1;

public:
    bool OnInit() override {
        settings_.values = OriginalSettings{};
        settings_.values.fileWatching.mode = FileWatchingMode::None;
        auto* frame = new EditorFrame(&settings_);
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-native-viewport");
            try {
                Require(presented, "the native viewport editor is presented");
                std::filesystem::create_directory(directory);
                std::string text = "name = [";
                for (int item = 0; text.size() < MinimumFixtureBytes; ++item)
                    text += "\"item" + std::to_string(item) + " 中文😀 text words \", ";
                text += "0]\n";
                Require(text.size() == FixtureBytes, "the complete shared original TOML fixture is unchanged");
                const auto path = directory / "viewport.toml";
                TestFiles::WriteBytes(path, text);
                Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "open shared fixture");
                auto* editor = frame->Editor();
                editor->Send(Message::SetTechnology, static_cast<uintptr_t>(Technology));
                editor->Send(Message::SetWrapMode, static_cast<uintptr_t>(Scintilla::Wrap::None));
                // Preserve the original physical 18-pixel em at any monitor DPI.
                for (int style = 0; style < StyleCount; ++style) {
                    editor->Send(Message::StyleSetFont, style, reinterpret_cast<intptr_t>("Consolas"));
                    editor->Send(Message::StyleSetSizeFractional, style,
                                 ReferencePointSizeFractional * ReferenceDpi / editor->GetDPI().y);
                }
                editor->Send(Message::SetZoom, 100);
                editor->Send(Message::SetPhasesDraw, static_cast<uintptr_t>(Scintilla::PhasesDraw::Two));
                editor->Send(Message::SetWrapVisualFlags, 0);
                editor->Send(Message::SetWrapVisualFlagsLocation, 0);
                editor->Send(Message::SetWrapStartIndent, 0);
                editor->Send(Message::SetWrapIndentMode, static_cast<uintptr_t>(Scintilla::WrapIndentMode::Fixed));
                editor->Send(Message::SetExtraAscent, 0);
                editor->Send(Message::SetExtraDescent, 0);
                editor->Send(Message::Colourise, 0, -1);
                NativePaint::Complete(editor);
                wxYield();
                constexpr std::array Margins{25, 0, 26, 0, 0};
                for (size_t index = 0; index < Margins.size(); ++index)
                    editor->Send(Message::SetMarginWidthN, index, Margins[index]);
                editor->Send(Message::SetMarginLeft, 0, 1);
                editor->Send(Message::SetMarginRight, 0, 1);
                editor->Send(Message::SetSel, FixtureBytes - 1, FixtureBytes - 1);
                editor->Send(Message::SetWrapMode, static_cast<uintptr_t>(Scintilla::Wrap::Word));
                NativePaint::Complete(editor);
                Resize(frame, Reference[0].width, true);
                editor->Send(Message::GotoPos, FixtureBytes - 1);
                NativePaint::Complete(editor);
                Check(editor, Reference[0]);
                for (size_t index = 1; index < Reference.size(); ++index) {
                    Resize(frame, Reference[index].width, false);
                    Check(editor, Reference[index]);
                }
                Require(TestEditor::Text(editor) == text && !frame->DocumentIsModified(),
                        "original viewport resizing preserves all document bytes and save point");
                result_ = 0;
                std::puts("PASS: all thirteen pinned original physical viewport, wrap and scroll records");
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
};
} // namespace
wxIMPLEMENT_APP(NativeResizeApp);
