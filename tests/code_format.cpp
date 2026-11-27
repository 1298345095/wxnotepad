#include "diagnostics/print.h"
#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "syntax_menu_helpers.h"
#include "syntax/syntax_catalog.h"
#include "file_helpers.h"
#include <array>
#include <fstream>
#include <sstream>
#include <wx/menu.h>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
using enum Scintilla::Message;
constexpr std::array referenceLanguages = {
    SyntaxLanguage::Css,        SyntaxLanguage::Scss,       SyntaxLanguage::Less,
    SyntaxLanguage::Hss,        SyntaxLanguage::JavaScript, SyntaxLanguage::Jsx,
    SyntaxLanguage::TypeScript, SyntaxLanguage::Tsx,        SyntaxLanguage::Json};
constexpr std::array names = {"Css", "Scss", "Less", "Hss", "JavaScript", "Jsx", "TypeScript", "Tsx", "Json"};
std::string Unhex(const std::string& value) {
    if (value == "-") return {};
    std::string result;
    for (size_t index = 0; index < value.size(); index += 2)
        result += static_cast<char>(std::stoul(value.substr(index, 2), nullptr, 16));
    return result;
}
void Choose(EditorFrame* frame, SyntaxLanguage language) {
    const auto label = LanguageName(language);
    const int id = TestSyntax::SchemeCommand(*frame->GetMenuBar(), label);
    Require(id != wxNOT_FOUND, "delivered scheme is available from its real menu");
    TestSyntax::WithSchemeOptions(label, [=] { NativeSearch::Command(frame, id); });
}
int Command(EditorFrame* frame, bool pretty) {
    return frame->GetMenuBar()->FindMenuItem("Tools", pretty ? "CSS/JS/JSON Pretty" : "CSS/JS/JSON Compress");
}
void UseTabs(EditorFrame* frame, bool tabs) {
    if (bool(frame->Editor()->Send(GetUseTabs)) != tabs) {
        const int id = frame->GetMenuBar()->FindMenuItem("Settings", "Insert Tabs as Spaces");
        Require(id != wxNOT_FOUND, "actual indentation setting is available");
        NativeSearch::Command(frame, id);
    }
    Require(bool(frame->Editor()->Send(GetUseTabs)) == tabs, "actual setting applies to the formatter's editor");
}
struct State {
    std::string bytes;
    intptr_t anchor, caret;
    bool modified, undo, redo;
    intptr_t selections, main, mode;
};
State ReadState(std::istringstream& row) {
    State state;
    Require(bool(row >> state.bytes >> state.anchor >> state.caret >> state.modified >> state.undo >> state.redo >>
                 state.selections >> state.main >> state.mode),
            "read frozen original formatter state");
    return state;
}
void Check(EditorControl* editor, const State& state) {
    const auto actual = TestEditor::Text(editor);
    if (actual != Unhex(state.bytes))
        std::println(stderr, "Formatter bytes: expected [{}], actual [{}]", Unhex(state.bytes), actual);
    Require(actual == Unhex(state.bytes), "formatter retains original exact bytes");
    TestEditor::RequireSelection(editor, state.anchor, state.caret, "formatter retains original anchor/caret");
    Require(bool(editor->Send(GetModify)) == state.modified && bool(editor->Send(CanUndo)) == state.undo &&
                bool(editor->Send(CanRedo)) == state.redo,
            "original modified/save-point and single Undo/Redo result");
    Require(editor->Send(GetSelections) == state.selections && editor->Send(GetMainSelection) == state.main &&
                editor->Send(GetSelectionMode) == state.mode,
            "original selection shape, count and main selection");
}
void Availability(EditorFrame* frame) {
    auto* bar = frame->GetMenuBar();
    const int parent = bar->FindMenuItem("Tools", "Web Tools");
    Require(parent != wxNOT_FOUND, "original Web Tools submenu is present");
    const auto* web = bar->FindItem(parent)->GetSubMenu();
    Require(web && web->GetMenuItemCount() >= 2 && web->FindItemByPosition(0)->GetId() == Command(frame, false) &&
                web->FindItemByPosition(1)->GetId() == Command(frame, true),
            "original Compress/Pretty hierarchy and order");
    for (const auto& syntax : SyntaxCatalog()) {
        Choose(frame, syntax.language);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        const bool enabled = std::ranges::find(referenceLanguages, syntax.language) != referenceLanguages.end() &&
                             syntax.language != SyntaxLanguage::TypeScript && syntax.language != SyntaxLanguage::Tsx;
        if (bar->IsEnabled(Command(frame, false)) != enabled || bar->IsEnabled(Command(frame, true)) != enabled)
            std::println(stderr, "Availability: scheme={} expected={} Compress={} Pretty={}", syntax.name, enabled,
                         bar->IsEnabled(Command(frame, false)), bar->IsEnabled(Command(frame, true)));
        Require(bar->IsEnabled(Command(frame, false)) == enabled && bar->IsEnabled(Command(frame, true)) == enabled,
                "only original CodePretty-capable schemes enable the commands");
    }
}
void CapturedCommands(EditorFrame* frame) {
    std::ifstream source{std::filesystem::path(CODE_FORMAT_REFERENCE)};
    Require(source.is_open(), "open independent original formatter observations");
    auto* editor = frame->Editor();
    std::string line, current;
    std::getline(source, line);
    unsigned cases = 0;
    while (std::getline(source, line)) {
        std::istringstream row(line);
        std::string language, mode, bytes;
        bool pretty, tabs;
        unsigned eol;
        intptr_t anchor, caret;
        Require(bool(row >> language >> pretty >> eol >> tabs >> mode >> bytes >> anchor >> caret),
                "read original formatter input");
        if (language != current) {
            const auto found = std::ranges::find(names, language);
            Require(found != names.end(), "original reference names a delivered scheme");
            Choose(frame, referenceLanguages[static_cast<size_t>(found - names.begin())]);
            current = language;
        }
        const auto input = Unhex(bytes);
        TestEditor::ResetStream(editor, input.c_str(), anchor, caret);
        editor->Send(SetEOLMode, eol);
        editor->Send(SetTabWidth, 2);
        UseTabs(frame, tabs);
        if (mode == "multiple" || mode == "empty-main") {
            editor->Send(SetMultipleSelection, 1);
            editor->Send(AddSelection, 7, 4);
            editor->Send(SetMainSelection, 0);
        }
        if (mode == "rectangle") {
            editor->Send(SetSelectionMode, 1);
            editor->Send(SetRectangularSelectionAnchor, anchor);
            editor->Send(SetRectangularSelectionCaret, caret);
        }
        editor->Send(SetReadOnly, mode == "readonly");
        // The command must style unseen bytes itself; do not paint this input first.
        editor->Send(ClearDocumentStyle);
        editor->Send(StartStyling, 0);
        const auto after = ReadState(row), undo = ReadState(row), redo = ReadState(row);
        try {
            NativeSearch::Command(frame, Command(frame, pretty));
            Check(editor, after);
            editor->Send(Undo);
            Check(editor, undo);
            editor->Send(Redo);
            Check(editor, redo);
        } catch (...) {
            std::println(stderr, "Original formatter sample {}: scheme={} pretty={} mode={} input={}", cases, language,
                         pretty, mode, bytes);
            throw;
        }
        ++cases;
    }
    Require(source.eof() && cases == 2604, "replay every captured original formatter observation");
    std::println("PASS: {} original byte/selection/history formatting observations", cases);
}
FileEncoding Encoding(const std::string& name) {
    if (name == "utf-16le") return FileEncoding::Utf16Le;
    if (name == "utf-16be") return FileEncoding::Utf16Be;
    if (name == "gbk") return FileEncoding::Gbk;
    if (name == "big5") return FileEncoding::Big5;
    if (name == "windows-1252") return FileEncoding::Windows1252;
    return FileEncoding::Utf8;
}
void CapturedFiles(EditorFrame* frame) {
    auto* editor = frame->Editor();
    editor->Send(SetReadOnly, 0);
    editor->Send(SetSavePoint);
    std::ifstream source{std::filesystem::path(CODE_FORMAT_FILES_REFERENCE)};
    Require(source.is_open(), "open original encoded file observations");
    const auto directory = TestFiles::TemporaryDirectory("code-format");
    std::filesystem::create_directory(directory);
    const auto path = directory / "fixture.json";
    std::string line;
    std::getline(source, line);
    unsigned cases = 0;
    while (std::getline(source, line)) {
        std::istringstream row(line);
        std::string encoding, bytes, after, saved;
        bool pretty, modified, undo, redo;
        unsigned eol;
        intptr_t anchor, caret;
        Require(bool(row >> encoding >> pretty >> eol >> bytes >> after >> saved >> anchor >> caret >> modified >>
                     undo >> redo),
                "read original file input and result");
        const auto input = Unhex(bytes);
        const bool sameLength = encoding == "same-length";
        if (sameLength) {
            TestEditor::ResetStream(editor, input.c_str(), static_cast<intptr_t>(input.size()), 0);
        } else {
            TestFiles::WriteBytes(path, input);
            Require(frame->Open(TestFiles::Filename(path), Encoding(encoding)).status == OpenStatus::Opened,
                    "open encoded original input through the application");
            editor->Send(SetSel, 0, editor->Send(GetLength));
            editor->Send(EmptyUndoBuffer);
            editor->Send(SetSavePoint);
        }
        Choose(frame, SyntaxLanguage::Json);
        editor->Send(SetEOLMode, eol);
        editor->Send(SetTabWidth, 2);
        UseTabs(frame, false);
        const auto identity = frame->DocumentEncoding();
        const bool bom = frame->DocumentHasBom();
        NativeSearch::Command(frame, Command(frame, pretty));
        Check(editor, {after, anchor, caret, modified, undo, redo, 1, 0, 0});
        Require(frame->DocumentEncoding() == identity && frame->DocumentHasBom() == bom,
                "formatting preserves encoding and BOM identity");
        if (!sameLength) {
            Require(frame->Save().status == SaveStatus::Saved, "save through actual document transaction");
            Require(TestFiles::Bytes(path) == Unhex(saved), "saved bytes equal the independent original file");
            Require(!editor->Send(GetModify), "saving establishes the real save point");
            Require(frame->Open(TestFiles::Filename(path), identity).status == OpenStatus::Opened,
                    "reopen the actual saved file");
            Require(TestEditor::Text(editor) == Unhex(after) && frame->DocumentEncoding() == identity &&
                        frame->DocumentHasBom() == bom && !editor->Send(GetModify),
                    "saved document reopens unchanged");
        }
        ++cases;
    }
    Require(source.eof() && cases == 46, "replay every original encoded-file and equal-length observation");
    std::println("PASS: {} original saved-file/encoding/BOM and equal-length observations", cases);
}
class CodeFormatApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor presents the owned application");
                auto* bar = frame->GetMenuBar();
                NativeSearch::Command(frame, TestSyntax::SchemeCommand(*bar, LanguageName(SyntaxLanguage::Json)));
                const int command = bar->FindMenuItem("Tools", "CSS/JS/JSON Compress");
                Require(command != wxNOT_FOUND, "Tools/Web Tools exposes original CSS/JS/JSON Compress");
                TestEditor::ResetStream(frame->Editor(), "{ \"a\": 1 }");
                NativeSearch::Command(frame, command);
                Require(TestEditor::Text(frame) == "{\"a\":1}", "original JSON compression bytes");
                const int pretty = bar->FindMenuItem("Tools", "CSS/JS/JSON Pretty");
                Require(pretty != wxNOT_FOUND, "Tools/Web Tools exposes original CSS/JS/JSON Pretty");
                frame->Editor()->Send(Scintilla::Message::SetEOLMode, 2);
                frame->Editor()->Send(Scintilla::Message::SetUseTabs, 0);
                frame->Editor()->Send(Scintilla::Message::SetTabWidth, 2);
                NativeSearch::Command(frame, pretty);
                Require(TestEditor::Text(frame) == "{\n  \"a\": 1\n}\n", "original JSON pretty bytes");
                Availability(frame);
                CapturedFiles(frame);
                CapturedCommands(frame);
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(CodeFormatApp);
