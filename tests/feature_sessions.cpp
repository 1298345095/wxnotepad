#include "document/document_session.h"
#include "editor/editor_control.h"
#include "search/search_session.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <optional>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::filesystem::path NativePath(const wxString& path) {
#ifdef __WXMSW__
    return path.ToStdWstring();
#else
    return path.ToStdString(wxConvUTF8);
#endif
}
struct Files {
    Files() = default;
    Files(const Files&) = delete;
    Files& operator=(const Files&) = delete;
    wxString base = wxFileName::CreateTempFileName("wxnotepad-sessions-");
    wxString cpp = base + ".cpp";
    wxString json = base + ".json";
    ~Files() {
        for (const auto* path : {&base, &cpp, &json}) {
            if (wxFileExists(*path)) wxRemoveFile(*path);
        }
    }
};
std::string Text(EditorControl& editor) {
    std::string text(static_cast<size_t>(editor.Send(Message::GetLength)) + 1, '\0');
    editor.Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
void Load(EditorControl& editor, const char* text) {
    using enum Scintilla::Message;
    editor.Send(SetText, 0, reinterpret_cast<intptr_t>(text));
    editor.Send(EmptyUndoBuffer);
    editor.Send(SetSavePoint);
    editor.Send(SetSel, 0, 0);
}

struct EncodingState {
    std::string text;
    EncodingChoice encoding;
    intptr_t anchor;
    intptr_t caret;
    intptr_t readOnly;
    intptr_t undoActions;
    intptr_t canUndo;
    intptr_t canRedo;
    intptr_t markers;
    intptr_t textModified;
    bool modified;

    EncodingState(const DocumentSession& document, EditorControl& editor)
        : text(Text(editor)), encoding(document.CurrentEncoding()), anchor(editor.Send(Message::GetAnchor)),
          caret(editor.Send(Message::GetCurrentPos)), readOnly(editor.Send(Message::GetReadOnly)),
          undoActions(editor.Send(Message::GetUndoActions)), canUndo(editor.Send(Message::CanUndo)),
          canRedo(editor.Send(Message::CanRedo)), markers(editor.Send(Message::MarkerGet, 0)),
          textModified(editor.Send(Message::GetModify)), modified(document.IsModified()) {}
    bool operator==(const EncodingState&) const = default;
};

void EncodingWithoutConfirmation(EditorControl& editor, EncodingChoice native) {
    using enum Scintilla::Message;
    DocumentSession document(editor);
    document.New();
    int decisions = 0;
    const auto decline = [&decisions](EncodingChangeWarning) {
        ++decisions;
        return false;
    };
    Require(!document.SelectEncoding(document.CurrentEncoding(), decline) && decisions == 0,
            "same encoding selection returns unchanged without asking for a decision");
    Load(editor, "kept text");
    editor.Send(InsertText, 9, reinterpret_cast<intptr_t>("!"));
    editor.Send(Undo);
    editor.Send(SetSel, 1, 4);
    Require(document.SelectEncoding(EncodingChoice(FileEncoding::Utf16Le, true), decline) && decisions == 0 &&
                document.CurrentEncoding() == EncodingChoice(FileEncoding::Utf16Le, true) && document.IsModified() &&
                Text(editor) == "kept text" && editor.Send(CanRedo) && !editor.Send(GetModify) &&
                editor.Send(GetAnchor) == 1 && editor.Send(GetCurrentPos) == 4,
            "non-ANSI selection changes format without a decision and preserves selection and Redo");

    document.New();
    Require(document.SelectEncoding(native, decline) && decisions == 0 && document.CurrentEncoding() == native &&
                Text(editor).empty() && !document.IsModified() && !editor.Send(GetUndoActions),
            "a fresh empty document adopts native ANSI without confirmation or modification");

    document.New();
    Load(editor, "noninteractive");
    Require(document.SelectEncoding(native) && document.CurrentEncoding() == native && Text(editor) == "noninteractive",
            "omitting the decision retains noninteractive encoding selection");
}

void RejectedEncodingSelection(EditorControl& editor, EncodingChoice native) {
    using enum Scintilla::Message;
    DocumentSession document(editor);
    document.New();
    Load(editor, "kept text");
    editor.Send(InsertText, 9, reinterpret_cast<intptr_t>("!"));
    editor.Send(Undo);
    editor.Send(MarkerAdd, 0, 0);
    editor.Send(SetSel, 1, 4);
    editor.Send(SetReadOnly, 1);
    const EncodingState before(document, editor);
    int decisions = 0;
    const bool changed = document.SelectEncoding(native, [&decisions](EncodingChangeWarning warning) {
        ++decisions;
        Require(warning == EncodingChangeWarning::AnsiConversion, "nonempty ANSI selection requests its text warning");
        return false;
    });
    Require(!changed && decisions == 1 && EncodingState(document, editor) == before,
            "rejected ANSI selection retains text, encoding, selection, protection, markers and history");
    editor.Send(SetReadOnly, 0);
    editor.Send(Redo);
    Require(Text(editor) == "kept text!", "the retained Redo still restores the pending edit after rejection");
}

void EmptyEncodingHistory(EditorControl& editor, EncodingChoice native) {
    using enum Scintilla::Message;
    DocumentSession document(editor);
    document.New();
    Require(document.SelectEncoding(native), "the empty history fixture starts in native ANSI");
    editor.Send(InsertText, 0, reinterpret_cast<intptr_t>("x"));
    editor.Send(Undo);
    Require(Text(editor).empty() && !editor.Send(CanUndo) && editor.Send(CanRedo) && editor.Send(GetUndoActions),
            "the empty encoding fixture has only pending Redo history");
    const EncodingState before(document, editor);
    int decisions = 0;
    const bool changed =
        document.SelectEncoding(EncodingChoice(FileEncoding::Utf8), [&decisions](EncodingChangeWarning warning) {
            ++decisions;
            Require(warning == EncodingChangeWarning::EmptyAnsiHistory,
                    "an empty buffer with Redo requests its distinct warning");
            return false;
        });
    Require(!changed && decisions == 1 && EncodingState(document, editor) == before,
            "rejecting empty ANSI history conversion retains the format and Redo");
    editor.Send(Redo);
    Require(Text(editor) == "x", "the empty buffer can still Redo its original insertion");
}

void EncodingTextAfterDecision(EditorControl& editor, EncodingChoice native) {
    using enum Scintilla::Message;
    DocumentSession document(editor);
    document.New();
    Load(editor, "old");
    int decisions = 0;
    const bool replaced = document.SelectEncoding(native, [&decisions, &editor](EncodingChangeWarning warning) {
        ++decisions;
        Require(warning == EncodingChangeWarning::AnsiConversion, "the original nonempty text requires confirmation");
        Load(editor, "replacement with a different length\r\n");
        return true;
    });
    Require(replaced && decisions == 1 && document.CurrentEncoding() == native &&
                Text(editor) == "replacement with a different length\r\n" && document.IsModified(),
            "accepted conversion reads the replacement text and its current length after the decision");

    document.New();
    Load(editor, "text cleared during confirmation");
    decisions = 0;
    const bool cleared = document.SelectEncoding(native, [&decisions, &editor](EncodingChangeWarning) {
        ++decisions;
        Load(editor, "");
        return true;
    });
    Require(cleared && decisions == 1 && document.CurrentEncoding() == native && Text(editor).empty() &&
                !document.IsModified() && !editor.Send(GetUndoActions),
            "accepted conversion adopts a newly empty Untitled document without stale nonempty modification");
}

void EncodingIdentityAfterDecision(EditorControl& editor, EncodingChoice native) {
    using enum Scintilla::Message;
    DocumentSession document(editor);
    document.New();
    Load(editor, "changed while deciding");
    int decisions = 0;
    std::optional<EncodingState> afterDecision;
    const bool changed = document.SelectEncoding(
        native, [&decisions, &document, &editor, &afterDecision, &native](EncodingChangeWarning) {
            ++decisions;
            Require(document.SelectEncoding(native), "the decision can select the requested encoding noninteractively");
            editor.Send(InsertText, editor.Send(GetLength), reinterpret_cast<intptr_t>("!"));
            editor.Send(Undo);
            editor.Send(MarkerAdd, 0, 0);
            editor.Send(SetSel, 1, 4);
            editor.Send(SetReadOnly, 1);
            afterDecision.emplace(document, editor);
            return true;
        });
    Require(!changed && decisions == 1 && afterDecision && EncodingState(document, editor) == *afterDecision,
            "an accepted decision that already selected the target preserves its latest state and returns unchanged");
    editor.Send(SetReadOnly, 0);
    editor.Send(Redo);
    Require(Text(editor) == "changed while deciding!", "the outer selection preserves history created by the decision");

    document.New();
    Require(document.SelectEncoding(native), "the crossing fixture starts in native ANSI");
    Load(editor, "latest crossing");
    decisions = 0;
    const bool recrossed = document.SelectEncoding(
        EncodingChoice(FileEncoding::Utf16Le), [&decisions, &document, &editor](EncodingChangeWarning) {
            ++decisions;
            Require(document.SelectEncoding(EncodingChoice(FileEncoding::Utf8)),
                    "the decision can move to a different non-ANSI encoding");
            editor.Send(InsertText, editor.Send(GetLength), reinterpret_cast<intptr_t>("!"));
            editor.Send(Undo);
            editor.Send(MarkerAdd, 0, 0);
            editor.Send(SetSel, 1, 4);
            editor.Send(SetReadOnly, 1);
            return true;
        });
    Require(recrossed && decisions == 1 && document.CurrentEncoding() == EncodingChoice(FileEncoding::Utf16Le) &&
                Text(editor) == "latest crossing" && editor.Send(GetUndoActions) && editor.Send(GetReadOnly) &&
                editor.Send(MarkerGet, 0) && editor.Send(GetAnchor) == 1 && editor.Send(GetCurrentPos) == 4,
            "the final selection recomputes ANSI crossing and preserves the new non-ANSI history and protection");
    editor.Send(SetReadOnly, 0);
    Require(editor.Send(CanRedo), "the retained Redo is available after lifting read-only protection");
    editor.Send(Redo);
    Require(Text(editor) == "latest crossing!", "the decision's non-ANSI edit remains replayable after selection");
}

void RejectedEncodingDecisionEdits(EditorControl& editor, EncodingChoice native) {
    using enum Scintilla::Message;
    DocumentSession document(editor);
    document.New();
    Load(editor, "old");
    const auto originalEncoding = document.CurrentEncoding();
    int decisions = 0;
    const bool changed = document.SelectEncoding(native, [&decisions, &editor](EncodingChangeWarning) {
        ++decisions;
        editor.Send(InsertText, 3, reinterpret_cast<intptr_t>(" changed"));
        return false;
    });
    Require(!changed && decisions == 1 && document.CurrentEncoding() == originalEncoding &&
                Text(editor) == "old changed" && document.IsModified() && editor.Send(CanUndo),
            "a rejected encoding decision retains edits performed during confirmation");
    editor.Send(Undo);
    Require(Text(editor) == "old" && !document.IsModified(), "the decision's edit retains its original Undo step");
}

void EncodingSelection(EditorControl& editor) {
    const auto native = NativeEncodingChoice();
    if (!native) {
        std::puts("SKIP: native ANSI encoding selection requires a delivered host codec");
        return;
    }
    EncodingWithoutConfirmation(editor, *native);
    RejectedEncodingSelection(editor, *native);
    EmptyEncodingHistory(editor, *native);
    EncodingTextAfterDecision(editor, *native);
    EncodingIdentityAfterDecision(editor, *native);
    RejectedEncodingDecisionEdits(editor, *native);
    std::puts("PASS: session encoding confirmation, retained history and live state after decisions");
}

class SessionApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad feature sessions");
        auto* editor = new EditorControl(frame);
        frame->Show();
        CallAfter([this, frame, editor] {
            try {
                SearchSession search(*editor);
                Require(search.Find(SearchDirection::Forward).status == FindStatus::EmptyQuery,
                        "search must operate without any dialog");
                Load(*editor, "one one");
                editor->Send(Message::InsertText, 7, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 0, 0);
                search.Configure({"one", "one", {}, false});
                const auto match = search.Find(SearchDirection::Forward);
                Require(match.status == FindStatus::Found && match.start == 0 && match.end == 3 && !match.wrapped,
                        "Find returns the selected byte range");
                const auto one = search.ReplaceOne();
                Require(!one.changed && one.next && one.next->start == 4 && one.next->status == FindStatus::Found &&
                            editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                        "no-op replacement reports no edit, advances and preserves redo/save point");
                const auto all = search.ReplaceAll();
                Require(all.status == ReplaceAllStatus::Completed && all.count == 2 && !all.changed &&
                            editor->Send(Message::CanRedo) && Text(*editor) == "one one",
                        "Replace All returns the count without introducing no-op history");

                Load(*editor, "中文");
                search.Configure({wxString::FromUTF8("(?=文)"), "X", {false, false, true}, false});
                Require(search.Find(SearchDirection::Forward).start == 3, "regex positions are UTF-8 byte offsets");
                const auto repeated = search.Find(SearchDirection::Forward);
                Require(repeated.status == FindStatus::Found && repeated.start == 3 && repeated.end == 3,
                        "pinned Find resumes at the selection end, including the same zero-width match");
                auto request = search.Request();
                request.query = "other";
                search.Configure(request);
                request.query = wxString::FromUTF8("(?=文)");
                search.Configure(request);
                const auto reset = search.Find(SearchDirection::Forward);
                Require(reset.status == FindStatus::Found && reset.start == 3,
                        "restoring the query resumes at the current empty selection");
                Load(*editor, "aab");
                search.Configure({"aa|(?<=aa)b", "", {false, false, true}, false});
                Require(search.Find(SearchDirection::Forward).end == 2, "original alternation first selects aa");
                Require(search.Find(SearchDirection::Forward).status == FindStatus::NotFound,
                        "original forward resumption does not retain lookbehind before target start");
                Load(*editor, "a\nb a\nb");
                editor->Send(Message::GotoPos, 7);
                search.Configure({"a\\nb", "", {false, false, true}, false});
                Require(search.Find(SearchDirection::Backward).status == FindStatus::NotFound,
                        "original backward regex searches separate lines without cross-line matches");
                Load(*editor, "");
                search.Configure({"^", "X", {false, false, true}, false});
                Require(search.ReplaceOne().changed && Text(*editor) == "X",
                        "original Replace accepts matching empty selection without separate confirmation");
                Load(*editor, "a a");
                editor->Send(Message::SetSel, 0, 3);
                search.Configure({"(a)", "X", {false, false, true}, false});
                const auto selectedMatch = search.ReplaceOne();
                Require(!selectedMatch.changed && selectedMatch.next &&
                            selectedMatch.next->status == FindStatus::Found && selectedMatch.next->start == 0 &&
                            selectedMatch.next->end == 1 && Text(*editor) == "a a",
                        "original Replace selects the first revalidation match inside a larger selection");
                Load(*editor, "a");
                search.Configure({"a*", "_", {false, false, true}, false});
                const auto consumingAll = search.ReplaceAll();
                Require(consumingAll.count == 1 && Text(*editor) == "_",
                        "original Replace All stops at document end after a consuming replacement");
                Load(*editor, "a\nb\n");
                search.Configure({"^", "", {false, false, true}, false});
                Require(
                    search.ReplaceAll().count == 2 && Text(*editor) == "a\nb\n" && !editor->Send(Message::CanUndo),
                    "original pattern-leading caret skips the final empty line when replacement ends at line start");
                Load(*editor, "中文");
                request.query = "(";
                search.Configure(request);
                Require(search.Find(SearchDirection::Forward).status == FindStatus::InvalidPattern &&
                            search.ReplaceAll().status == ReplaceAllStatus::InvalidPattern && Text(*editor) == "中文",
                        "invalid patterns return structured errors without changing text");
                std::puts("PASS: independent SearchSession outcomes, progress and history");

                Files files;
                const std::string bytes = "int value;\n";
                {
                    std::ofstream output(NativePath(files.cpp), std::ios::binary);
                    output << bytes;
                    Require(output.good(), "fixture write must succeed");
                }
                DocumentSession document(*editor);
                Require(document.Load(DocumentLoad::Open{files.cpp}).status == OpenStatus::Opened &&
                            document.Language() == SyntaxLanguage::Cpp && !document.LanguageOverride(),
                        "document Open owns automatic classification without a frame");
                document.SelectLanguage(SyntaxLanguage::Python);
                Require(document.SaveAs(files.json, {}, SaveType::AllFiles).status == SaveStatus::Saved &&
                            document.Language() == SyntaxLanguage::Json && !document.LanguageOverride(),
                        "Save As clears the explicit language choice and reidentifies from the filename");
                document.SelectLanguage(std::nullopt);
                Require(document.Language() == SyntaxLanguage::Json, "Automatic follows the current file path");
                Require(document.SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                            document.Language() == SyntaxLanguage::Json && document.Path() == files.json,
                        "cancelled Save As retains document state");
                std::ifstream saved(NativePath(files.json), std::ios::binary);
                const std::string disk{std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>()};
                Require(disk == bytes, "language changes preserve independently read saved bytes");
                saved.close();
                document.New();
                Require(document.Language() == SyntaxLanguage::PlainText && !document.LanguageOverride() &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            Text(*editor).empty() && search.Request().query == "(",
                        "New resets document identity/language while retaining search preferences");
                std::puts("PASS: independent DocumentSession language transitions and saved bytes");
                EncodingSelection(*editor);
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

wxIMPLEMENT_APP(SessionApp);
