#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "syntax_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "file_command_driver.h"
#include "tab_settings_helpers.h"
#include "native_paint_helpers.h"
#include "native_search_helpers.h"
#include <SciLexer.h>
#include <wx/wx.h>
#include <wx/statusbr.h>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <memory>
#include <string_view>
#include <wx/textctrl.h>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}
void Choose(EditorFrame* frame, const char* name) {
    const int id = frame->GetMenuBar()->FindMenuItem("Scheme", name);
    Require(id != wxNOT_FOUND, "native Scheme menu must offer the delivered language");
    TestSyntax::Command(frame, id);
    Require(frame->GetMenuBar()->IsChecked(id), "selected Scheme menu item must be checked");
}
struct Dialect {
    const char* name;
    SyntaxLanguage language;
    const char* property;
};
constexpr Dialect dialects[] = {
    {"GitHub GFM", SyntaxLanguage::MarkdownGitHub, "0"},
    {"GitLab GLFM", SyntaxLanguage::MarkdownGitLab, "1"},
    {"Pandoc", SyntaxLanguage::MarkdownPandoc, "2"},
};
void Workflows(EditorFrame* frame, SettingsStore& settings) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetSavePoint);
    constexpr auto defaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
    const auto fontSize = editor->Send(Message::StyleGetSizeFractional, defaultStyle);
    const char* text = "# First\nbody\n## Second\nneedle\n";
    for (const auto& dialect : dialects) {
        const auto path = TestFiles::TemporaryDirectory("workflow.md");
        TestFiles::WriteBytes(path, text);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "Markdown workflow must Open");
        Choose(frame, dialect.name);
        char property[8]{};
        editor->Send(Message::GetProperty, reinterpret_cast<uintptr_t>("lexer.lang"),
                     reinterpret_cast<intptr_t>(property));
        Require(editor->Send(Message::GetLexer) == SCLEX_MARKDOWN && std::string(property) == dialect.property,
                "native Markdown choice must install the original lexer and dialect property");
        Require(editor->Send(Message::GetTabWidth) == 7 && editor->Send(Message::GetIndent) == 3 &&
                    !editor->Send(Message::GetUseTabs),
                "Markdown dialects inherit the selected global indentation defaults");
        Require(editor->Send(Message::StyleGetStrike, SCE_MARKDOWN_STRIKEOUT) &&
                    editor->Send(Message::StyleGetUnderline, SCE_MARKDOWN_SUBSCRIPT) &&
                    editor->Send(Message::StyleGetEOLFilled, SCE_MARKDOWN_BLOCKQUOTE) &&
                    editor->Send(Message::StyleGetItalic, SCE_MARKDOWN_EM_ASTERISK) &&
                    editor->Send(Message::StyleGetBold, SCE_MARKDOWN_HEADER1) &&
                    editor->Send(Message::StyleGetFore, SCE_MARKDOWN_PLAIN_LINK) == 0x008064 &&
                    editor->Send(Message::StyleGetFore, SCE_H_TAG) == 0x008064,
                "Markdown installs original line flags, emphasis, headings, links and embedded HTML attributes");
        for (int style = 0; style <= SCE_MARKDOWN_HARD_LINE_BREAK; ++style)
            Require(editor->Send(Message::StyleGetSizeFractional, style) == fontSize,
                    "all original Markdown styles inherit the native font point size");
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                    frame->GetStatusBar()->GetStatusText().Contains(wxString(dialect.name) + " (override)"),
                "same-path reopen retains the explicit Markdown dialect");
        Choose(frame, "Plain Text");
        Require(editor->Send(Message::GetLexer) == SCLEX_NULL &&
                    !editor->Send(Message::StyleGetStrike, SCE_MARKDOWN_STRIKEOUT) &&
                    !editor->Send(Message::StyleGetUnderline, SCE_MARKDOWN_SUBSCRIPT) &&
                    !editor->Send(Message::StyleGetEOLFilled, SCE_MARKDOWN_BLOCKQUOTE),
                "highlight suspension clears obsolete Markdown line flags and filled backgrounds");
        Choose(frame, dialect.name);
        Require(TestEditor::Text(editor) == text && !editor->Send(Message::CanUndo) && !frame->DocumentIsModified(),
                "dialect changes and highlight resume preserve document bytes and history");
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
        Require(!editor->Send(Message::GetLineVisible, 1), "Markdown heading collapse hides the body");
        Choose(frame, "Plain Text");
        Require(editor->Send(Message::GetLineVisible, 1) &&
                    !(static_cast<unsigned>(editor->Send(Message::GetFoldLevel, 0)) &
                      static_cast<unsigned>(Scintilla::FoldLevel::HeaderFlag)),
                "highlight suspension clears obsolete heading folds and reveals their hidden text");
        Choose(frame, dialect.name);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Expand All"));
        Require(editor->Send(Message::GetLineVisible, 1), "Markdown heading expand restores the body");
        TestEditor::ResetStream(editor, "text\n");
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment"));
        Require(TestEditor::Text(editor) == "text\n" && !editor->Send(Message::CanUndo),
                "Markdown has no original line-comment delimiter");
        editor->Send(Message::SetSel, 0, 4);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment"));
        Require(TestEditor::Text(editor) == "<!--text-->\n", "Markdown block comments retain original HTML delimiters");
        TestSyntax::Command(frame, wxID_UNDO);
        Require(TestEditor::Text(editor) == "text\n", "Markdown comments remain undoable");
        editor->Send(Message::SetSavePoint);
        TabDialog::Show(frame, [](wxDialog* dialog) {
            TabDialog::Toggle(TabDialog::Option(dialog, "Use Global Tab Settings"), false);
            Require(TabDialog::Width(dialog, "Scheme Tab Width")->GetValue() == 4 &&
                        TabDialog::Width(dialog, "Scheme Indentation Width")->GetValue() == 4,
                    "Markdown retains original independent 4/4 defaults");
            TabDialog::Width(dialog, "Scheme Tab Width")->SetValue(5);
            TabDialog::Width(dialog, "Scheme Indentation Width")->SetValue(2);
        });
        for (const auto& other : dialects) {
            Choose(frame, other.name);
            Require(editor->Send(Message::GetTabWidth) == 5 && editor->Send(Message::GetIndent) == 2,
                    "all Markdown dialects share the original Markdown tab settings");
        }
        SettingsStore reloaded;
        const auto saved = reloaded.values.schemeTabs[static_cast<size_t>(SyntaxLanguage::MarkdownGitHub)];
        Require(saved.tabWidth == 5 && saved.indentWidth == 2 && !saved.useGlobal,
                "native Tab Settings persists to the shared original Markdown section");
        settings.values.schemeTabs[static_cast<size_t>(SyntaxLanguage::MarkdownGitHub)] = OriginalTabs{};
        Require(settings.SaveSchemeTabs(dialect.language), "Markdown tab defaults must restore through every dialect");
        const std::string bytes = std::string("\xEF\xBB\xBF") + "# UTF-8 \xE7\x95\x8C\r\n" + text;
        TestFiles::WriteBytes(path, bytes);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "BOM Markdown must Open");
        Choose(frame, dialect.name);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == bytes,
                "ordinary Markdown Save preserves BOM, Unicode and mixed EOL bytes");
        const auto current = TestFiles::TemporaryDirectory("current.txt");
        Require(frame->SaveAs(TestFiles::Filename(current)).status == SaveStatus::Saved &&
                    frame->GetStatusBar()->GetStatusText().Contains(dialect.name) && TestFiles::Bytes(current) == bytes,
                "current-scheme Save As retains explicit dialect and exact saved bytes");
        const auto renamed = TestFiles::TemporaryDirectory("renamed.md");
        Require(frame->SaveAs(TestFiles::Filename(renamed), {}, SaveType::AllFiles).status == SaveStatus::Saved &&
                    frame->GetStatusBar()->GetStatusText().Contains("GitHub GFM") && TestFiles::Bytes(renamed) == bytes,
                "All Files Save As recognizes Markdown with original default GitHub dialect");
        const auto explicitPath = TestFiles::TemporaryDirectory("explicit.txt");
        Require(frame->SaveAs(TestFiles::Filename(explicitPath), {}, dialect.language).status == SaveStatus::Saved &&
                    frame->GetStatusBar()->GetStatusText().Contains(dialect.name) &&
                    TestFiles::Bytes(explicitPath) == bytes,
                "explicit Markdown Save As type overrides conflicting extension and preserves bytes");
        Require(frame->NewDocument(), "clean Markdown allows New");
        Choose(frame, dialect.name);
        TestEditor::Append(frame, text);
        const auto first = TestFiles::TemporaryDirectory("first.md");
        Require(frame->SaveFirst(TestFiles::Filename(first)).status == SaveStatus::Saved &&
                    frame->GetStatusBar()->GetStatusText().Contains(dialect.name) && TestFiles::Bytes(first) == text,
                "first Save retains selected dialect and typed Markdown bytes");
        const auto nativePath = TestFiles::TemporaryDirectory(std::string(dialect.name) + ".md");
        NativeDialogs::FileCommandDriver nativeSave(NativeDialogs::FileDialogProfile::Save, wxID_CANCEL,
                                                    TestFiles::Filename(nativePath));
        TestSyntax::Command(frame, wxID_SAVEAS);
        nativeSave.Stop();
        Require(nativeSave.choosers == 1 && nativeSave.initialType.StartsWith("Markdown (*.") &&
                    nativeSave.initialType.Contains("*.md") && TestFiles::Bytes(nativePath) == text &&
                    frame->GetStatusBar()->GetStatusText().Contains(dialect.name),
                "native Save As uses the shared Markdown extension filter and preserves selected dialect");
    }
    for (const auto* extension : {"md", "MARKDOWN", "mdown", "mkdn", "mkd", "litcoffee", "rmd", "jmd"}) {
        const auto path = TestFiles::TemporaryDirectory(std::string("extra.") + extension);
        TestFiles::WriteBytes(path, "<!DOCTYPE html><html/>\n");
        NativeDialogs::FileCommandDriver open(NativeDialogs::FileDialogProfile::Open, wxID_CANCEL,
                                              TestFiles::Filename(path));
        TestSyntax::Command(frame, wxID_OPEN);
        open.Stop();
        Require(open.choosers == 1 && editor->Send(Message::GetLexer) == SCLEX_MARKDOWN &&
                    frame->GetStatusBar()->GetStatusText().Contains("GitHub GFM"),
                "all original Markdown extensions take precedence over markup header sniffing in native Open");
    }
    std::puts(
        "PASS: Markdown recognition, dialect lifecycle, styles/fonts, folds, comments, shared tabs and saved bytes");
}
void NativeStylesAndFoldSearch(EditorFrame* frame) {
    auto* editor = frame->Editor();
    Choose(frame, "Pandoc");
    const char* sample = "# Native Markdown\nH~2~O x^2^ ~~strike~~ *emphasis* **strong**\n\n> filled "
                         "quote\n\n---\n\n## Nested\nneedle\n";
    TestEditor::ResetStream(editor, sample);
    TestSyntax::Paint(frame);
    NativePaint::Complete(editor);
    TestSyntax::Style(editor, std::string_view(sample).find("^2^"), SCE_MARKDOWN_SUPERSCRIPT);
    const auto position = std::string_view(sample).find("^2^");
    const auto x = static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position));
    const auto y = static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position));
    const auto width = static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position + 3)) - x;
    const wxRect area(x, y, width, static_cast<int>(editor->Send(Message::TextHeight, 1)));
    const auto overlined = NativePaint::Read(editor, area);
    editor->Send(Message::StyleSetOverline, SCE_MARKDOWN_SUPERSCRIPT, false);
    NativePaint::Complete(editor);
    const auto plain = NativePaint::Read(editor, area);
    Require(overlined != plain, "original superscript overline must draw on the actual native editor surface");
    editor->Send(Message::StyleSetOverline, SCE_MARKDOWN_SUPERSCRIPT, true);
    NativePaint::Complete(editor);
    std::printf("Native Markdown dpi=%d scale=%.2f default-point-size=%zd text-height=%zd overline-pixels-observed=1\n",
                editor->GetDPI().y, editor->GetContentScaleFactor(),
                editor->Send(Message::StyleGetSizeFractional, static_cast<uintptr_t>(Scintilla::StylesCommon::Default)),
                editor->Send(Message::TextHeight, 1));
    wxString samplePath;
    if (wxGetEnv("WXNOTEPAD_MARKDOWN_SAMPLE", &samplePath) && !samplePath.empty()) {
        const wxRect sampleArea(0, 0, std::min(editor->GetClientSize().x, 700),
                                std::min(editor->GetClientSize().y, 260));
        const auto pixels = NativePaint::Read(editor, sampleArea);
        wxImage image(sampleArea.width, sampleArea.height);
        for (size_t index = 0; index < pixels.size(); ++index) {
            image.GetData()[index * 3] = pixels[index].Red();
            image.GetData()[index * 3 + 1] = pixels[index].Green();
            image.GetData()[index * 3 + 2] = pixels[index].Blue();
        }
        Require(image.SaveFile(samplePath, wxBITMAP_TYPE_PNG), "native Markdown style sample must save");
    }
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
    Require(!editor->Send(Message::GetLineVisible, 8), "Markdown search target starts folded");
    editor->Send(Message::GotoPos, 0);
    TestSyntax::Command(frame, wxID_FIND);
    auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
    Require(query, "native Find must expose its query");
    query->SetValue("needle");
    NativeSearch::NativeF3(query);
    const auto found = std::string_view(sample).find("needle");
    NativeSearch::Selection(editor, static_cast<intptr_t>(found), static_cast<intptr_t>(found + 6),
                            "native Find selects the hidden Markdown match");
    Require(editor->Send(Message::GetLineVisible, 8), "search unfolds the matched Markdown heading body");
    wxDynamicCast(query->GetParent(), wxDialog)->Hide();
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Search", "Goto Block Start"));
    Require(TestEditor::Text(editor) == sample && !editor->Send(Message::CanUndo) && !frame->DocumentIsModified(),
            "folding, native search, navigation and style rendering preserve bytes, history and save point");
    std::puts("PASS: actual native overline rendering, retained style sample, folding/search and navigation");
}
std::string Unhex(const std::string& hex) {
    std::string bytes;
    for (size_t index = 0; index < hex.size(); index += 2)
        bytes += static_cast<char>(std::stoi(hex.substr(index, 2), nullptr, 16));
    return bytes;
}
void OriginalOracle(EditorFrame* frame) {
    std::ifstream input(std::filesystem::path(MARKDOWN_REFERENCE));
    Require(input.is_open(), "independently captured original style/fold fixture must open");
    std::string line;
    size_t rows = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream row(line);
        std::string name, state, deleted, textHex, styleHex, foldText;
        std::getline(row, name, '\t');
        std::getline(row, state, '\t');
        std::getline(row, deleted, '\t');
        std::getline(row, textHex, '\t');
        std::getline(row, styleHex, '\t');
        std::getline(row, foldText);
        auto* editor = frame->Editor();
        const auto text = Unhex(textHex);
        if (state == "base") {
            editor->Send(Message::SetSavePoint);
            const auto path = TestFiles::TemporaryDirectory("oracle.md");
            TestFiles::WriteBytes(path, text);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                    "original Markdown fixture Open must succeed");
            Choose(frame, name.c_str());
        } else if (state == "edit") {
            editor->Send(Message::GotoPos, std::stoul(deleted));
            wxKeyEvent event(wxEVT_KEY_DOWN);
            event.m_keyCode = WXK_DELETE;
            event.SetEventObject(editor);
            editor->GetEventHandler()->ProcessEvent(event);
        } else {
            TestSyntax::Command(frame, state == "undo" ? wxID_UNDO : wxID_REDO);
        }
        TestSyntax::Paint(frame);
        // Scroll through the actual viewport to complete styling below the
        // initial visible page; do not replace incremental styling with Colourise.
        const auto firstVisible = editor->Send(Message::GetFirstVisibleLine);
        editor->Send(Message::SetFirstVisibleLine, editor->Send(Message::GetLineCount));
        TestSyntax::Paint(frame);
        editor->Send(Message::SetFirstVisibleLine, firstVisible);
        TestSyntax::Paint(frame);
        Require(TestEditor::Text(editor) == text, "incremental edit/Undo/Redo bytes must match the original");
        const auto styles = Unhex(styleHex);
        Require(styles.size() == text.size(), "original styles must cover every UTF-8 byte");
        for (size_t position = 0; position < styles.size(); ++position) {
            if (editor->Send(Message::GetStyleIndexAt, position) != static_cast<unsigned char>(styles[position])) {
                std::fprintf(stderr, "fixture=%s state=%s position=%zu expected=%u actual=%zd\n", name.c_str(),
                             state.c_str(), position, static_cast<unsigned char>(styles[position]),
                             editor->Send(Message::GetStyleIndexAt, position));
                throw std::runtime_error("Markdown styles must match independent original observations");
            }
        }
        std::istringstream folds(foldText);
        size_t foldLine = 0;
        while (std::getline(folds, line, ',')) {
            if (editor->Send(Message::GetFoldLevel, foldLine) != std::stoi(line)) {
                std::fprintf(stderr, "fixture=%s state=%s line=%zu expected=%d actual=%zd\n", name.c_str(),
                             state.c_str(), foldLine, std::stoi(line), editor->Send(Message::GetFoldLevel, foldLine));
                throw std::runtime_error("Markdown folds must match independent original observations");
            }
            ++foldLine;
        }
        ++rows;
    }
    Require(rows == 48, "all three original dialects and four editing states must run");
    std::puts(
        "PASS: 48 independent original Markdown dialect style/fold states including incremental edit and Undo/Redo");
}
class MarkdownApp final : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        settings_->values.warnLineEndings = false;
        settings_->values.detectIndentation = false;
        settings_->values.globalTabs = {7, 3, true};
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                OriginalOracle(frame);
                Workflows(frame, *settings_);
                NativeStylesAndFoldSearch(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s", error.what());
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
wxIMPLEMENT_APP(MarkdownApp);
