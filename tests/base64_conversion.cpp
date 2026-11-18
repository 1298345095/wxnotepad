#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <wx/stattext.h>
#include <wx/modalhook.h>

namespace {
using Scintilla::Message;
constexpr std::array<const char*, 5> labels = {"Standard Encode", "URL Safe Encode", "Encode as HTML Embedded Image",
                                               "Decode", "Decode as Hex"};
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Menu(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Tools", label);
    Require(id != wxNOT_FOUND, "Tools/Base64 exposes the original command");
    NativeSearch::Command(frame, id);
}
std::string Unhex(const std::string& value) {
    std::string result;
    for (size_t index = 0; index < value.size(); index += 2)
        result += static_cast<char>(std::stoul(value.substr(index, 2), nullptr, 16));
    return result;
}
struct State {
    std::string bytes;
    intptr_t anchor, caret;
    bool modified, undo, redo;
};
State ReadState(std::istringstream& row) {
    State state;
    Require(bool(row >> state.bytes >> state.anchor >> state.caret >> state.modified >> state.undo >> state.redo),
            "read original Base64 state");
    return state;
}
void Check(EditorControl* editor, const State& state) {
    Require(TestEditor::Text(editor) == Unhex(state.bytes), "native command retains original exact bytes");
    TestEditor::RequireSelection(editor, state.anchor, state.caret, "original Base64 selection endpoints");
    Require(bool(editor->Send(Message::GetModify)) == state.modified &&
                bool(editor->Send(Message::CanUndo)) == state.undo &&
                bool(editor->Send(Message::CanRedo)) == state.redo,
            "original Base64 modified state and undo/redo availability");
}
void CapturedCommands(EditorFrame* frame) {
    std::ifstream source{std::filesystem::path(BASE64_REFERENCE)};
    Require(source.is_open(), "open frozen original native Base64 samples");
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("base64-reference");
    std::filesystem::create_directory(directory);
    std::string line, currentSuffix = "-";
    std::getline(source, line);
    unsigned cases = 0;
    while (std::getline(source, line)) {
        std::istringstream row(line);
        unsigned command, eol;
        std::string suffix, bytes;
        intptr_t anchor, caret;
        Require(bool(row >> command >> suffix >> eol >> bytes >> anchor >> caret) && command < labels.size(),
                "read original Base64 command sample");
        if (suffix != currentSuffix) {
            editor->Send(Message::SetSavePoint);
            const auto path = directory / ("image" + (suffix == "." ? std::string() : suffix));
            TestFiles::WriteBytes(path, "");
            Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
                    "open original HTML current-path fixture");
            currentSuffix = suffix;
        }
        const auto input = Unhex(bytes);
        TestEditor::ResetStream(editor, "");
        editor->Send(Message::AddText, input.size(), reinterpret_cast<intptr_t>(input.data()));
        editor->Send(Message::SetEOLMode, eol);
        editor->Send(Message::SetSel, anchor, caret);
        editor->Send(Message::EmptyUndoBuffer);
        editor->Send(Message::SetSavePoint);
        const auto after = ReadState(row), undo = ReadState(row), redo = ReadState(row);
        try {
            Menu(frame, labels[command]);
            Check(editor, after);
            editor->Send(Message::Undo);
            Check(editor, undo);
            editor->Send(Message::Redo);
            Check(editor, redo);
        } catch (...) {
            std::fprintf(stderr, "original Base64 sample %u, command %s, input %s\n", cases, labels[command],
                         bytes.c_str());
            throw;
        }
        ++cases;
    }
    Require(source.eof() && cases == 498, "replay every independently captured original Base64 command");
    std::filesystem::remove_all(directory);
    std::printf("PASS: %u original Base64 byte/selection/history samples\n", cases);
}
class RectangleFeedback final : public wxModalDialogHook {
public:
    RectangleFeedback() { Register(); }
    int Enter(wxDialog*) override {
        ++shown;
        return wxID_OK;
    }
    int shown = 0;
};
void CommandStates(EditorFrame* frame) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetSavePoint);
    Require(frame->NewDocument(), "start unnamed selection-state scenarios");
    for (size_t command = 0; command < labels.size(); ++command) {
        const std::string before = command < 3 ? "XabcY" : "XZm9vY";
        const std::string after = command < 2    ? "XYWJjY"
                                  : command == 2 ? "X<img src=\"data:image/;base64,YWJj\" />Y"
                                  : command == 3 ? "XfooY"
                                                 : "X66 6F 6FY";
        const int id = frame->GetMenuBar()->FindMenuItem("Tools", labels[command]);
        Require(id != wxNOT_FOUND && frame->GetMenuBar()->FindItem(id)->GetItemLabel().Find('\t') == wxNOT_FOUND,
                "all five original Base64 entries have no invented shortcuts");
        TestEditor::ResetStream(editor, before.c_str(), 1, 1);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(!frame->GetMenuBar()->IsEnabled(id), "Base64 menu requires an original nonempty selection");
        Menu(frame, labels[command]);
        Require(TestEditor::Text(editor) == before && !editor->Send(Message::CanUndo),
                "empty direct invocation preserves bytes and history");
        for (bool readOnly : {false, true}) {
            TestEditor::ResetStream(editor, before.c_str(), before.size() - 1, 1);
            editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
            editor->Send(Message::Undo);
            editor->Send(Message::SetSel, before.size() - 1, 1);
            editor->Send(Message::SetReadOnly, readOnly);
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            Require(frame->GetMenuBar()->IsEnabled(id), "selected read-only text retains original Base64 availability");
            Menu(frame, labels[command]);
            Require(TestEditor::Text(editor) == (readOnly ? before : after), "main replacement keeps original bytes");
            const auto anchor = readOnly ? std::min(before.size(), after.size() - 1) : after.size() - 1;
            TestEditor::RequireSelection(editor, static_cast<intptr_t>(anchor), 1,
                                         "original read-only replacement still adjusts and clamps reverse endpoints");
            Require(bool(editor->Send(Message::GetModify)) == !readOnly &&
                        bool(editor->Send(Message::CanUndo)) == !readOnly,
                    "read-only replacement preserves the original save point and edit history");
            editor->Send(Message::SetReadOnly, 0);
            Require(bool(editor->Send(Message::CanRedo)) == readOnly,
                    "successful edits clear pending Redo; read-only attempts retain it");
        }
        for (const bool empty : {false, true}) {
            TestEditor::ResetStream(editor, "abc\ndef");
            editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
            editor->Send(Message::SetRectangularSelectionAnchor, 1);
            editor->Send(Message::SetRectangularSelectionCaret, empty ? 1 : 6);
            RectangleFeedback feedback;
            Menu(frame, labels[command]);
            Require(feedback.shown == (empty ? 0 : 1) && TestEditor::Text(editor) == "abc\ndef" &&
                        !editor->Send(Message::CanUndo) && editor->Send(Message::SelectionIsRectangle),
                    "original empty gate precedes rectangular rejection and its notification");
        }
        if (command >= 3) {
            TestEditor::ResetStream(editor, "X!Y", 2, 1);
            editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
            editor->Send(Message::Undo);
            editor->Send(Message::SetSel, 2, 1);
            Menu(frame, labels[command]);
            Require(TestEditor::Text(editor) == "X!Y" && !editor->Send(Message::GetModify) &&
                        !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                    "zero decoded output retains save point and executable pending Redo");
            TestEditor::RequireSelection(editor, 2, 1, "zero output retains reverse endpoints");
        }
    }
    std::puts(
        "PASS: Base64 command availability, original empty/read-only/rectangle/zero-result and pending Redo rules");
}
#ifdef __WXMSW__
std::string HexBytes(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char byte : bytes) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}
class AnsiWarning final : public wxTimer {
public:
    explicit AnsiWarning(int response) : response_(response) { Start(20); }
    int shown = 0;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() || dialog->GetTitle() != "Encoding character loss") continue;
            Stop();
            ++shown;
            for (auto* child : dialog->GetChildren())
                if (auto* text = wxDynamicCast(child, wxStaticText))
                    std::printf("notification=%s\n", text->GetLabel().utf8_str().data());
            NativeSearch::Click(dialog, response_);
            return;
        }
    }

private:
    int response_;
};
void AnsiInvalidProbe(EditorFrame* frame, const wxString& output) {
    const auto directory = std::filesystem::path(output.ToStdWstring());
    std::filesystem::create_directories(directory);
    const auto path = directory / "ansi.txt";
    TestFiles::WriteBytes(path, "");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
            "open empty named ANSI diagnostic");
    const int native = frame->GetMenuBar()->FindMenuItem("File", "ANSI");
    Require(native != wxNOT_FOUND && GetACP() == 936, "focused original ANSI sample requires its actual CP936 locale");
    NativeSearch::Command(frame, native);
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "gQ==", 0, 4);
    Require(frame->Save().status == SaveStatus::Saved, "save ASCII diagnostic baseline");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    std::printf("native_checked=%d encoding=%d core_codepage=%lld baseline=%s\n",
                frame->GetMenuBar()->IsChecked(native), static_cast<int>(frame->DocumentEncoding()),
                static_cast<long long>(editor->Send(Message::GetCodePage)), HexBytes(TestFiles::Bytes(path)).c_str());
    Menu(frame, "Decode");
    std::printf("decoded=%s anchor=%lld caret=%lld modified=%lld undo=%lld redo=%lld\n",
                HexBytes(TestEditor::Text(editor)).c_str(), static_cast<long long>(editor->Send(Message::GetAnchor)),
                static_cast<long long>(editor->Send(Message::GetCurrentPos)),
                static_cast<long long>(editor->Send(Message::GetModify)),
                static_cast<long long>(editor->Send(Message::CanUndo)),
                static_cast<long long>(editor->Send(Message::CanRedo)));
    {
        AnsiWarning warning(wxID_CANCEL);
        const auto status = frame->Save().status;
        std::printf("cancel_save_status=%d notifications=%d disk=%s\n", static_cast<int>(status), warning.shown,
                    HexBytes(TestFiles::Bytes(path)).c_str());
    }
    {
        AnsiWarning warning(wxID_OK);
        const auto status = frame->Save().status;
        std::printf("accept_save_status=%d notifications=%d disk=%s\n", static_cast<int>(status), warning.shown,
                    HexBytes(TestFiles::Bytes(path)).c_str());
    }
}
#endif
class Base64App final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame(&settings_);
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor presents the owned editor");
#ifdef __WXMSW__
                wxString diagnostic;
                if (wxGetEnv("WXNOTEPAD_BASE64_ANSI_PROBE", &diagnostic)) {
                    AnsiInvalidProbe(frame, diagnostic);
                    result_ = 0;
                    frame->Editor()->Send(Message::SetSavePoint);
                    frame->Destroy();
                    return;
                }
#endif
                TestEditor::ResetStream(frame->Editor(), "abc", 0, 3);
                Menu(frame, "Standard Encode");
                Require(TestEditor::Text(frame) == "YWJj", "original standard Base64 bytes");
                frame->Editor()->Send(Message::SetSavePoint);
                Require(frame->NewDocument(), "start unnamed original-path sample");
                frame->SelectEncoding(EncodingChoice(FileEncoding::Utf8));
                Require(frame->DocumentEncoding() == FileEncoding::Utf8, "use the original UTF-8 edit buffer");
                CapturedCommands(frame);
                CommandStates(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Editor()->Send(Scintilla::Message::SetSavePoint);
            frame->Destroy();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    SettingsStore settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(Base64App);
