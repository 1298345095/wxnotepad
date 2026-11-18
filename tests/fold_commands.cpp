#include "file_helpers.h"
#include "native_key_helpers.h"
#include "native_fold_helpers.h"
#include <charconv>
#include <fstream>
#include <sstream>

namespace {
using enum Scintilla::Message;

void Require(bool condition, const std::string& diagnostic) {
    if (!condition) throw std::runtime_error(diagnostic);
}

std::string DecodeHex(std::string_view hex) {
    Require(hex.size() % 2 == 0, "reference bytes are complete");
    std::string output;
    for (size_t offset = 0; offset < hex.size(); offset += 2) {
        unsigned byte = 0;
        const auto* end = hex.data() + offset + 2;
        const auto result = std::from_chars(hex.data() + offset, end, byte, 16);
        Require(result.ptr == end && result.ec == std::errc{}, "reference byte is hexadecimal");
        output.push_back(static_cast<char>(byte));
    }
    return output;
}

struct FoldState {
    std::string expanded, visible;
    intptr_t anchor = 0, caret = 0, reversed = 0, first = 0, xOffset = 0, modified = 0, undo = 0, redo = 0;
    std::string text;
};

void CompareState(EditorControl& editor, const FoldState& expected, const std::string& label) {
    std::string expanded, visible;
    for (intptr_t line = 0; line < editor.Send(GetLineCount); ++line) {
        expanded += editor.Send(GetFoldExpanded, line) ? '1' : '0';
        visible += editor.Send(GetLineVisible, line) ? '1' : '0';
    }
    const auto anchor = editor.Send(GetAnchor);
    const auto caret = editor.Send(GetCurrentPos);
    Require(expanded == expected.expanded, label + " expanded: " + expanded + " != " + expected.expanded);
    Require(visible == expected.visible, label + " visible: " + visible + " != " + expected.visible);
    Require(anchor == expected.anchor && caret == expected.caret && (anchor > caret) == expected.reversed,
            label + " directed selection: " + std::to_string(anchor) + "/" + std::to_string(caret) +
                " != " + std::to_string(expected.anchor) + "/" + std::to_string(expected.caret));
    const auto first = editor.Send(GetFirstVisibleLine);
    const auto xOffset = editor.Send(GetXOffset);
    Require(first == expected.first && xOffset == expected.xOffset,
            label + " viewport: " + std::to_string(first) + "/" + std::to_string(xOffset) +
                " != " + std::to_string(expected.first) + "/" + std::to_string(expected.xOffset));
    Require(editor.Send(GetModify) == expected.modified && editor.Send(CanUndo) == expected.undo &&
                editor.Send(CanRedo) == expected.redo && TestEditor::Text(&editor) == expected.text,
            label + " document bytes, save point and Undo/Redo");
}

void RunCommand(EditorFrame* frame, std::string_view action, bool shortcuts) {
    wxString label;
    int key = 0;
    bool shift = false;
    if (action == "all") {
        label = "All Levels";
        key = 'A';
        shift = true;
    } else if (action == "default") {
        label = "Default Levels";
        key = 'D';
        shift = true;
    } else if (action == "block") {
        label = "Current Block";
        key = 'C';
    } else if (action == "current") {
        label = "Current Level";
    } else if (action == "hide" || action == "show") {
        label = "Show Code Folding";
    } else {
        Require(action.starts_with("level"), "reference fold command exists");
        const int level = std::stoi(std::string(action.substr(5)));
        Require(level >= 1 && level <= 10, "reference fold command is Level 1-10");
        label = wxString::Format("Level %d", level);
        if (level < 10) key = '0' + level;
    }
    const auto id = frame->GetMenuBar()->FindMenuItem("View", label);
    Require(id != wxNOT_FOUND, "native fold menu item exists: " + label.ToStdString());
    Require(frame->GetMenuBar()->IsEnabled(id), "native fold menu item stays enabled: " + label.ToStdString());
    if (shortcuts && key)
        NativeKeys::Shortcut(frame, key, false, true, shift);
    else
        NativeSearch::Command(frame, id);
}

void SeedHistory(EditorControl& editor) {
    for (const std::string_view addition : {"first", "second"}) {
        editor.Send(BeginUndoAction);
        editor.Send(AppendText, addition.size(), reinterpret_cast<intptr_t>(addition.data()));
        editor.Send(EndUndoAction);
    }
    editor.Send(Undo);
    Require(editor.Send(GetModify) && editor.Send(CanUndo) && editor.Send(CanRedo),
            "native fixture starts with modified text and pending Undo and Redo");
}

void OpenFixture(EditorFrame* frame, const std::string& name, const std::string& extension, const std::string& source,
                 int rows, int lexer) {
    auto* editor = frame->Editor();
    editor->Send(SetSavePoint);
    const auto file = TestFiles::TemporaryDirectory(name + "." + extension);
    TestFiles::WriteBytes(file, source);
    Require(frame->Open(TestFiles::Filename(file), FileEncoding::Utf8).status == OpenStatus::Opened,
            "open pinned original folding fixture " + name);
    editor->Send(Colourise, 0, -1);
    Require(editor->Send(GetLexer) == lexer, name + " uses the original lexer");
    if (name == "history") SeedHistory(*editor);
    NativeFold::Paint(editor);
    editor->SetClientSize(editor->GetClientSize().x, rows * static_cast<int>(editor->Send(TextHeight, 0)));
    NativeFold::Paint(editor);
    Require(editor->Send(LinesOnScreen) == rows, name + " matches the original viewport row count");
    if (name == "childless") {
        const auto level = Scintilla::FoldLevel::Base;
        const auto header = Scintilla::FoldLevel::HeaderFlag;
        editor->Send(SetFoldLevel, 0, std::to_underlying(level) | std::to_underlying(header));
        Require(editor->Send(GetLastChild, 0, -1) == 0, "first header has no effective child");
    }
}

void Replay(EditorFrame* frame, bool shortcuts) {
    std::ifstream input{std::filesystem::path(FOLD_COMMAND_REFERENCE)};
    Require(input.is_open(), "open independent original fold command reference");
    auto* editor = frame->Editor();
    std::string line, name, source;
    int fixtures = 0, transitions = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line.starts_with('#')) continue;
        std::istringstream fields(line);
        std::string action;
        fields >> action;
        if (action == "fixture") {
            std::string extension, hex;
            int rows = 0, lexer = 0;
            Require(static_cast<bool>(fields >> name >> extension >> hex >> rows >> lexer), "read fixture inputs");
            source = DecodeHex(hex);
            OpenFixture(frame, name, extension, source, rows, lexer);
            ++fixtures;
            continue;
        }
        intptr_t caretLine = -1;
        FoldState expected;
        std::string text;
        Require(static_cast<bool>(fields >> caretLine >> expected.expanded >> expected.visible >> expected.anchor >>
                                  expected.caret >> expected.reversed >> expected.first >> expected.xOffset >>
                                  expected.modified >> expected.undo >> expected.redo >> text),
                "read every captured viewport, document and fold state");
        expected.text = text == "-" ? source : DecodeHex(text);
        if (caretLine >= 0) {
            auto start = editor->Send(PositionFromLine, caretLine);
            if (name == "legacy-hidden" || name == "history") start += 4;
            editor->Send(SetSel, std::min(start + 2, editor->Send(GetLength)), start);
        }
        if (name == "viewport" && action == "all" && caretLine >= 0) {
            editor->Send(SetFirstVisibleLine, 5);
            editor->Send(SetXOffset, 41);
        }
        if (!shortcuts && name == "markdown" && action == "default" && caretLine >= 0) {
            editor->Send(ClearDocumentStyle);
            editor->Send(StartStyling, 0);
            Require(editor->Send(GetEndStyled) == 0, "default levels starts with unstyled Markdown");
        }
        RunCommand(frame, action, shortcuts);
        CompareState(*editor, expected, name + " " + action + " #" + std::to_string(++transitions));
    }
    Require(fixtures == 7 && transitions == 78, "complete captured fold fixtures were replayed");
    std::printf("PASS: %d original fold transitions through native %s\n", transitions,
                shortcuts ? "shortcuts" : "menus");
}

class FoldCommandsApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "the private compositor presents the editor before native shortcuts");
                frame->Editor()->SetWordWrapMode(Scintilla::Wrap::None);
                Replay(frame, false);
                Replay(frame, true);
                result_ = 0;
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
wxIMPLEMENT_APP(FoldCommandsApp);
