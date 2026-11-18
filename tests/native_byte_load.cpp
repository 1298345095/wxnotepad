#include "native_completion_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include <cstdio>
#include <exception>
#include <memory>

namespace {
using enum Scintilla::Message;
constexpr int SkipUnavailableNativeAnsi = 77;

void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}

void Menu(EditorFrame* frame, const char* label) {
    const auto id = frame->GetMenuBar()->FindMenuItem("Tools", label);
    Require(id != wxNOT_FOUND, "the native Base64 menu command exists");
    NativeSearch::Command(frame, id);
}

void CheckReload(EditorFrame* frame, const char* encoded, const std::string& original) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("native-byte-load");
    std::filesystem::create_directory(directory);
    const auto path = directory / "ansi.txt";
    TestFiles::WriteBytes(path, "");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "open the named native ANSI lifecycle fixture");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "ANSI"));
    editor->Send(SetSavePoint);
    TestEditor::ResetStream(editor, encoded, 0, 4);
    Menu(frame, "Decode");
    Require(frame->Save([] { return EncodingLossChoice::Cancel; }).status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == original,
            "native Decode saves its original arbitrary byte");
    const auto result = frame->ReloadCurrentFile();
    if (result.status != OpenStatus::Opened) std::fprintf(stderr, "Reload: %s\n", result.error.utf8_str().data());
    Require(result.status == OpenStatus::Opened, "reload accepts the bytes produced by native ANSI Decode");
    Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("File", "ANSI")),
            "reload retains actual native ANSI format identity");
    Require(!editor->Send(CanUndo) && !editor->Send(CanRedo) && !editor->Send(GetModify),
            "reload establishes a clean document and history");
    editor->Send(SelectAll);
    Require(editor->NativeSelectedBytes() == original, "reload seeds the original selected native byte identity");
    Menu(frame, "Standard Encode");
    Require(TestEditor::Text(editor) == encoded, "reloaded native ANSI selection encodes its original bytes");
    editor->Send(Undo);
    Require(frame->Save([] { return EncodingLossChoice::Cancel; }).status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == original,
            "Undo of re-encoding retains the loaded native byte origins on Save");
    editor->Send(InsertText, 0, reinterpret_cast<intptr_t>("A"));
    Require(frame->Save([] { return EncodingLossChoice::Cancel; }).status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == "A" + original,
            "ordinary insertion shifts loaded origins and saves original native bytes");
    editor->Send(Undo);
    Require(frame->Save([] { return EncodingLossChoice::Cancel; }).status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == original,
            "Undo restores loaded native origins after an ordinary edit");
    Require(frame->NewDocument() && editor->RawNativeBytes()->empty(), "New clears the old document's origins");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
            "a fresh native ANSI Open accepts the saved decoded bytes");
    Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("File", "ANSI")),
            "fresh Open selects actual native ANSI identity with promotion disabled");
    editor->Send(SelectAll);
    Menu(frame, "Standard Encode");
    Require(TestEditor::Text(editor) == encoded, "fresh Open retains the original selected native bytes");
    editor->Send(Undo);
    editor->Send(SetSavePoint);
    const auto before = TestEditor::Text(editor);
    const auto raw = editor->RawNativeBytes();
    if (original == std::string(1, '\x81')) {
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Failed,
                "explicit UTF-8 loading continues to reject invalid bytes");
        if (GetACP() == 936)
            Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Failed,
                    "explicit GBK loading continues to reject incomplete native input");
        Require(TestEditor::Text(editor) == before && *editor->RawNativeBytes() == *raw,
                "failed explicit-codec load preserves the native document and its origins");
    } else {
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
                "explicit GBK still accepts its original valid alias input");
        Require(editor->RawNativeBytes()->empty(), "explicit-codec replacement clears native byte origins");
    }
    std::printf("PASS: native Decode/Save/Reload/Open/Encode, ordinary edit history and explicit-codec policy (%s)\n",
                encoded);
    std::filesystem::remove_all(directory);
}

class NativeByteLoadApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>(
            TestFiles::Filename(TestFiles::TemporaryDirectory() / "native-byte-load.ini"));
        // Exercise Open's native ANSI source identity. The default true options
        // intentionally promote valid ANSI input to its explicit Unicode codec.
        settings_->values.encoding.loadANSIasUTF8 = false;
        settings_->values.encoding.loadASCIIasUTF8 = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native compositor presents the byte load fixture");
                const auto native = NativeEncodingChoice();
                if (native && native->encoding != FileEncoding::Utf8) {
                    CheckReload(frame, "gQ==", std::string(1, '\x81'));
                    if (GetACP() == 936) CheckReload(frame, "ouM=", std::string("\xa2\xe3", 2));
                    result_ = 0;
                } else {
                    std::printf("SKIP: native legacy ANSI lifecycle is unavailable for ACP %u\n", GetACP());
                    result_ = SkipUnavailableNativeAnsi;
                }
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(NativeByteLoadApp);
