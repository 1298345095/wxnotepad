#include "document/document_session.h"
#include "editor/editor_control.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <array>
#include <cstdio>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using Scintilla::Message;

class LoadContractFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void Require(bool condition, const char* message) {
    if (!condition) throw LoadContractFailure(message);
}

std::string Text(EditorControl& editor) {
    std::string text(static_cast<size_t>(editor.Send(Message::GetLength)) + 1, '\0');
    editor.Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}

void Append(EditorControl& editor, std::string_view text) {
    editor.Send(Message::AppendText, text.size(), reinterpret_cast<intptr_t>(text.data()));
}

void OpenFixture(DocumentSession& document, const std::filesystem::path& path, std::string_view text) {
    TestFiles::WriteBytes(path, text);
    Require(document.Load(DocumentLoad::Open{TestFiles::Filename(path), FileEncoding::Utf8}).status ==
                OpenStatus::Opened,
            "the independent file fixture must open");
}

struct State {
    std::string text;
    wxString path;
    EncodingChoice encoding;
    SyntaxLanguage language;
    intptr_t anchor;
    intptr_t caret;
    intptr_t readOnly;
    intptr_t undoActions;
    bool modified;

    State(const DocumentSession& document, EditorControl& editor)
        : text(Text(editor)), path(document.Path()), encoding(document.CurrentEncoding()),
          language(document.Language()), anchor(editor.Send(Message::GetAnchor)),
          caret(editor.Send(Message::GetCurrentPos)), readOnly(editor.Send(Message::GetReadOnly)),
          undoActions(editor.Send(Message::GetUndoActions)), modified(document.IsModified()) {}
    bool operator==(const State&) const = default;
};

void PathlessIntents(EditorControl& editor) {
    DocumentSession document(editor);
    document.New();
    Append(editor, "edited unnamed");
    document.SelectLanguage(SyntaxLanguage::Python);
    editor.Send(Message::SetSel, 1, 6);
    editor.Send(Message::SetReadOnly, 1);
    const State before(document, editor);
    int decisions = 0;
    int configurations = 0;
    const auto decide = [&decisions] {
        ++decisions;
        return true;
    };
    const std::array<DocumentLoad::Intent, 4> intents = {
        DocumentLoad::Open{std::nullopt, {}, decide}, DocumentLoad::Reload{},
        DocumentLoad::ReloadWithEncoding{FileEncoding::Utf8}, DocumentLoad::ReloadAsUtf8{decide}};
    for (const auto& intent : intents) {
        const auto result = document.Load(intent, [&configurations] { ++configurations; });
        Require(result.status == OpenStatus::Cancelled && result.error.empty() && decisions == 0 &&
                    configurations == 0 && State(document, editor) == before,
                "pathless load intents cancel without callbacks or changes to the edited unnamed document");
    }
    std::puts("PASS: document-load pathless intents cancel without callbacks or live-state changes");
}

void SourceIdentity(EditorControl& editor) {
    const auto native = NativeOemEncodingChoice();
    Require(native.has_value(), "the host-native OEM source must have a delivered codec");
    DocumentSession document(editor);
    const auto source = TestFiles::TemporaryDirectory("load-source-identity.txt");
    const auto path = TestFiles::Filename(source);
    const std::string bytes = "native OEM\r\n";
    TestFiles::WriteBytes(source, bytes);
    Require(document.Load(DocumentLoad::Open{path, native->encoding}).status == OpenStatus::Opened &&
                document.CurrentEncoding() == EncodingChoice(native->encoding) && !document.IsOemEncoding() &&
                Text(editor) == bytes && document.Save().status == SaveStatus::Saved &&
                TestFiles::Bytes(source) == bytes,
            "an explicit source codec loads and saves the file without adopting native OEM identity");
    Require(document.Load(DocumentLoad::Open{path, *native}).status == OpenStatus::Opened &&
                document.CurrentEncoding() == *native && document.IsOemEncoding() && !document.HasBom() &&
                Text(editor) == bytes && document.Save().status == SaveStatus::Saved &&
                TestFiles::Bytes(source) == bytes,
            "the full native OEM source retains its identity and exact saved bytes");
    const std::string signedUtf8 = "\xef\xbb\xbf" + bytes;
    TestFiles::WriteBytes(source, signedUtf8);
    Require(document.Load(DocumentLoad::Open{path, EncodingChoice(FileEncoding::Utf8)}).status == OpenStatus::Opened &&
                document.CurrentEncoding() == EncodingChoice(FileEncoding::Utf8, true) && Text(editor) == bytes &&
                document.Save().status == SaveStatus::Saved && TestFiles::Bytes(source) == signedUtf8,
            "a full source choice obtains its actual BOM from disk and preserves it on Save");
    std::puts("PASS: document-load source codecs and native OEM identities retain disk-derived BOM and saved bytes");
}

void DecisionOrder(EditorControl& editor) {
    DocumentSession document(editor);
    const auto source = TestFiles::TemporaryDirectory("load-decision.txt");
    OpenFixture(document, source, "live");
    Append(editor, " edit");
    document.SelectLanguage(SyntaxLanguage::Python);
    editor.Send(Message::SetSel, 1, 4);
    const State before(document, editor);
    int decisions = 0;
    int configurations = 0;
    const auto decide = [&decisions] {
        ++decisions;
        return true;
    };
    const auto configure = [&configurations] { ++configurations; };
    const auto missing = TestFiles::TemporaryDirectory("load-missing.txt");
    const auto unreadable = document.Load(DocumentLoad::Open{TestFiles::Filename(missing), {}, decide}, configure);
    Require(unreadable.status == OpenStatus::Failed && !unreadable.error.empty() && decisions == 0 &&
                configurations == 0 && State(document, editor) == before,
            "Open read failure precedes its decision and retains live state");
    const auto invalid = TestFiles::TemporaryDirectory("load-invalid.txt");
    TestFiles::WriteBytes(invalid, "\xff");
    const auto undecodable =
        document.Load(DocumentLoad::Open{TestFiles::Filename(invalid), FileEncoding::Utf8, decide}, configure);
    Require(undecodable.status == OpenStatus::Failed && !undecodable.error.empty() && decisions == 0 &&
                configurations == 0 && State(document, editor) == before,
            "Open decode failure precedes its decision and retains live state");
    Require(std::filesystem::remove(source), "the named reload source must be removed");
    const auto cancelled = document.Load(DocumentLoad::ReloadAsUtf8{[&decisions] {
                                             ++decisions;
                                             return false;
                                         }},
                                         configure);
    Require(cancelled.status == OpenStatus::Cancelled && cancelled.error.empty() && decisions == 1 &&
                configurations == 0 && State(document, editor) == before,
            "UTF-8 reload cancellation precedes a missing-file read and retains live state");
    std::puts("PASS: document-load Open errors precede decisions; UTF-8 cancellation precedes disk reads");
}

void RetainCandidate(EditorControl& editor) {
    DocumentSession document(editor);
    const auto source = TestFiles::TemporaryDirectory("load-candidate.txt");
    OpenFixture(document, source, "live");
    Append(editor, " edit");
    TestFiles::WriteBytes(source, "pre-decision candidate");
    int decisions = 0;
    const auto result = document.Load(
        DocumentLoad::Open{TestFiles::Filename(source), FileEncoding::Utf8, [&document, &editor, &source, &decisions] {
                               ++decisions;
                               TestFiles::WriteBytes(source, "written during decision");
                               Append(editor, " callback");
                               Require(document.IsModified(), "the callback must leave the live document modified");
                               return true;
                           }});
    Require(result.status == OpenStatus::Opened && decisions == 1 && Text(editor) == "pre-decision candidate" &&
                TestFiles::Bytes(source) == "written during decision" && !document.IsModified() &&
                !editor.Send(Message::GetUndoActions),
            "an accepted dirty document uses the candidate decoded before its decision");
    std::puts("PASS: document-load dirty confirmation retains the pre-read candidate and independent disk bytes");
}

void RereadWithLiveOptions(EditorControl& editor) {
    DocumentSession document(editor);
    const auto source = TestFiles::TemporaryDirectory("load-live-options.txt");
    OpenFixture(document, source, "# coding: gbk\nsaved");
    Append(editor, " edit");
    TestFiles::WriteBytes(source, "# coding: gbk\nstale");
    int decisions = 0;
    const auto result = document.Load(DocumentLoad::Open{TestFiles::Filename(source), {}, [&document, &decisions] {
                                                             ++decisions;
                                                             Require(document.Save().status == SaveStatus::Saved,
                                                                     "the decision must save the live UTF-8 text");
                                                             document.SetEncodingOptions({.noEncodingTags = true});
                                                             return true;
                                                         }});
    Require(result.status == OpenStatus::Opened && decisions == 1 && Text(editor) == "# coding: gbk\nsaved edit" &&
                TestFiles::Bytes(source) == Text(editor) &&
                document.CurrentEncoding() == EncodingChoice(FileEncoding::Utf8) && !document.IsModified() &&
                !editor.Send(Message::GetUndoActions),
            "dirty-to-clean Open re-reads saved bytes using the callback's live encoding options");
    std::puts("PASS: document-load Save re-reads disk bytes with live encoding options");
}

void RereadFailureRetainsSaveAs(EditorControl& editor) {
    DocumentSession document(editor);
    const auto source = TestFiles::TemporaryDirectory("load-reread-source.txt");
    const auto destination = TestFiles::TemporaryDirectory("load-saved-as.py");
    OpenFixture(document, source, "live");
    Append(editor, " edit");
    TestFiles::WriteBytes(source, "candidate");
    int decisions = 0;
    int configurations = 0;
    std::optional<State> saved;
    const auto result = document.Load(
        DocumentLoad::Open{
            TestFiles::Filename(source),
            {},
            [&document, &editor, &source, &destination, &decisions, &saved] {
                ++decisions;
                Require(document.SaveAs(TestFiles::Filename(destination), {}, SyntaxLanguage::Python).status ==
                            SaveStatus::Saved,
                        "the decision's Save As must succeed");
                editor.Send(Message::SetSel, 2, 6);
                editor.Send(Message::SetReadOnly, 1);
                saved.emplace(document, editor);
                Require(std::filesystem::remove(source), "the selected source must disappear after Save As");
                return true;
            }},
        [&configurations] { ++configurations; });
    Require(result.status == OpenStatus::Failed && !result.error.empty() && decisions == 1 && configurations == 0 &&
                saved && State(document, editor) == *saved && document.Path() == TestFiles::Filename(destination) &&
                TestFiles::Bytes(destination) == "live edit" && !std::filesystem::exists(source),
            "second-read failure retains the callback's Save As, selection, protection, history and save point");
    std::puts("PASS: document-load second-read failure retains completed Save As and skips loaded configuration");
}

void Utf8CapturesBeforeDecision(EditorControl& editor) {
    DocumentSession document(editor);
    const auto source = TestFiles::TemporaryDirectory("load-utf8-source.cpp");
    const auto destination = TestFiles::TemporaryDirectory("load-utf8-saved.json");
    OpenFixture(document, source, "original live\n");
    Append(editor, "edit");
    document.SelectLanguage(SyntaxLanguage::Python);
    editor.Send(Message::SetSel, 2, 8);
    editor.Send(Message::SetReadOnly, 1);
    int decisions = 0;
    const auto result = document.Load(DocumentLoad::ReloadAsUtf8{[&document, &editor, &source, &destination,
                                                                  &decisions] {
        ++decisions;
        editor.Send(Message::SetReadOnly, 0);
        editor.Send(Message::SetSel, 0, 1);
        Require(document.SaveAs(TestFiles::Filename(destination), {}, SyntaxLanguage::Json).status == SaveStatus::Saved,
                "UTF-8 confirmation must be able to save under another path and scheme");
        TestFiles::WriteBytes(source, "fresh source\n");
        return true;
    }});
    Require(result.status == OpenStatus::Opened && decisions == 1 && document.Path() == TestFiles::Filename(source) &&
                Text(editor) == "fresh source\n" && TestFiles::Bytes(destination) == "original live\nedit" &&
                editor.Send(Message::GetAnchor) == 2 && editor.Send(Message::GetCurrentPos) == 8 &&
                editor.Send(Message::GetReadOnly) && document.LanguageOverride() == SyntaxLanguage::Json &&
                !document.IsModified() && !editor.Send(Message::GetUndoActions),
            "UTF-8 reload freezes path, view and protection before confirmation while retaining the live scheme");
    std::puts(
        "PASS: document-load UTF-8 confirmation freezes path, view and read-only state while language stays live");
}

void ConfigureBeforeRestore(EditorControl& editor) {
    DocumentSession document(editor);
    const auto source = TestFiles::TemporaryDirectory("load-configure.txt");
    OpenFixture(document, source, "# tab-width: 8; indent-tabs-mode: true\nold\n");
    editor.Send(Message::SetSel, 2, 7);
    editor.Send(Message::SetReadOnly, 1);
    const std::string replacement = "# tab-width: 3; indent-tabs-mode: false\nloaded\n";
    TestFiles::WriteBytes(source, replacement);
    int configurations = 0;
    const auto result = document.Load(DocumentLoad::Reload{}, [&document, &editor, &replacement, &configurations] {
        ++configurations;
        Require(document.FileVariables().tabWidth == 3 && document.FileVariables().tabsAsSpaces == true &&
                    Text(editor) == replacement && editor.Send(Message::GetAnchor) == 0 &&
                    editor.Send(Message::GetCurrentPos) == 0 && !editor.Send(Message::GetReadOnly),
                "loaded configuration observes committed file variables and text before view/protection restore");
        editor.Send(Message::SetTabWidth, static_cast<uintptr_t>(*document.FileVariables().tabWidth));
    });
    Require(result.status == OpenStatus::Opened && configurations == 1 && editor.Send(Message::GetTabWidth) == 3 &&
                editor.Send(Message::GetAnchor) == 2 && editor.Send(Message::GetCurrentPos) == 7 &&
                editor.Send(Message::GetReadOnly) && !document.IsModified(),
            "successful reload configures once before restoring the captured view and protection");
    std::puts("PASS: document-load configuration runs once after file variables and before view/read-only restore");
}

class DocumentLoadApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad document load intents");
        auto* editor = new EditorControl(frame);
        frame->Show();
        CallAfter([this, frame, editor] {
            try {
                PathlessIntents(*editor);
                SourceIdentity(*editor);
                DecisionOrder(*editor);
                RetainCandidate(*editor);
                RereadWithLiveOptions(*editor);
                RereadFailureRetainsSaveAs(*editor);
                Utf8CapturesBeforeDecision(*editor);
                ConfigureBeforeRestore(*editor);
                result_ = 0;
            } catch (const std::exception& error) {
                std::cerr << "FAIL: " << error.what() << '\n';
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

wxIMPLEMENT_APP(DocumentLoadApp);
