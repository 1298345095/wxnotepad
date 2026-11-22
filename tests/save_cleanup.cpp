#include "native_search_helpers.h"
#include <array>
#include "settings/settings_store.h"
#include "file_helpers.h"
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/timer.h>
#include <filesystem>
#include <utility>
#include <fstream>

namespace {
using Scintilla::Message;
using TestFiles::Bytes;
using TestFiles::Filename;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Load(EditorControl* editor, const std::string& text) {
    using enum Scintilla::Message;
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    editor->Send(SetEOLMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::EndOfLine::Lf)));
}
class LineEndingOptionsDriver final : public wxTimer {
public:
    LineEndingOptionsDriver(bool& observed, bool fix, bool strip, bool accept, int mode)
        : observed_(observed), fix_(fix), strip_(strip), accept_(accept), mode_(mode) {
        Start(10);
    }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || dialog->GetTitle() != "Line Endings" || !dialog->IsModal() || !dialog->IsShown()) continue;
            auto* mode = wxDynamicCast(wxWindow::FindWindowByName("Default line ending mode", dialog), wxChoice);
            auto* warn = wxDynamicCast(
                wxWindow::FindWindowByLabel("Show inconsistent line endings warning on loading.", dialog), wxCheckBox);
            auto* consistent = wxDynamicCast(
                wxWindow::FindWindowByLabel("&Ensure consistent line endings when saving.", dialog), wxCheckBox);
            auto* blanks =
                wxDynamicCast(wxWindow::FindWindowByLabel("&Strip trailing blanks when saving.", dialog), wxCheckBox);
            observed_ = mode && mode->GetCount() == 3 && mode->GetString(0) == "Windows (CR+LF)" &&
                        mode->GetString(1) == "Unix/macOS (LF)" && mode->GetString(2) == "Classic Mac OS (CR)" &&
                        warn && consistent && blanks;
            Stop();
            if (observed_) {
                if (mode_ >= 0) mode->SetSelection(mode_);
                if (warn->GetValue()) NativeSearch::Toggle(warn, false);
                if (consistent->GetValue() != fix_) NativeSearch::Toggle(consistent, fix_);
                if (blanks->GetValue() != strip_) NativeSearch::Toggle(blanks, strip_);
            }
            NativeSearch::Click(dialog, observed_ && accept_ ? wxID_OK : wxID_CANCEL);
            return;
        }
    }

private:
    bool& observed_;
    bool fix_, strip_, accept_;
    int mode_;
};
void Options(EditorFrame* frame, bool fix, bool strip, bool accept = true, int defaultMode = -1) {
    bool observed = false;
    LineEndingOptionsDriver driver(observed, fix, strip, accept, defaultMode);
    // Native events may run before menu dispatch creates the modal dialog.
    wxYield();
    auto* file = frame->GetMenuBar()->GetMenu(frame->GetMenuBar()->FindMenu("File"));
    auto* lineEndings = file->FindItem(file->FindItem("Line Endings"))->GetSubMenu();
    const auto id = lineEndings->FindItem("Default...");
    Require(id != wxNOT_FOUND, "original Default Line Ending options must be exposed");
    NativeSearch::Command(frame, id);
    Require(observed, "original line-ending selector and three original checkboxes must be exposed");
}
void Defaults(EditorFrame* frame) {
    using enum Scintilla::EndOfLine;
    auto* editor = frame->Editor();
    const std::string source = "keep\r\ncurrent";
    for (const auto& [defaultMode, endOfLine] : {std::pair{1, Lf}, std::pair{2, Cr}, std::pair{0, CrLf}}) {
        Load(editor, source);
        Options(frame, false, false, true, defaultMode);
        Require(NativeSearch::Text(editor) == source &&
                    editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(std::to_underlying(Lf)),
                "changing the original default mode must retain current document text and insertion mode");
        Require(frame->NewDocument() &&
                    editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(std::to_underlying(endOfLine)),
                "the default line ending selector must apply to the next New document in original order");
    }
    Options(frame, false, false, false, 1);
    Require(frame->NewDocument() &&
                editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(std::to_underlying(CrLf)),
            "Cancel must retain the configured default mode");
    std::puts("PASS: default EOL selector order, current document retention, next-New application and Cancel");
}
struct Policy {
    bool fix;
    bool strip;
    const char* expected;
};

void CheckPolicy(EditorFrame* frame, const std::filesystem::path& directory, const std::string& source,
                 const Policy& policy) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    Options(frame, policy.fix, policy.strip);
    Load(editor, source);
    editor->Send(SetSel, source.size(), 1);
    const auto output = directory / "policy.txt";
    const auto result = frame->SaveAs(Filename(output));
    if (result.status != SaveStatus::Saved || NativeSearch::Text(editor) != policy.expected ||
        Bytes(output) != policy.expected)
        std::fprintf(stderr, "Policy fix=%d strip=%d status=%d length=%zu expected=%zu error=%s\n", policy.fix,
                     policy.strip, std::to_underlying(result.status), NativeSearch::Text(editor).size(),
                     std::string(policy.expected).size(), result.error.utf8_str().data());
    Require(result.status == SaveStatus::Saved && Bytes(output) == policy.expected &&
                NativeSearch::Text(editor) == policy.expected,
            "each cleanup policy must independently change buffer and disk only as originally configured");
    Require(editor->Send(GetEOLMode) == static_cast<intptr_t>(std::to_underlying(Scintilla::EndOfLine::Lf)) &&
                editor->Send(GetAnchor) == static_cast<intptr_t>(std::string(policy.expected).size()) &&
                editor->Send(GetCurrentPos) == 1 && !editor->Send(GetModify),
            "cleanup must retain EOL mode, reversed selection and establish the final save point");
    Require(frame->Save().status == SaveStatus::Saved && !editor->Send(GetModify), "repeat Save must remain clean");
    if (policy.strip) {
        editor->Send(Undo);
        Require(NativeSearch::Text(editor) == (policy.fix ? "a \t\nb \t\nc \t" : source),
                "first Undo must restore the independent trailing-blank cleanup");
    }
    if (policy.fix) {
        editor->Send(Undo);
        Require(NativeSearch::Text(editor) == source, "second Undo must restore the independent EOL cleanup");
    }
    Require(!editor->Send(CanUndo), "repeat Save must not add undo actions");
    if (policy.fix || policy.strip) {
        Require(editor->Send(GetModify), "Undo away from the successful cleanup save point must be dirty");
        if (policy.fix) editor->Send(Redo);
        if (policy.strip) editor->Send(Redo);
        Require(!editor->Send(GetModify) && NativeSearch::Text(editor) == policy.expected,
                "Redo to the cleanup save point must restore clean final text");
    }
}

void Policies(EditorFrame* frame, const std::filesystem::path& directory) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const std::string source = "a \t\r\nb \t\nc \t";
    const std::array<Policy, 4> policies = {{{false, false, "a \t\r\nb \t\nc \t"},
                                             {true, false, "a \t\nb \t\nc \t"},
                                             {false, true, "a\r\nb\nc"},
                                             {true, true, "a\nb\nc"}}};
    for (const auto& policy : policies) {
        CheckPolicy(frame, directory, source, policy);
    }
    Options(frame, false, false);
    Options(frame, true, true, false);
    Load(editor, source);
    Require(frame->Save().status == SaveStatus::Saved && NativeSearch::Text(editor) == source,
            "Cancel in the options dialog must discard both policy changes");
    Options(frame, true, true);
    for (const auto& mode :
         {std::pair{Scintilla::EndOfLine::CrLf, "a\r\nb\r\nc\r\n"}, std::pair{Scintilla::EndOfLine::Lf, "a\nb\nc\n"},
          std::pair{Scintilla::EndOfLine::Cr, "a\rb\rc\r"}}) {
        Load(editor, "a \nb\t\r\nc \r");
        editor->Send(SetEOLMode, static_cast<uintptr_t>(std::to_underlying(mode.first)));
        Require(frame->Save().status == SaveStatus::Saved && NativeSearch::Text(editor) == mode.second &&
                    editor->Send(GetEOLMode) == static_cast<intptr_t>(std::to_underlying(mode.first)),
                "pre-save cleanup must normalize to each current EOL mode and retain that mode");
    }
    Load(editor, "中 \r\nx \t");
    editor->Send(SetSel, 1, 2);
    Require(frame->Save().status == SaveStatus::Saved && NativeSearch::Text(editor) == "中\nx" &&
                editor->Send(GetAnchor) == 3 && editor->Send(GetCurrentPos) == 3,
            "original EOL cleanup must fix split Unicode selection positions before stripping blanks");
    for (const auto& sample : {std::pair{" \ta\t \r\n \tb \t", " \ta\n \tb"}, std::pair{"a \r\n\t\r\n\r\n", "a\n\n\n"},
                               std::pair{"\t  ", ""}, std::pair{"", ""}}) {
        Load(editor, sample.first);
        Require(frame->Save().status == SaveStatus::Saved && NativeSearch::Text(editor) == sample.second &&
                    Bytes(directory / "policy.txt") == sample.second,
                "cleanup must retain indentation, final empty lines and absence of a final newline");
    }
    Load(editor, "a\nb");
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Undo);
    Require(frame->Save().status == SaveStatus::Saved && editor->Send(CanRedo) && !editor->Send(CanUndo),
            "no-op cleanup must retain the pending redo history");
    editor->Send(Redo);
    Require(NativeSearch::Text(editor) == "a\nb!", "no-op Save must retain redo bytes");
    Load(editor, source);
    editor->Send(SetReadOnly, 1);
    Require(frame->Save().status == SaveStatus::Saved && NativeSearch::Text(editor) == source &&
                Bytes(directory / "policy.txt") == source && !editor->Send(CanUndo),
            "original read-only save must bypass both cleanup policies");
    editor->Send(SetReadOnly, 0);
    std::puts("PASS: native original options; independent/together policies, selection, undo order, final lines, no-op "
              "and read-only");
}
void SelectionScopes(EditorFrame* frame, const std::filesystem::path& directory) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const std::string source = "first \t\nsecond \t\nlast \t";
    const std::string cleaned = "first\nsecond\nlast";
    Options(frame, false, true);
    for (const auto mode : {Scintilla::SelectionMode::Stream, Scintilla::SelectionMode::Rectangle}) {
        Load(editor, source);
        editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(mode)));
        if (mode == Scintilla::SelectionMode::Rectangle) {
            editor->Send(SetRectangularSelectionAnchor, 3);
            editor->Send(SetRectangularSelectionCaret, 1);
        } else {
            editor->Send(SetSel, 3, 1);
        }
        const auto output = directory / "selection-scope.txt";
        Require(frame->SaveAs(Filename(output)).status == SaveStatus::Saved && Bytes(output) == cleaned &&
                    NativeSearch::Text(editor) == cleaned,
                "save cleanup must strip every line even when the selection contains no trailing blanks");
        Require(editor->Send(GetSelectionMode) == static_cast<intptr_t>(std::to_underlying(mode)),
                "save cleanup must retain the selection mode");
        editor->Send(Undo);
        Require(NativeSearch::Text(editor) == source && !editor->Send(CanUndo),
                "whole-document cleanup from a partial selection must undo in one step");
        editor->Send(Redo);
        Require(NativeSearch::Text(editor) == cleaned && !editor->Send(GetModify),
                "redo must restore the saved cleanup bytes and save point");
    }
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Stream)));
    Options(frame, true, true);
}

void Encodings(EditorFrame* frame, const std::filesystem::path& directory) {
    using namespace std::string_literals;
    struct Sample {
        const char* name;
        std::string source;
        std::string expected;
        std::optional<FileEncoding> encoding;
    };
    const std::array<Sample, 3> samples = {
        {{"utf8", "中 \t\r\nnext \nlast\t", "中\nnext\nlast", FileEncoding::Utf8},
         {"utf16", "\x{ff}\x{fe}\x{2d}\x{4e} \0\t\0\r\0\n\0n\0 \0\n\0z\0\t\0"s,
          "\x{ff}\x{fe}\x{2d}\x{4e}\n\0n\0\n\0z\0"s, FileEncoding::Utf16Le},
         {"gbk", "\x{d6}\x{d0} \t\r\nnext \nlast\t", "\x{d6}\x{d0}\nnext\nlast", FileEncoding::Gbk}}};
    for (const auto& sample : samples) {
        const auto input = directory / (std::string(sample.name) + ".txt");
        const auto output = directory / (std::string(sample.name) + "-as.txt");
        std::ofstream(input, std::ios::binary) << sample.source;
        frame->Editor()->Send(Message::SetSavePoint);
        Require(frame->Open(Filename(input), sample.encoding).status == OpenStatus::Opened, "encoding fixture opens");
        Options(frame, false, false);
        Require(frame->SaveAs(Filename(output)).status == SaveStatus::Saved && Bytes(output) == sample.source,
                "disabled cleanup must preserve exact UTF-8, UTF-16LE and GBK bytes");
        Options(frame, true, true);
        frame->Editor()->Send(Message::SetEOLMode,
                              static_cast<uintptr_t>(std::to_underlying(Scintilla::EndOfLine::Lf)));
        Require(frame->Save().status == SaveStatus::Saved && Bytes(output) == sample.expected &&
                    Bytes(input) == sample.source,
                "cleanup must save independent UTF-8, UTF-16LE and GBK literals without changing the original file");
        std::printf("BYTES %s:", sample.name);
        for (const unsigned char byte : Bytes(output))
            std::printf(" %02x", byte);
        std::puts("");
    }
    std::puts("PASS: independent UTF-8/UTF-16LE/GBK disabled and cleanup destination bytes");
}
void Failures(EditorFrame* frame, const std::filesystem::path& directory) {
    auto* editor = frame->Editor();
    Options(frame, true, true);
    const std::string source = "中 \t\r\n😀 \t\nlast \t";
    const auto seed = directory / "seed-gbk.txt";
    std::ofstream(seed, std::ios::binary) << "\x{d6}\x{d0}";
    editor->Send(Message::SetSavePoint);
    Require(frame->Open(Filename(seed), FileEncoding::Gbk).status == OpenStatus::Opened, "GBK seed must open");
    Load(editor, source);
    const auto identity = frame->DocumentPath();
    Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled && NativeSearch::Text(editor) == source &&
                !editor->Send(Message::CanUndo),
            "cancelled filename selection must not start cleanup");
    Require(frame->SaveAs(Filename(directory / "missing" / "file.txt")).status == SaveStatus::Failed &&
                NativeSearch::Text(editor) == source && !editor->Send(Message::CanUndo),
            "failed destination creation must precede cleanup");
    const auto blocked = directory / "blocked";
    std::filesystem::create_directory(blocked);
    std::ofstream(blocked / "sentinel") << "keep";
    Require(frame->SaveAs(Filename(blocked)).status == SaveStatus::Failed && NativeSearch::Text(editor) == source &&
                Bytes(blocked / "sentinel") == "keep",
            "unopenable destination must not run cleanup");
    const auto output = directory / "lossy.txt";
    bool sawCleaned = false;
    Require(frame->SaveAs(
                     Filename(output),
                     [&sawCleaned, editor] {
                         sawCleaned = NativeSearch::Text(editor) == "中\n😀\nlast";
                         return EncodingLossChoice::Cancel;
                     }).status == SaveStatus::Cancelled &&
                sawCleaned && NativeSearch::Text(editor) == "中\n😀\nlast" && editor->Send(Message::GetModify) &&
                frame->DocumentPath() == identity && Bytes(seed) == "\x{d6}\x{d0}" && !std::filesystem::exists(output),
            "lossy cancellation must retain undoable cleanup while preserving disk and identity");
    editor->Send(Message::Undo);
    Require(NativeSearch::Text(editor) == "中 \t\n😀 \t\nlast \t",
            "cancelled cleanup keeps strip as the last undo action");
    editor->Send(Message::Undo);
    Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify),
            "cancelled cleanup Undo must return to the old save point");
    Require(frame->SaveAs(Filename(output), [] { return EncodingLossChoice::Replace; }).status == SaveStatus::Saved &&
                Bytes(output) == "\x{d6}\x{d0}\n??\nlast" && NativeSearch::Text(editor) == "中\n😀\nlast" &&
                !editor->Send(Message::GetModify),
            "accepted GBK loss must save cleaned replacement bytes without rewriting Unicode text");
    Load(editor, source);
    const auto lateFailure = directory / "late-failure.txt";
    std::ofstream(lateFailure) << "original";
    const auto preserved = directory / "preserved.txt";
    Require(frame->SaveAs(
                     Filename(lateFailure),
                     [&lateFailure, &preserved] {
                         Require(Bytes(lateFailure) == "original",
                                 "safe preflight must not truncate the destination before the prompt");
                         std::filesystem::rename(lateFailure, preserved);
                         std::filesystem::create_directory(lateFailure);
                         std::ofstream(lateFailure / "sentinel") << "keep";
                         return EncodingLossChoice::Replace;
                     }).status == SaveStatus::Failed &&
                NativeSearch::Text(editor) == "中\n😀\nlast" && editor->Send(Message::GetModify) &&
                Bytes(preserved) == "original" && Bytes(lateFailure / "sentinel") == "keep",
            "late replacement failure must retain original cleanup edits and protect existing disk contents");
    editor->Send(Message::Undo);
    editor->Send(Message::Undo);
    Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify),
            "failed save retains the original cleanup undo sequence and save point");
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        Require(entry.path().filename().string().find(".wxnotepad-") == std::string::npos,
                "cancelled and failed cleanup saves must remove temporary siblings");
    std::puts("PASS: filename cancel/preflight failure, lossy cancel, accepted loss and late write failure preserve "
              "original edit/undo semantics and safe disk state");
}
class CleanupApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-cleanup");
            try {
                std::filesystem::create_directory(directory);
                Policies(frame, directory);
                SelectionScopes(frame, directory);
                Encodings(frame, directory);
                Failures(frame, directory);
                Defaults(frame);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
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
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(CleanupApp);
