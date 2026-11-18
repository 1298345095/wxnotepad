#include "completion_allocation_fault.h"
#include "native_completion_helpers.h"
#include "file_helpers.h"
#include "editor/platform/native_encoding.h"
#include <cstdio>
#include <exception>

namespace {
using enum Scintilla::Message;
constexpr auto Ok = static_cast<uintptr_t>(std::to_underlying(Scintilla::Status::Ok));
constexpr auto BadAlloc = static_cast<intptr_t>(std::to_underlying(Scintilla::Status::BadAlloc));
constexpr int SkipUnavailableProvenance = 77;

void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}

void RawFixture(EditorFrame* frame) {
    auto* editor = frame->Editor();
    editor->Send(SetSavePoint);
    const auto directory = TestFiles::TemporaryDirectory("native-byte-allocation");
    std::filesystem::create_directory(directory);
    const auto path = directory / "ansi.txt";
    TestFiles::WriteBytes(path, "");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "open the named native ANSI allocation fixture");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "ANSI"));
    TestEditor::ResetStream(editor, "/w==", 0, 4);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Tools", "Decode"));
    Require(TestEditor::Text(editor) == std::string(1, '\xff'), "native Base64 Decode installs the literal raw byte");
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    editor->Send(SetSel, 1, 1);
    Require(*editor->RawNativeBytes() == NativeByteRanges{{0, 1}}, "native ANSI Decode activates raw provenance");
    std::filesystem::remove_all(directory);
}

void CheckBytes(EditorControl* editor, std::string_view text, NativeByteRanges ranges) {
    if (TestEditor::Text(editor) != text)
        std::fprintf(stderr, "document length=%zu; expected=%zu\n", TestEditor::Text(editor).size(), text.size());
    Require(TestEditor::Text(editor) == text, "document bytes survive the rejected edit and history traversal");
    Require(*editor->RawNativeBytes() == ranges,
            "raw byte provenance survives the rejected edit and history traversal");
    editor->Send(SetSel, 0, text.size());
    Require(editor->NativeSelectedBytes(true) == text, "native conversion preserves the literal raw byte");
}

void CheckRaw(EditorControl* editor, std::string_view text) {
    CheckBytes(editor, text, {{0, 1}});
}

template <typename Operation> void RejectAllocation(EditorControl* editor, size_t ranges, Operation operation) {
    editor->Send(SetStatus, Ok);
    const auto size = CompletionAllocationFault::ByteVectorAllocationSize(ranges * sizeof(NativeByteRange));
    bool injected = false;
    {
        const CompletionAllocationFault::ScopedFailure failure(size);
        operation();
        injected = failure.Injected();
    }
    Require(injected && editor->Send(GetStatus) == BadAlloc,
            "provenance reservation fails through the native editor and retains BadAlloc status");
    editor->Send(SetStatus, Ok);
}

void CheckRecovery(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const std::string raw(1, '\xff');
    RawFixture(frame);
    RejectAllocation(editor, 2, [&] { editor->Send(InsertText, 1, reinterpret_cast<intptr_t>("x")); });
    CheckRaw(editor, raw);
    Require(!editor->Send(CanUndo) && !editor->Send(GetModify), "rejected insertion retains the clean history");
    std::puts("PASS: rejected insertion preserves original bytes, provenance and save point");
    editor->Send(InsertText, 1, reinterpret_cast<intptr_t>("x"));
    CheckRaw(editor, raw + "x");
    editor->Send(Undo);
    CheckRaw(editor, raw);
    editor->Send(Redo);
    CheckRaw(editor, raw + "x");
    std::puts("PASS: rejected provenance allocation permits later insertion and Undo/Redo");
}

void CheckDeletionAndHistory(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const std::string raw(1, '\xff');
    RawFixture(frame);
    RejectAllocation(editor, 3, [&] { editor->Send(DeleteRange, 0, 1); });
    CheckRaw(editor, raw);
    Require(!editor->Send(CanUndo) && !editor->Send(GetModify), "rejected deletion retains the clean history");
    editor->Send(DeleteRange, 0, 1);
    CheckBytes(editor, "", {});
    Require(editor->Send(CanUndo) && editor->Send(GetModify), "the later deletion remains undoable");

    RejectAllocation(editor, 2, [&] { editor->Send(Undo); });
    CheckBytes(editor, "", {});
    Require(editor->Send(CanUndo) && !editor->Send(CanRedo) && editor->Send(GetModify),
            "rejected Undo retains its action and modified state");
    editor->Send(Undo);
    CheckRaw(editor, raw);
    Require(!editor->Send(CanUndo) && editor->Send(CanRedo) && !editor->Send(GetModify),
            "retried Undo restores the raw byte and its save point");

    RejectAllocation(editor, 3, [&] { editor->Send(Redo); });
    CheckRaw(editor, raw);
    Require(!editor->Send(CanUndo) && editor->Send(CanRedo) && !editor->Send(GetModify),
            "rejected Redo retains its action and save point");
    editor->Send(Redo);
    CheckBytes(editor, "", {});
    editor->Send(Undo);
    CheckRaw(editor, raw);
    editor->Send(InsertText, 1, reinterpret_cast<intptr_t>("z"));
    CheckRaw(editor, raw + "z");
    editor->Send(Undo);
    CheckRaw(editor, raw);
    std::puts("PASS: rejected deletion, Undo and Redo preserve provenance, save point and future editing");
}

void CheckRejectedFork(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const std::string raw(1, '\xff');
    for (const bool deletion : {false, true}) {
        RawFixture(frame);
        editor->Send(SetUndoSelectionHistory, std::to_underlying(Scintilla::UndoSelectionHistoryOption::Enabled));
        editor->Send(BeginUndoAction);
        editor->Send(AddText, 1, reinterpret_cast<intptr_t>("x"));
        editor->Send(SetSel, 2, 0);
        editor->Send(EndUndoAction);
        editor->Send(Undo);
        CheckRaw(editor, raw);
        editor->Send(SetSel, 0, 0);
        RejectAllocation(editor, deletion ? 3 : 2, [&] {
            if (deletion)
                editor->Send(DeleteRange, 0, 1);
            else
                editor->Send(InsertText, 0, reinterpret_cast<intptr_t>("z"));
        });
        CheckRaw(editor, raw);
        Require(!editor->Send(CanUndo) && editor->Send(CanRedo) && !editor->Send(GetModify),
                "rejected fork retains the original redo action and save point");
        editor->Send(Redo);
        TestEditor::RequireSelection(editor, 2, 0, "rejected fork retains the original reverse redo selection");
        CheckRaw(editor, raw + "x");
        editor->Send(Undo);
        CheckRaw(editor, raw);
        editor->Send(Redo);
        CheckRaw(editor, raw + "x");
    }
    std::puts("PASS: rejected insertion/deletion forks retain redo bytes, provenance and reverse selection");
}

class NativeByteAllocationApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native compositor presents the provenance allocation fixture");
                NativeByteRanges raw;
                const auto decoded = NativeDefaultEncodingBuffer(std::string(1, '\xff'), &raw);
                if (NativeEncodingChoice() && decoded == std::string(1, '\xff') && raw == NativeByteRanges{{0, 1}}) {
                    CheckRecovery(frame);
                    CheckDeletionAndHistory(frame);
                    CheckRejectedFork(frame);
                    result_ = 0;
                } else {
                    std::printf("SKIP: native ACP %u does not provide this raw-origin fixture\n", GetACP());
                    result_ = SkipUnavailableProvenance;
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(NativeByteAllocationApp);
