#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "document/document_session.h"
#include "file_helpers.h"
#include "syntax_test_helpers.h"
#include "tab_settings_helpers.h"
#include <SciLexer.h>
#include <wx/app.h>
#include <wx/menu.h>
#include <wx/statusbr.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Detection(EditorFrame* frame) {
    using enum SyntaxLanguage;
    struct Fixture {
        const char* name;
        const char* text;
        SyntaxLanguage language;
    };
    for (const auto& fixture : {Fixture{"untyped", " <html><body/></html>", Html},
                                {"declaration", "\n<?xml version=\"1.0\"?><Root/>", Xml},
                                {"doctype", "<!DOCTYPE html><html/>", Html},
                                {"schema", "<!DOCTYPE schema><schema/>", Xsd},
                                {"guess", "<Root/>", Xml},
                                {"embedded", "<!-- header --><HTML/>", Html},
                                {"sample.cfg", "<?xml version=\"1.0\"?><Root/>", Xml},
                                {"sample.conf", "<html/>", PlainText},
                                {"sample.txt", "<html/>", PlainText},
                                {"sample.cpp", "<html/>", Cpp},
                                {"page.asp", "<%@ Language=\"JavaScript\" %>", AspJs},
                                {"page.aspx", "<%@ Page Language=\"VB\" %>", AspxVb},
                                {"page.jsp", "<html/>", Jsp},
                                {"page.jspx", "<jsp:root/>", Jspx},
                                {"image.SVG", "<svg/>", Svg}}) {
        const auto path = TestFiles::TemporaryDirectory(fixture.name);
        TestFiles::WriteBytes(path, fixture.text);
        DocumentSession document(*frame->Editor());
        Require(document.Open(TestFiles::Filename(path), [] { return true; }).status == OpenStatus::Opened,
                "prefix fixture Open must succeed");
        if (document.Language() != fixture.language) {
            std::fprintf(stderr, "fixture=%s expected=%s actual=%s\n", fixture.name, SyntaxFor(fixture.language).name,
                         SyntaxFor(document.Language()).name);
            throw std::runtime_error("original filename and prefix precedence must select the effective dialect");
        }
    }
}
std::string Unhex(const std::string& hex) {
    std::string bytes;
    for (size_t index = 0; index < hex.size(); index += 2)
        bytes += static_cast<char>(std::stoi(hex.substr(index, 2), nullptr, 16));
    return bytes;
}
void OriginalOracle(EditorFrame* frame) {
    std::ifstream input(std::filesystem::path(MARKUP_REFERENCE));
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
            const auto path = TestFiles::TemporaryDirectory(name);
            TestFiles::WriteBytes(path, text);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                    "original markup fixture Open must succeed");
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
        Require(TestEditor::Text(editor) == text, "incremental edit/Undo/Redo bytes must match the original");
        const auto styles = Unhex(styleHex);
        Require(styles.size() == text.size(), "original styles must cover every UTF-8 byte");
        for (size_t position = 0; position < styles.size(); ++position) {
            if (editor->Send(Message::GetStyleIndexAt, position) != static_cast<unsigned char>(styles[position])) {
                std::fprintf(stderr, "fixture=%s state=%s position=%zu expected=%u actual=%zd\n", name.c_str(),
                             state.c_str(), position, static_cast<unsigned char>(styles[position]),
                             editor->Send(Message::GetStyleIndexAt, position));
                throw std::runtime_error("markup styles must match independent original observations");
            }
        }
        std::istringstream folds(foldText);
        size_t foldLine = 0;
        while (std::getline(folds, line, ',')) {
            if ((editor->Send(Message::GetFoldLevel, foldLine) & 0xffff) != std::stoi(line)) {
                std::fprintf(stderr, "fixture=%s state=%s line=%zu expected=%d actual=%zd\n", name.c_str(),
                             state.c_str(), foldLine, std::stoi(line), editor->Send(Message::GetFoldLevel, foldLine));
                throw std::runtime_error("markup folds must match independent original observations");
            }
            ++foldLine;
        }
        ++rows;
    }
    Require(rows == 44, "all eleven original fixtures and four editing states must run");
    std::puts("PASS: 44 independent original markup style/fold states including incremental edit and Undo/Redo");
}
void Commands(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* before;
        const char* token;
        const char* after;
    };
    for (const auto& fixture :
         {Fixture{"<p>value</p>", "value", "<p><!--value--></p>"},
          {"<script>const value = 1;</script>", "const", "<script>/*const*/ value = 1;</script>"},
          {"<style>.value {color:red}</style>", "color", "<style>.value {/*color*/:red}</style>"}}) {
        TestEditor::ResetStream(editor, fixture.before, 0, 0);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Web Source Code"));
        editor->Send(Message::Colourise, 0, -1);
        const auto start = std::string(fixture.before).find(fixture.token);
        editor->Send(Message::SetSel, start, start + std::strlen(fixture.token));
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment"));
        Require(TestEditor::Text(editor) == fixture.after,
                "comment delimiters must follow the original embedded region");
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment"));
        Require(TestEditor::Text(editor) == fixture.before, "second toggle must remove the embedded region comment");
        TestSyntax::Command(frame, wxID_UNDO);
        Require(TestEditor::Text(editor) == fixture.after, "Undo restores the full embedded comment operation");
    }
    struct LineFixture {
        const char* scheme;
        const char* before;
        const char* after;
    };
    for (const auto& fixture : {LineFixture{"Web Source Code", "<script>\nconst value = (1);\n</script>\n",
                                            "<script>\n// const value = (1);\n</script>\n"},
                                {"ASP VBScript", "<%\nvalue = 1\n%>\n", "<%\n' value = 1\n%>\n"}}) {
        TestEditor::ResetStream(editor, fixture.before, 0, 0);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", fixture.scheme));
        editor->Send(Message::Colourise, 0, -1);
        editor->Send(Message::GotoPos, std::string(fixture.before).find("value"));
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment"));
        Require(TestEditor::Text(editor) == fixture.after, "line comments follow original embedded JS/VB regions");
        TestSyntax::Command(frame, wxID_UNDO);
        Require(TestEditor::Text(editor) == fixture.before, "embedded line comment is one Undo action");
    }
    const std::string braces = "<script>\nconst value = (1);\n</script>\n";
    TestEditor::ResetStream(editor, braces.c_str(), 0, 0);
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Web Source Code"));
    editor->Send(Message::Colourise, 0, -1);
    editor->Send(Message::GotoPos, braces.find('('));
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Search", "Find Matching Brace"));
    Require(editor->Send(Message::GetCurrentPos) == static_cast<intptr_t>(braces.find(')')) &&
                !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
            "shared brace command uses embedded JavaScript token styles without editing");
}
void NativeTabsAndFoldedFind(EditorFrame* frame) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetSavePoint);
    Require(frame->NewDocument(), "New must reset markup file settings");
    const auto choose = [frame](const char* name) {
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", name));
    };
    choose("Web Source Code");
    TabDialog::Show(frame, [](wxDialog* dialog) {
        TabDialog::Width(dialog, "Scheme Tab Width")->SetValue(6);
        TabDialog::Width(dialog, "Scheme Indentation Width")->SetValue(3);
    });
    choose("ASP JavaScript");
    Require(editor->Send(Message::GetTabWidth) == 6 && editor->Send(Message::GetIndent) == 3,
            "ASP must share changes made in the native HTML Scheme tab controls");
    choose("XML Document");
    Require(editor->Send(Message::GetTabWidth) == 4 && editor->Send(Message::GetUseTabs),
            "XML retains original global 4/4 tabs defaults");
    TabDialog::Show(frame, [](wxDialog* dialog) { TabDialog::Width(dialog, "Global Tab Width")->SetValue(7); });
    choose("SVG");
    Require(editor->Send(Message::GetTabWidth) == 7, "SVG shares XML global inheritance");
    TabDialog::File(frame, 9, 5, true);
    choose("JSPX");
    Require(editor->Send(Message::GetTabWidth) == 9 && editor->Send(Message::GetIndent) == 5,
            "file tab override survives a markup dialect switch");
    choose("Web Source Code");
    const std::string text = "<html>\n<body>\n<p>needle</p>\n</body>\n</html>\n";
    TestEditor::ResetStream(editor, text.c_str(), 0, 0);
    editor->Send(Message::Colourise, 0, -1);
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
    Require(!editor->Send(Message::GetLineVisible, 2), "shared Collapse All hides nested markup");
    NativeSearch::Command(frame, wxID_FIND);
    auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
    Require(query, "native Find exposes its query control");
    query->SetValue("needle");
    NativeSearch::NativeF3(query);
    Require(editor->Send(Message::GetSelectionStart) == static_cast<intptr_t>(text.find("needle")) &&
                editor->Send(Message::GetLineVisible, 2) && TestEditor::Text(editor) == text &&
                !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
            "native folded Find reveals the exact markup match without editing or history");
    query->SetValue("body");
    auto* findAll = wxDynamicCast(wxWindow::FindWindowByName("Find All", frame), wxButton);
    Require(findAll, "native Find exposes Find All");
    NativeSearch::Click(wxDynamicCast(query->GetParent(), wxDialog), findAll->GetId());
    Require(editor->Send(Message::IndicatorValueAt, 8, text.find("body")) &&
                editor->Send(Message::IndicatorValueAt, 8, text.rfind("body")) && TestEditor::Text(editor) == text &&
                !editor->Send(Message::GetModify),
            "shared occurrence marks cover opening and closing markup tags without editing");
    NativeSearch::Click(wxDynamicCast(query->GetParent(), wxDialog), wxID_CANCEL);
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Expand All"));
    Require(editor->Send(Message::GetLineVisible, 3), "shared Expand All restores markup visibility");
    std::puts("PASS: native HTML/XML tabs, alias/file precedence and folded markup Find");
}
class FileDriver final : public wxTimer {
public:
    explicit FileDriver(wxString path, wxString type = {}) : path_(std::move(path)), type_(std::move(type)) {
        Start(100);
    }
    int choosers = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<FileDriver*>(data);
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                if (IsWindowVisible(window) &&
                    (wxString(title) == "Open file" || wxString(title) == "Save file" ||
                     wxString(title) == "Save As") &&
                    !driver.stage_++) {
                    ++driver.choosers;
                    if (!driver.type_.empty()) NativeDialogs::ChooseFileType(window, driver.type_);
                    NativeDialogs::ChooseFile(window, driver.path_);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget) || !GTK_IS_FILE_CHOOSER(widget)) continue;
            if (stage_ == 0) {
                stage_ = 1;
                ++choosers;
                if (!type_.empty()) NativeDialogs::ChooseFileType(widget, type_);
                NativeDialogs::ChooseFile(widget, path_);
            } else if (stage_ == 1 && !path_.empty() && NativeDialogs::CanAcceptFile(widget, path_)) {
                stage_ = 2;
                NativeDialogs::Respond(widget, GTK_RESPONSE_ACCEPT);
            }
        }
        g_list_free(windows);
#endif
    }

private:
    wxString path_, type_;
    int stage_ = 0;
};
void NativeFiles(EditorFrame* frame) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetSavePoint);
    Require(frame->NewDocument(), "New before native file workflow succeeds");
    const std::string text = "<html>\n<body>value</body>\n</html>\n";
    editor->Send(Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    const auto html = TestFiles::TemporaryDirectory("native.html");
    const auto xml = TestFiles::TemporaryDirectory("native.xml");
    const auto file = [&](int id, const std::filesystem::path& path, const char* type) {
        FileDriver driver(path.empty() ? wxString{} : TestFiles::Filename(path), type);
        TestSyntax::Command(frame, id);
        driver.Stop();
        Require(driver.choosers == 1, "file command must use one native file dialog");
    };
    file(wxID_SAVE, html, "All Files");
    Require(TestFiles::Bytes(html) == text && frame->GetStatusBar()->GetStatusText().Contains("Web Source Code"),
            "native first Save selects HTML and preserves independent disk bytes");
    file(wxID_SAVEAS, xml, "XML Document");
    Require(TestFiles::Bytes(xml) == text && frame->GetStatusBar()->GetStatusText().Contains("XML Document (override)"),
            "native XML filter forces XML with exact saved bytes");
    file(wxID_SAVEAS, {}, "Web Source Code");
    Require(frame->DocumentPath() == TestFiles::Filename(xml) &&
                frame->GetStatusBar()->GetStatusText().Contains("XML Document (override)"),
            "native cancelled Save As preserves current markup dialect");
    file(wxID_OPEN, html, "");
    Require(frame->GetStatusBar()->GetStatusText().Contains("Web Source Code") &&
                !frame->GetStatusBar()->GetStatusText().Contains("override"),
            "native different-path Open resets markup to Automatic");
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "SVG"));
    Require(frame->Open(TestFiles::Filename(html)).status == OpenStatus::Opened &&
                frame->GetStatusBar()->GetStatusText().Contains("SVG (override)"),
            "same-path reopen retains the selected markup dialect");
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Automatic"));
    Require(frame->GetStatusBar()->GetStatusText().Contains("Web Source Code") && TestEditor::Text(editor) == text,
            "explicit Automatic restores HTML without changing text");
    std::puts("PASS: native first Save, XML filter, cancel, Open, same-path override and Automatic");
}
class MarkupApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                const auto path = TestFiles::TemporaryDirectory("sample.html");
                TestFiles::WriteBytes(path, "<html>\n<body id=\"page\">&amp;</body>\n</html>\n");
                DocumentSession document(*frame->Editor());
                Require(document.Open(TestFiles::Filename(path), [] { return true; }).status == OpenStatus::Opened,
                        "HTML Open must succeed");
                Require(LanguageName(document.Language()) == "Web Source Code",
                        "HTML Open must automatically choose the original Web Source Code scheme");
                Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                        "HTML frame Open must succeed");
                Require(frame->Editor()->Send(Message::GetTabWidth) == 2 &&
                            frame->Editor()->Send(Message::GetIndent) == 2 &&
                            !frame->Editor()->Send(Message::GetUseTabs),
                        "HTML must use independent original 2/2 spaces defaults");
                Detection(frame);
                OriginalOracle(frame);
                Commands(frame);
                NativeTabsAndFoldedFind(frame);
                NativeFiles(frame);
                result_ = 0;
                std::puts("PASS: original markup document association");
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
wxIMPLEMENT_APP(MarkupApp);
