#include "research_profile.h"
#include "prototype_memory.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "settings/settings_store.h"
#include <cstdlib>
#include <filesystem>

namespace {
using Scintilla::Message;
constexpr size_t FixtureBytes = size_t{1024} * 1024;
constexpr int ResizeCount = 12;
std::string Fixture(bool longLine) {
    std::string text = longLine ? "name = [" : "";
    for (unsigned item = 0; text.size() < FixtureBytes; ++item) {
        const auto number = std::to_string(item);
        if (longLine)
            text += "\"item" + number + " 中文😀 text words \", ";
        else
            text += "key" + number + " = \"value" + number +
                    " 中文😀 words repeated for wrapping and native measurement\" # comment" + number + "\n";
    }
    if (longLine) text += "0]\n";
    return text;
}
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Resize(EditorFrame* frame, int width) {
    const auto target = frame->FromDIP(wxSize(width, 400));
    frame->SetClientSize(target);
    const auto deadline = ResearchNow() + 3000000000ULL;
    do {
        frame->Layout();
        NativePaint::Complete(frame->Editor());
        if (frame->GetClientSize() == target && frame->Editor()->GetSize() == target) return;
        wxYield();
        wxMilliSleep(1);
    } while (ResearchNow() < deadline);
    throw std::runtime_error("native resize geometry");
}
void Measure(EditorFrame* frame, const std::filesystem::path& folder, bool longLine) {
    const char* name = longLine ? "long-line-1MiB" : "multi-line-1MiB";
    const auto text = Fixture(longLine);
    const auto path = folder / "fixture.toml";
    TestFiles::WriteBytes(path, text);
    auto* editor = frame->Editor();
    Resize(frame, 680);
    PrototypeMemory(name, "empty");
    editor->Freeze();
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "load fixture");
    editor->Send(Message::Colourise, 0, -1);
    editor->Send(Message::SetCurrentPos, text.size() - 1);
    editor->Send(Message::SetAnchor, text.size() - 1);
    ResearchReset();
    auto cpu = ResearchCpu();
    auto wall = ResearchNow();
    editor->ToggleWordWrap();
    editor->Thaw();
    editor->Send(Message::GotoPos, text.size() - 1);
    NativePaint::Complete(editor);
    wall = ResearchNow() - wall;
    cpu = ResearchCpu() - cpu;
    ResearchReport(name, "wrap-and-first-paint", wall, cpu);
    PrototypeMemory(name, "wrapped");
    Require(editor->Send(Message::WrapCount, 0) > 1, "fixture is wrapped");
    ResearchReset();
    cpu = ResearchCpu();
    wall = ResearchNow();
    for (int pass = 0; pass < ResizeCount; ++pass)
        Resize(frame, pass % 2 ? 800 : 600);
    wall = ResearchNow() - wall;
    cpu = ResearchCpu() - cpu;
    ResearchReport(name, "12-natural-resizes", wall, cpu);
    Require(TestEditor::Text(editor) == text && !frame->DocumentIsModified(), "preserve full text and save point");
    std::printf("CHECK bytes=%zu lines=%lld first-wrap=%lld last-wrap=%lld\n", text.size(),
                static_cast<long long>(editor->Send(Message::GetLineCount)),
                static_cast<long long>(editor->Send(Message::WrapCount, 0)),
                static_cast<long long>(editor->Send(Message::WrapCount, editor->Send(Message::GetLineCount) - 2)));
    PrototypeMemory(name, "after-resizes");
    Require(frame->NewDocument(), "new empty document after clean fixture");
    NativePaint::Complete(editor);
    PrototypeMemory(name, "after-new");
}
class ProbeApp final : public wxApp {
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;

public:
    bool OnInit() override {
        ResearchGuiThread = std::this_thread::get_id();
        if (argc != 3) return false;
        ResearchRequestedThreads = static_cast<unsigned>(std::stoul(wxString(argv[1]).ToStdString()));
        const bool longLine = wxString(argv[2]) == "long";
        SetExitOnFrameDelete(false);
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        settings_->values.saveSettings = false;
        settings_->values.fileWatching.mode = FileWatchingMode::None;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame, longLine](bool presented) {
            const auto folder = TestFiles::TemporaryDirectory("wxnotepad-layout-research");
            try {
                Require(presented, "native frame presentation");
                std::filesystem::create_directory(folder);
                Measure(frame, folder, longLine);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            std::filesystem::remove_all(folder);
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
};
} // namespace
wxIMPLEMENT_APP(ProbeApp);
