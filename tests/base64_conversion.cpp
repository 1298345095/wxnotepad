#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include <print>
#include "native_completion_helpers.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <vector>
#include <wx/stattext.h>
#include <wx/modalhook.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>

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
void Backspace(EditorFrame* frame) {
#ifdef __WXMSW__
    BYTE previous[256];
    Require(GetKeyboardState(previous), "read owned thread key state");
    BYTE keys[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(keys));
    keys[VK_CONTROL] = keys[VK_MENU] = keys[VK_SHIFT] = 0;
    Require(SetKeyboardState(keys), "set plain native Backspace modifiers");
    NativeCompletion::Key(frame->Editor(), VK_BACK);
    SetKeyboardState(previous);
#else
    NativeCompletion::Key(frame->Editor(), GDK_KEY_BackSpace);
#endif
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
            std::println(stderr, "original Base64 sample {:d}, command {}, input {}", cases, labels[command],
                         bytes.c_str());
            throw;
        }
        ++cases;
    }
    Require(source.eof() && cases == 498, "replay every independently captured original Base64 command");
    std::filesystem::remove_all(directory);
    std::println("PASS: {:d} original Base64 byte/selection/history samples", cases);
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
class CharacterFeedback final : public wxTimer {
public:
    CharacterFeedback() { Start(20); }
    wxString payload;
    int shown = 0;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal()) continue;
            auto* text = wxDynamicCast(dialog->FindWindowByName("Character information", dialog), wxStaticText);
            if (!text) continue;
            Stop();
            ++shown;
            payload = text->GetLabelText();
            NativeSearch::Click(dialog, wxID_OK);
            return;
        }
    }
};
std::string ObserveCharacterInfo(EditorFrame* frame) {
#ifdef __WXGTK__
    NativeKeys::Shortcut(frame, 'A', true); // Gives Wayland the real owned input serial for clipboard publication.
#endif
    {
        wxClipboardLocker clipboard;
        if (!clipboard) throw std::runtime_error("open owned clipboard for Character Info seed");
        Require(wxTheClipboard->SetData(new wxTextDataObject("owned Base64 character-info offer")),
                "seed an owned native offer before character information");
    }
    const int command = frame->GetMenuBar()->FindMenuItem("Edit", "Show Character Info");
    Require(command != wxNOT_FOUND, "reachable native Character Info command exists");
    CharacterFeedback feedback;
    NativeSearch::Command(frame, command);
    Require(feedback.shown == 1 && !feedback.payload.empty(), "native Character Info displays its technical payload");
    wxTextDataObject copied;
    {
        wxClipboardLocker clipboard;
        if (!clipboard) throw std::runtime_error("open owned clipboard for Character Info result");
        Require(wxTheClipboard->GetData(copied) && copied.GetText() == feedback.payload,
                "Character Info copies the same native-byte interpretation shown in its feedback");
    }
    return feedback.payload.utf8_string();
}
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
void NativeAnsiBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetSavePoint);
    Require(frame->NewDocument(), "start native ANSI byte preservation scenarios");
    const auto directory = TestFiles::TemporaryDirectory("base64-native-ansi");
    std::filesystem::create_directory(directory);
    const auto path = directory / "ansi.txt";
    TestFiles::WriteBytes(path, "");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "open empty named native ANSI fixture");
    const int ansi = frame->GetMenuBar()->FindMenuItem("File", "ANSI");
    Require(ansi != wxNOT_FOUND, "supported native encoding exposes its real ANSI command");
    NativeSearch::Command(frame, ansi);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsChecked(ansi), "logical native ANSI identity is selected");
    struct Sample {
        const char* encoded;
        std::string bytes;
    };
    std::vector<Sample> samples = {{"gQ==", "\x{81}"}, {"QYE=", "A\x{81}"}};
    if (frame->DocumentEncoding() == FileEncoding::Gbk) {
        samples.push_back({"QdbQzsSB", "A\x{d6}\x{d0}\x{ce}\x{c4}\x{81}"});
        samples.push_back({"qKbW0A==", "\x{a8}\x{a6}\x{d6}\x{d0}"});
        samples.push_back({"ouM=", "\x{a2}\x{e3}"}); // Native Euro alias must retain its original two bytes.
    } else if (frame->DocumentEncoding() == FileEncoding::Utf8) {
        samples.push_back({"5Lit5paH", "中文"});
    }
    for (const auto& sample : samples) {
        TestEditor::ResetStream(editor, sample.encoded, 0, static_cast<intptr_t>(std::strlen(sample.encoded)));
        Require(frame->Save().status == SaveStatus::Saved, "save native ANSI ASCII baseline");
        Menu(frame, "Decode");
        Require(editor->Send(Message::GetModify) && editor->Send(Message::CanUndo),
                "native ANSI decode is one modified reversible edit");
        int lossPrompts = 0;
        const auto save = [&] {
            const auto result = frame->Save([&] {
                ++lossPrompts;
                return EncodingLossChoice::Cancel;
            });
            Require(result.status == SaveStatus::Saved && lossPrompts == 0,
                    "original native ANSI bytes save without a conversion or loss notification");
        };
        save();
        Require(TestFiles::Bytes(path) == sample.bytes,
                "native ANSI Save retains original decoded bytes, including 81");
        editor->Send(Message::Undo);
        save();
        Require(TestFiles::Bytes(path) == sample.encoded, "native ANSI Undo Save restores original Base64 input");
        editor->Send(Message::Redo);
        save();
        Require(TestFiles::Bytes(path) == sample.bytes, "native ANSI Redo Save restores original arbitrary bytes");
        editor->Send(Message::SelectAll);
        Menu(frame, "Standard Encode");
        Require(TestEditor::Text(editor) == sample.encoded,
                "native ANSI selected-byte representation encodes losslessly again");
    }
    TestEditor::ResetStream(editor, "gQ==", 0, 4);
    Menu(frame, "Decode");
    const int showHex = frame->GetMenuBar()->FindMenuItem("Edit", "Show Hex Code");
    Require(showHex != wxNOT_FOUND, "reachable Show Hex Code command exists");
    NativeSearch::Command(frame, showHex);
    const auto end = editor->Send(Message::GetLength);
    TestEditor::RequireSelection(editor, end - 4, end, "Show Hex Code selects the inserted original [81]");
    Require(TestEditor::Text(editor).ends_with("[81]"),
            "Base64 Decode 81 feeds the same native-byte Show Hex boundary");
    const auto saveCombo = [&] {
        Require(frame->Save([] { return EncodingLossChoice::Cancel; }).status == SaveStatus::Saved,
                "native byte and Show Hex combination saves without conversion loss");
    };
    saveCombo();
    Require(TestFiles::Bytes(path) == "\x{81}[81]", "Decode/Show Hex saves the original raw byte and its annotation");
    editor->Send(Message::Undo);
    saveCombo();
    Require(TestFiles::Bytes(path) == "\x{81}", "one Undo removes only the Show Hex annotation");
    editor->Send(Message::Redo);
    saveCombo();
    Require(TestFiles::Bytes(path) == "\x{81}[81]", "Redo restores the original Show Hex annotation");
    TestEditor::ResetStream(editor, "wg==\noA==", 0, 4, Scintilla::EndOfLine::Lf);
    Menu(frame, "Decode");
    const auto secondLine = editor->Send(Message::PositionFromLine, 1);
    editor->Send(Message::SetSel, secondLine, editor->Send(Message::GetLineEndPosition, 1));
    Menu(frame, "Decode");
    editor->Send(Message::GotoPos, editor->Send(Message::PositionFromLine, 1));
    Backspace(frame);
    saveCombo();
    Require(TestFiles::Bytes(path) == "\x{c2}\x{a0}",
            "whole-line decodes and real native Backspace preserve C2+A0 origin even when the core bytes form UTF-8");
    editor->Send(Message::Undo);
    saveCombo();
    Require(TestFiles::Bytes(path) == "\x{c2}\n\x{a0}", "Undo ordinary Backspace restores LF and both byte origins");
    editor->Send(Message::Redo);
    saveCombo();
    Require(TestFiles::Bytes(path) == "\x{c2}\x{a0}", "Redo ordinary Backspace retains raw byte identity");
    editor->Send(Message::SelectAll);
    Backspace(frame);
    saveCombo();
    Require(TestFiles::Bytes(path).empty(), "native selection deletion removes the raw byte range");
    editor->Send(Message::Undo);
    saveCombo();
    Require(TestFiles::Bytes(path) == "\x{c2}\x{a0}", "Undo deletion restores the removed bytes' origin");
    editor->Send(Message::EmptyUndoBuffer);
    saveCombo();
    Require(TestFiles::Bytes(path) == "\x{c2}\x{a0}", "clearing history retains live native origins");
    editor->Send(Message::SelectAll);
    Menu(frame, "Standard Encode");
    Require(TestEditor::Text(editor) == "wqA=", "composed bytes re-encode as native C2 A0 rather than Unicode NBSP");
    editor->Send(Message::Undo);
    saveCombo();
    Require(TestFiles::Bytes(path) == "\x{c2}\x{a0}", "Undo Base64 re-encoding restores native byte provenance");
    editor->Send(Message::SelectAll);
    NativeSearch::Command(frame, showHex);
    Require(TestEditor::Text(editor).ends_with("[C2 A0]"), "composed raw bytes feed Show Hex as C2 A0");
    editor->Send(Message::Undo);
    editor->Send(Message::SelectAll);
    const auto beforeInfo = TestEditor::Text(editor);
    const auto payload = ObserveCharacterInfo(frame);
    const auto encoding = frame->DocumentEncoding();
    const char* unicode = encoding == FileEncoding::Gbk                                         ? "807D"
                          : encoding == FileEncoding::Windows1252                               ? "00C2"
                          : encoding == FileEncoding::Dos437                                    ? "252C"
                          : encoding == FileEncoding::Big5 || encoding == FileEncoding::Gb18030 ? "003F"
                                                                                                : "00A0";
    Require(payload.starts_with(std::string("Unicode: U+") + unicode + "\n"),
            "native Character Info interprets original C2 A0 bytes in their active native code page");
    Require(TestEditor::Text(editor) == beforeInfo, "character information retains composed document bytes");
    for (const auto encoding : {FileEncoding::Gbk, FileEncoding::Utf8}) {
        editor->Send(Message::SetSavePoint);
        TestFiles::WriteBytes(path, "gQ==");
        Require(frame->Open(TestFiles::Filename(path), encoding).status == OpenStatus::Opened,
                "open explicit GBK/UTF-8 representation control");
        editor->Send(Message::SelectAll);
        Menu(frame, "Decode");
        const auto saved = frame->Save([] { return EncodingLossChoice::Cancel; });
        Require(saved.status == (encoding == FileEncoding::Gbk ? SaveStatus::Failed : SaveStatus::Saved) &&
                    TestFiles::Bytes(path) == (encoding == FileEncoding::Gbk ? "gQ==" : "\x{81}"),
                "explicit GBK retains UTF-8 validation while explicit UTF-8 retains raw byte output");
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: logical native ANSI preserves arbitrary decoded bytes and Undo/Redo Save; explicit encoding rules "
              "retain their boundaries");
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
    TestEditor::ResetStream(editor, "wg==", 0, 4);
    Require(frame->Save().status == SaveStatus::Saved, "save byte-composition baseline");
    Menu(frame, "Decode");
    std::printf("pair_first=%s\n", HexBytes(TestEditor::Text(editor)).c_str());
    editor->Send(Message::AppendText, 4, reinterpret_cast<intptr_t>("oA=="));
    editor->Send(Message::SetSel, 1, 5);
    Menu(frame, "Decode");
    std::printf("pair_second=%s anchor=%lld caret=%lld\n", HexBytes(TestEditor::Text(editor)).c_str(),
                static_cast<long long>(editor->Send(Message::GetAnchor)),
                static_cast<long long>(editor->Send(Message::GetCurrentPos)));
    const auto savePair = [&](const char* phase) {
        AnsiWarning warning(wxID_OK);
        const auto status = frame->Save().status;
        std::printf("pair_%s_status=%d notifications=%d disk=%s\n", phase, static_cast<int>(status), warning.shown,
                    HexBytes(TestFiles::Bytes(path)).c_str());
    };
    savePair("save");
    editor->Send(Message::Undo);
    std::printf("pair_undo=%s\n", HexBytes(TestEditor::Text(editor)).c_str());
    savePair("undo_save");
    editor->Send(Message::Redo);
    std::printf("pair_redo=%s\n", HexBytes(TestEditor::Text(editor)).c_str());
    savePair("redo_save");
    TestEditor::ResetStream(editor, "wg==\noA==", 0, 4, Scintilla::EndOfLine::Lf);
    Require(frame->Save().status == SaveStatus::Saved, "save real line-boundary composition baseline");
    Menu(frame, "Decode");
    editor->Send(Message::SetSel, 2, 6);
    Menu(frame, "Decode");
    std::printf("lines_decoded=%s\n", HexBytes(TestEditor::Text(editor)).c_str());
    editor->Send(Message::GotoPos, 2);
    Backspace(frame);
    std::printf("lines_backspace=%s anchor=%lld caret=%lld\n", HexBytes(TestEditor::Text(editor)).c_str(),
                static_cast<long long>(editor->Send(Message::GetAnchor)),
                static_cast<long long>(editor->Send(Message::GetCurrentPos)));
    savePair("lines_save");
    editor->Send(Message::Undo);
    std::printf("lines_undo=%s\n", HexBytes(TestEditor::Text(editor)).c_str());
    savePair("lines_undo_save");
    editor->Send(Message::Redo);
    std::printf("lines_redo=%s\n", HexBytes(TestEditor::Text(editor)).c_str());
    savePair("lines_redo_save");
    editor->Send(Message::SelectAll);
    Menu(frame, "Standard Encode");
    std::printf("lines_encoded=%s anchor=%lld caret=%lld\n", HexBytes(TestEditor::Text(editor)).c_str(),
                static_cast<long long>(editor->Send(Message::GetAnchor)),
                static_cast<long long>(editor->Send(Message::GetCurrentPos)));
    editor->Send(Message::Undo);
    editor->Send(Message::SelectAll);
    const int hex = frame->GetMenuBar()->FindMenuItem("Edit", "Show Hex Code");
    NativeSearch::Command(frame, hex);
    std::printf("lines_hex=%s anchor=%lld caret=%lld\n", HexBytes(TestEditor::Text(editor)).c_str(),
                static_cast<long long>(editor->Send(Message::GetAnchor)),
                static_cast<long long>(editor->Send(Message::GetCurrentPos)));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 0, 0);
    std::printf("lines_info_hex=%s\n", HexBytes(ObserveCharacterInfo(frame)).c_str());
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
                NativeAnsiBytes(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
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
