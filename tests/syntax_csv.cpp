#include "native_key_helpers.h"
#include "app/localization.h"
#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "syntax_language_test_helpers.h"
#include "native_paint_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <wx/modalhook.h>
#include <wx/radiobut.h>
#include <wx/display.h>
#include <array>
#include <fstream>
#include <sstream>

namespace {
using Scintilla::Message;
using TestLanguages::Require;
void Run(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("columns.csv");
    TestFiles::WriteBytes(path, "one,two,three\r\na,b,c\r\n");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "CSV file opens");
    auto* editor = frame->Editor();
    Require(editor->Send(Message::GetLexer) == SCLEX_CSV, "CSV selects the original column lexer");
    NativePaint::Complete(editor);
    Require(editor->Send(Message::GetStyleIndexAt, 0) == SCE_CSV_COLUMN_0 &&
                editor->Send(Message::GetStyleIndexAt, 4) == SCE_CSV_COLUMN_1,
            "original column styles distinguish CSV fields");
}

void ConfigureOptions(EditorFrame* frame, const std::array<std::string, 12>& fields);
void Oracle(EditorFrame* frame, bool options = false) {
    std::ifstream input(std::filesystem::path(options ? CSV_OPTIONS_REFERENCE : CSV_REFERENCE));
    Require(input.is_open(), "independent original CSV fixture opens");
    size_t count = 0;
    for (std::string line; std::getline(input, line);) {
        if (line.empty() || line.front() == '#') continue;
        std::array<std::string, 12> fields;
        std::istringstream row(line);
        for (auto& field : fields)
            std::getline(row, field, '\t');
        auto* editor = frame->Editor();
        if (fields[2] == "base") {
            editor->Send(Message::SetSavePoint);
            const auto path = TestFiles::TemporaryDirectory(fields[0]);
            TestFiles::WriteBytes(path, TestLanguages::Unhex(fields[3]));
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "CSV oracle fixture opens");
            if (options) ConfigureOptions(frame, fields);
            editor->Send(Message::Colourise, 0, -1);
        } else if (fields[2] == "edit") {
            editor->Send(Message::GotoPos, std::stoul(fields[1]));
            wxKeyEvent event(wxEVT_KEY_DOWN);
            event.m_keyCode = WXK_DELETE;
            event.SetEventObject(editor);
            editor->GetEventHandler()->ProcessEvent(event);
        } else {
            TestSyntax::Command(frame, fields[2] == "undo" ? wxID_UNDO : wxID_REDO);
        }
        NativePaint::Complete(editor);
        editor->Send(Message::Colourise, editor->Send(Message::GetEndStyled), -1);
        const auto compare = [&] {
            Require(TestEditor::Text(editor) == TestLanguages::Unhex(fields[3]),
                    "CSV edit/Undo/Redo bytes match original");
            const auto styles = TestLanguages::Unhex(fields[4]);
            for (size_t position = 0; position < styles.size(); ++position) {
                if (editor->Send(Message::GetStyleIndexAt, position) != static_cast<unsigned char>(styles[position])) {
                    std::fprintf(stderr, "%s %s at %zu\n", fields[0].c_str(), fields[2].c_str(), position);
                    throw TestEditor::Failure("all CSV style bytes match the independent original");
                }
            }
            for (size_t column = 5; column < 7; ++column) {
                std::istringstream values(fields[column]);
                size_t index = 0;
                for (std::string value; std::getline(values, value, ','); ++index)
                    Require(editor->Send(column == 5 ? Message::GetLineState : Message::GetFoldLevel, index) ==
                                std::stoll(value),
                            "all CSV line states and fold levels match the independent original");
                Require(index == static_cast<size_t>(editor->Send(Message::GetLineCount)),
                        "original states cover every line");
            }
        };
        compare();
        editor->Send(Message::ClearDocumentStyle);
        editor->Send(Message::StartStyling, 0);
        editor->Send(Message::Colourise, 0, -1);
        compare();
        ++count;
    }
    Require(count == (options ? 40 : 32), "all original CSV/TSV base/edit/Undo/Redo snapshots execute");
}
class CsvDialogDriver final : public wxModalDialogHook {
public:
    explicit CsvDialogDriver(std::function<void(wxDialog*)> action) : action_(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == wxGetTranslation("CSV Options")) {
            ++opened;
            wxTheApp->CallAfter([this, dialog] {
                NativeKeys::AfterPresentation(dialog, [this, dialog](bool ready) {
                    try {
                        Require(ready, "CSV Options native dialog is presented");
                        Require(!(dialog->GetWindowStyleFlag() & wxRESIZE_BORDER) && !dialog->FindWindow(wxID_CANCEL) &&
                                    dialog->FindWindow(wxID_OK),
                                "CSV Options retains the original fixed layout and sole OK button");
                        Require(dialog->GetDPI() == dialog->GetParent()->GetDPI() &&
                                    wxDisplay(dialog).GetClientArea().Contains(dialog->GetScreenRect()),
                                "CSV Options fits the native work area at the parent DPI");
#ifdef __WXMSW__
                        Require(static_cast<int>(GetDpiForWindow(static_cast<HWND>(dialog->GetHandle()))) ==
                                    dialog->GetDPI().y,
                                "CSV Options reports its actual native window DPI");
#endif
                        const auto inspect = [&](auto&& self, wxWindow* parent) -> void {
                            for (auto* child : parent->GetChildren()) {
                                if (child->IsShown())
                                    Require(dialog->GetScreenRect().Contains(child->GetScreenRect()),
                                            "every CSV control fits at the active language/font/DPI");
                                self(self, child);
                            }
                        };
                        inspect(inspect, dialog);
                        action_(dialog);
                    } catch (...) {
                        failure = std::current_exception();
                        dialog->EndModal(wxID_CANCEL);
                    }
                });
            });
        }
        return wxID_NONE;
    }
    unsigned opened = 0;
    std::exception_ptr failure;

private:
    std::function<void(wxDialog*)> action_;
};
template <class Action> void VisitCsv(EditorFrame* frame, Action action) {
    const auto id =
        frame->GetMenuBar()->FindMenuItem(wxGetTranslation("Sche&me"), wxGetTranslation("CSV File") + "...");
    Require(id != wxNOT_FOUND, "Scheme exposes the original CSV File... options entry");
    CsvDialogDriver driver(action);
    TestSyntax::Command(frame, id);
    if (driver.failure) std::rethrow_exception(driver.failure);
    Require(driver.opened == 1, "CSV command opens exactly one options dialog");
}
void OptionsEntry(EditorFrame* frame) {
    const auto text = TestEditor::Text(frame->Editor());
    VisitCsv(frame, [](wxDialog* dialog) { dialog->Close(); });
    Require(TestEditor::Text(frame->Editor()) == text, "closing CSV Options preserves document bytes");
}
void FontInheritance(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto defaultStyle = static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::Default));
    constexpr intptr_t TextFontHundredths = 1300;
    constexpr intptr_t CodeFontHundredths = 1700;
    const auto codeStyle = frame->GetMenuBar()->FindMenuItem("Scheme", "Use Default Code Style");
    Require(codeStyle != wxNOT_FOUND && !frame->GetMenuBar()->IsChecked(codeStyle) &&
                editor->Send(Message::StyleGetSizeFractional, defaultStyle) == TextFontHundredths,
            "CSV inherits the original plain-text font by default");
    TestSyntax::Command(frame, codeStyle);
    Require(frame->GetMenuBar()->IsChecked(codeStyle) &&
                editor->Send(Message::StyleGetSizeFractional, defaultStyle) == CodeFontHundredths,
            "Use Default Code Style applies the configured code font to CSV");
    TestSyntax::Command(frame, codeStyle);
    Require(!frame->GetMenuBar()->IsChecked(codeStyle) &&
                editor->Send(Message::StyleGetSizeFractional, defaultStyle) == TextFontHundredths,
            "CSV can return to the plain-text font without editing");
}
void Radio(wxDialog* dialog, const char* name) {
    auto* radio = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxRadioButton);
    Require(radio, "original CSV radio option exists");
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(radio->GetHandle()), BM_CLICK, 0, 0);
#else
    gtk_button_clicked(GTK_BUTTON(radio->GetHandle()));
#endif
    Require(radio->GetValue(), "native CSV radio accepts selection");
}
wxCheckBox* Checkbox(wxDialog* dialog, const char* name) {
    auto* control = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxCheckBox);
    Require(control, "original CSV checkbox exists");
    return control;
}
void ConfigureOptions(EditorFrame* frame, const std::array<std::string, 12>& fields) {
    // Original public control IDs are capture data, independent of wx widget IDs.
    constexpr auto delimiters =
        std::to_array<const char*>({"Comma (,)", "Semicolon (;)", "Tab", "Space", "Pipe (|)", "Other:"});
    constexpr auto qualifiers = std::to_array<const char*>({"Double quote", "Single quote", "None"});
    auto* editor = frame->Editor();
    const auto text = TestEditor::Text(editor);
    editor->Send(Message::SetSel, 3, 1);
    VisitCsv(frame, [&](wxDialog* dialog) {
        Radio(dialog, delimiters.at(std::stoul(fields[7]) - 100));
        Radio(dialog, qualifiers.at(std::stoul(fields[8]) - 110));
        NativeSearch::Toggle(Checkbox(dialog, "Backslash escape"), fields[9] == "1");
        NativeSearch::Toggle(Checkbox(dialog, "Merge delimiters"), fields[10] == "1");
        auto* custom = wxDynamicCast(wxWindow::FindWindowByName("Custom delimiter", dialog), wxTextCtrl);
        Require(custom, "custom CSV delimiter text field exists");
        Require(custom->IsEnabled() == (fields[7] == "105"), "only Other enables the custom delimiter");
        custom->SetValue(wxString::FromUTF8(TestLanguages::Unhex(fields[11])));
        NativeSearch::Click(dialog, wxID_OK);
    });
    Require(TestEditor::Text(editor) == text && editor->Send(Message::GetAnchor) == 3 &&
                editor->Send(Message::GetCurrentPos) == 1 && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo),
            "CSV Options preserve text, reversed selection, savepoint and clean history");
}
void RuntimeOptions(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto path = TestFiles::TemporaryDirectory("runtime.csv");
    const std::string original = "a,b,c\n1,2,3\n";
    TestFiles::WriteBytes(path, original);
    editor->Send(Message::SetSavePoint);
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "runtime CSV fixture opens");
    editor->Send(Message::GotoPos, 1);
    wxKeyEvent character(wxEVT_CHAR);
    character.m_keyCode = 'X';
    character.m_uniChar = 'X';
    character.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(character);
    const std::string edited = "aX,b,c\n1,2,3\n";
    Require(TestEditor::Text(editor) == edited, "real CSV character input inserts an undoable character");
    editor->Send(Message::SetSel, 3, 1);
    VisitCsv(frame, [](wxDialog* dialog) {
        Radio(dialog, "Pipe (|)");
        Radio(dialog, "None");
        Require(Checkbox(dialog, "Backslash escape")->GetValue(), "None checks backslash escape as in the original");
        NativeSearch::Toggle(Checkbox(dialog, "Merge delimiters"), true);
        NativeSearch::Click(dialog, wxID_OK);
    });
    const auto preserved = [&] {
        Require(TestEditor::Text(editor) == edited && editor->Send(Message::GetAnchor) == 3 &&
                    editor->Send(Message::GetCurrentPos) == 1 && editor->Send(Message::GetModify) &&
                    editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo),
                "CSV options preserve edited text, reversed selection, savepoint and Undo/Redo availability");
    };
    preserved();
    VisitCsv(frame, [](wxDialog* dialog) {
        Radio(dialog, "Tab");
        dialog->Close();
    });
    preserved();
    VisitCsv(frame, [](wxDialog* dialog) {
        auto* pipe = wxDynamicCast(wxWindow::FindWindowByName("Pipe (|)", dialog), wxRadioButton);
        auto* none = wxDynamicCast(wxWindow::FindWindowByName("None", dialog), wxRadioButton);
        Require(pipe && pipe->GetValue() && none && none->GetValue() &&
                    Checkbox(dialog, "Merge delimiters")->GetValue(),
                "closing restores the previous CSV format options");
        NativeSearch::Click(dialog, wxID_OK);
    });
    preserved();
    VisitCsv(frame, [](wxDialog* dialog) {
        Radio(dialog, "Semicolon (;)");
#ifdef __WXMSW__
        NativeKeys::DialogNavigation(dialog, WXK_ESCAPE);
#else
        wxKeyEvent escape(wxEVT_CHAR_HOOK);
        escape.m_keyCode = WXK_ESCAPE;
        dialog->GetEventHandler()->ProcessEvent(escape);
#endif
    });
    preserved();
    TestLanguages::Choose(frame, "Plain Text");
    VisitCsv(frame, [](wxDialog* dialog) { dialog->Close(); });
    Require(editor->Send(Message::GetLexer) == SCLEX_CSV,
            "cancel still selects CSV when switching from another scheme");
    preserved();
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Dark"));
    preserved();
    TestSyntax::Command(frame, wxID_UNDO);
    Require(TestEditor::Text(editor) == original && !editor->Send(Message::GetModify),
            "options/theme/scheme changes retain the original Undo action and savepoint");
    TestSyntax::Command(frame, wxID_REDO);
    Require(TestEditor::Text(editor) == edited && frame->Save().status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == edited,
            "CSV native edit/Redo/save retains independently checked exact bytes");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "same-path CSV reopens");
    VisitCsv(frame, [](wxDialog* dialog) {
        auto* pipe = wxDynamicCast(wxWindow::FindWindowByName("Pipe (|)", dialog), wxRadioButton);
        Require(pipe && pipe->GetValue() && Checkbox(dialog, "Merge delimiters")->GetValue(),
                "same-file reopen retains manual CSV options");
        NativeSearch::Click(dialog, wxID_OK);
    });
    editor->Send(Message::SetReadOnly, true);
    VisitCsv(frame, [](wxDialog* dialog) {
        Radio(dialog, "Comma (,)");
        NativeSearch::Click(dialog, wxID_OK);
    });
    Require(editor->Send(Message::GetReadOnly) && TestEditor::Text(editor) == edited &&
                !editor->Send(Message::GetModify),
            "CSV Options remain usable in read-only mode without changing text or savepoint");
    editor->Send(Message::SetReadOnly, false);
    const auto other = TestFiles::TemporaryDirectory("new-runtime.csv");
    TestFiles::WriteBytes(other, "a;b;c\n1;2;3\n");
    Require(frame->Open(TestFiles::Filename(other)).status == OpenStatus::Opened, "different CSV opens");
    VisitCsv(frame, [](wxDialog* dialog) {
        auto* delimiter = wxDynamicCast(wxWindow::FindWindowByName("Semicolon (;)", dialog), wxRadioButton);
        auto* doubleQuote = wxDynamicCast(wxWindow::FindWindowByName("Double quote", dialog), wxRadioButton);
        Require(delimiter && delimiter->GetValue() && doubleQuote && doubleQuote->GetValue() &&
                    !Checkbox(dialog, "Merge delimiters")->GetValue() &&
                    !Checkbox(dialog, "Backslash escape")->GetValue(),
                "new automatic CSV sniff resets qualifier/flags with original delimiter priority");
        dialog->Close();
    });
    VisitCsv(frame, [](wxDialog* dialog) {
        Radio(dialog, "Pipe (|)");
        NativeSearch::Click(dialog, wxID_OK);
    });
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Automatic"));
    editor->Send(Message::Colourise, 0, -1);
    Require(editor->Send(Message::GetStyleIndexAt, 1) == SCE_CSV_DELIMITER,
            "Automatic reapplies sniffed CSV options even when the scheme identity stays CSV");
    const auto tsv = TestFiles::TemporaryDirectory("same-scheme-save.tsv");
    Require(frame->SaveAs(TestFiles::Filename(tsv), {}, SaveType::AllFiles).status == SaveStatus::Saved &&
                TestFiles::Bytes(tsv) == "a;b;c\n1;2;3\n",
            "All Files Save As keeps exact CSV bytes under a TSV name");
    editor->Send(Message::Colourise, 0, -1);
    Require(editor->Send(Message::GetStyleIndexAt, 1) == SCE_CSV_COLUMN_0,
            "same-scheme All Files Save As applies the original forced TSV Tab option");
}
void LargeCsv(EditorFrame* frame) {
    constexpr size_t LargeBytes = 32U * 1024U * 1024U;
    constexpr size_t FieldPaddingCharacters = 123;
    std::string bytes = "a,b,c\n";
    const auto record = "a,b," + std::string(FieldPaddingCharacters, 'x') + "\n";
    bytes.reserve(LargeBytes + record.size());
    while (bytes.size() <= LargeBytes)
        bytes += record;
    const auto path = TestFiles::TemporaryDirectory("large.csv");
    TestFiles::WriteBytes(path, bytes);
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "CSV above 32 MiB opens");
    auto* editor = frame->Editor();
    NativePaint::Complete(editor);
    Require(editor->Send(Message::GetLexer) == SCLEX_CSV &&
                editor->Send(Message::GetStyleIndexAt, 0) == SCE_CSV_COLUMN_0,
            "CSV retains the existing large-file highlighting policy");
    TestLanguages::Choose(frame, "Plain Text");
    Require(editor->Send(Message::GetLexer) == SCLEX_NULL && !editor->Send(Message::CanUndo),
            "manual Plain Text pauses large CSV highlighting without editing");
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == bytes &&
                editor->Send(Message::GetLexer) == SCLEX_NULL,
            "saving a large paused CSV preserves independent bytes and manual Plain Text");
    VisitCsv(frame, [](wxDialog* dialog) { dialog->Close(); });
    NativePaint::Complete(editor);
    Require(editor->Send(Message::GetLexer) == SCLEX_CSV && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify) && TestEditor::Text(editor) == bytes,
            "CSV Options can restore large-file highlighting without changing history or bytes");
}
class SchemePicker final : public wxModalDialogHook {
public:
    SchemePicker() { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Select Scheme") {
            wxTheApp->CallAfter([this, dialog] {
                NativeKeys::AfterPresentation(dialog, [this, dialog](bool ready) {
                    try {
                        Require(ready, "F12 scheme chooser is natively presented");
                        auto* tree = wxDynamicCast(wxWindow::FindWindowByName("Syntax schemes", dialog), wxTreeCtrl);
                        Require(tree && NativeDialogs::SelectTreeLabel(*tree, "CSV File"),
                                "F12 exposes CSV through the shared catalog");
                        Require(tree->GetItemText(tree->GetItemParent(tree->GetSelection())) == "Favorite Schemes",
                                "stable original favorite ID 80 selects the CSV favorite");
                        NativeSearch::Click(dialog, wxID_OK);
                    } catch (...) {
                        failure = std::current_exception();
                        dialog->EndModal(wxID_CANCEL);
                    }
                });
            });
        }
        return wxID_NONE;
    }
    std::exception_ptr failure;
};
void ChooserAndFiles(EditorFrame* frame) {
    const auto fixtures = std::to_array<TestLanguages::SchemeFixture>({
        {"CSV File...", SyntaxLanguage::Csv, SCLEX_CSV, "csv", "a,b,c\n1,2,3\n", "", nullptr, nullptr, "CSV File",
         "CSV File"},
        {"CSV File...", SyntaxLanguage::Csv, SCLEX_CSV, "tsv", "a\tb\tc\n1\t2\t3\n", "", nullptr, nullptr, "CSV File",
         "CSV File"},
    });
    TestLanguages::NativeFiles(frame, fixtures);
    TestLanguages::Choose(frame, "Plain Text");
    const auto text = TestEditor::Text(frame->Editor());
    for (unsigned selection = 0; selection < 2; ++selection) {
        SchemePicker picker;
        CsvDialogDriver options([](wxDialog* dialog) { dialog->Close(); });
        NativePaint::Complete(frame->Editor());
        frame->SetTitle("wxnotepad CSV F12");
        NativeKeys::Shortcut(frame, WXK_F12, false, false, false, nullptr, "wxnotepad CSV F12");
        if (picker.failure) std::rethrow_exception(picker.failure);
        if (options.failure) std::rethrow_exception(options.failure);
        Require(options.opened == (selection == 0 ? 1U : 0U), "F12 opens CSV Options only when the scheme changes");
        Require(frame->Editor()->Send(Message::GetLexer) == SCLEX_CSV && TestEditor::Text(frame->Editor()) == text &&
                    !frame->Editor()->Send(Message::CanUndo),
                "F12/favorites preserve CSV bytes and history");
    }
}
class DismissUnexpectedPopup final : public wxTimer {
public:
    void Notify() override {
#ifdef __WXMSW__
        EndMenu();
#else
        if (auto* widget = gtk_grab_get_current(); widget && GTK_IS_MENU(widget)) gtk_menu_popdown(GTK_MENU(widget));
#endif
    }
};
void StatusOptions(EditorFrame* frame) {
    frame->SetClientSize(wxSize(wxDisplay(frame).GetClientArea().width - frame->FromDIP(40), frame->GetClientSize().y));
    NativePaint::Complete(frame->Editor());
    auto* status = frame->GetStatusBar();
    wxRect pane;
    Require(status->GetFieldRect(StatusField::Scheme, pane) && pane.width > 0,
            "wide native status bar exposes CSV scheme pane");
    CsvDialogDriver options([](wxDialog* dialog) { dialog->Close(); });
    DismissUnexpectedPopup popup;
    constexpr int PopupDismissMilliseconds = 50;
    popup.StartOnce(PopupDismissMilliseconds);
    wxMouseEvent click(wxEVT_LEFT_DCLICK);
    click.SetPosition(pane.GetPosition() + wxPoint(pane.width / 2, pane.height / 2));
    click.SetEventObject(status);
    status->GetEventHandler()->ProcessEvent(click);
    popup.Stop();
    if (options.failure) std::rethrow_exception(options.failure);
    Require(options.opened == 1, "original CSV scheme status double-click opens CSV Options directly");
}
class App final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        if (wxGetEnv("WXNOTEPAD_CSV_DPI96", nullptr))
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "create complementary native 96-DPI CSV dialog");
#endif
        wxString language;
        const bool localized = wxGetEnv("WXNOTEPAD_CSV_LANGUAGE", &language);
        if (localized) AppLocalization::Initialize(static_cast<uint16_t>(wxAtoi(language)));
        const auto configuration = TestFiles::TemporaryDirectory("csv.ini");
        TestFiles::WriteBytes(configuration, "[Settings]\nSaveSettings=0\n[Styles]\nFavoriteSchemes=80\n"
                                             "[Global Styles]\nDefault Code Style=font:$(Code); size:17\n"
                                             "Default Text Style=font:$(Text); size:13\n");
        settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(configuration));
        settings_->values.wordWrap = false;
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame, localized](bool ready) {
            try {
                Require(ready, "CSV editor is natively presented before interaction");
#ifdef __WXMSW__
                if (!localized) {
                    const bool gdi = wxGetEnv("WXNOTEPAD_CSV_GDI", nullptr);
                    TestSyntax::Command(frame,
                                        frame->GetMenuBar()->FindMenuItem("Settings", gdi ? "Legacy GDI" : "Direct2D"));
                    Require(frame->Editor()->Send(Message::GetTechnology) ==
                                std::to_underlying(gdi ? Scintilla::Technology::Default
                                                       : Scintilla::Technology::DirectWrite),
                            "CSV runs the requested native renderer");
                }
                if (wxGetEnv("WXNOTEPAD_CSV_DPI96", nullptr))
                    Require(frame->GetDPI().y == 96, "complementary CSV layout uses the native 96-DPI window");
#endif
                Run(frame);
                if (localized)
                    OptionsEntry(frame);
                else {
                    FontInheritance(frame);
                    Oracle(frame);
                    OptionsEntry(frame);
                    Oracle(frame, true);
                    RuntimeOptions(frame);
                    LargeCsv(frame);
                    ChooserAndFiles(frame);
                    StatusOptions(frame);
                }
                std::puts("PASS: original CSV options, snapshots, runtime/file/history and native layout");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "%s\n", error.what());
                result_ = 1;
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
    int OnExit() override { return result_; }

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(App);
