#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "app/find_replace_dialog.h"
#include "app/go_to_line_dialog.h"
#include "app/tab_settings_dialog.h"
#include "settings/settings_store.h"
#include <wx/menu.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/artprov.h>
#include <wx/statbmp.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <cstdlib>
#include <vector>

namespace {
struct ChosenSaveFile {
    wxString path;
    SaveFileType type;
};

void ShowZoomLevelDialog(wxWindow* parent, EditorControl& editor) {
    using Scintilla::Message;
    // Original Dialogs.cpp::ZoomLevelDlgProc and InitZoomLevelComboBox.
    wxDialog dialog(parent, wxID_ANY, "Zoom Level");
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY, "Zoom level (10% to 500%):"), 0, wxALL, dialog.FromDIP(10));
    auto* input =
        new wxComboBox(&dialog, wxID_ANY, wxString::Format("%d%%", static_cast<int>(editor.Send(Message::GetZoom))));
    input->SetName("Zoom level");
    input->SetMaxLength(8);
    for (int level : {500, 450, 400, 350, 300, 250, 200, 175, 150, 125, 100, 75, 50, 25})
        input->Append(wxString::Format("%d%%", level));
    contents->Add(input, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL | wxAPPLY), 0, wxEXPAND | wxALL,
                  dialog.FromDIP(10));
    const auto apply = [&](wxCommandEvent& event) {
        const auto value = input->GetValue().ToStdString(wxConvUTF8);
        char* end = nullptr;
        const long level = std::strtol(value.c_str(), &end, 10);
        if (end != value.c_str() && level >= 10 && level <= 500) editor.Send(Message::SetZoom, level);
        // As in the original, invalid OK closes without changing zoom; Apply
        // keeps the dialog open, and Cancel retains any already applied zoom.
        if (event.GetId() == wxID_OK) dialog.EndModal(wxID_OK);
    };
    dialog.Bind(wxEVT_BUTTON, apply, wxID_OK);
    dialog.Bind(wxEVT_BUTTON, apply, wxID_APPLY);
    dialog.SetSizerAndFit(contents);
    input->SetSelection(-1, -1);
    input->SetFocus();
    dialog.CentreOnParent();
    dialog.ShowModal();
    editor.SetFocus();
}

std::optional<ChosenSaveFile> ChooseSaveFile(wxWindow* parent, const wxString& title, SyntaxLanguage currentLanguage,
                                             const wxString& path) {
    wxString wildcard;
    std::vector<SaveFileType> types;
    const auto addLanguage = [&](const SyntaxDefinition& definition, bool current) {
        wxArrayString patterns;
        for (const auto* extension : definition.extensions)
            patterns.Add("*." + wxString(extension));
        const auto extension = wxFileName(path).GetExt();
        if (current && !extension.empty() && patterns.Index("*." + extension, false) == wxNOT_FOUND)
            patterns.Add("*." + extension);
        const auto pattern = wxJoin(patterns, ';');
        wildcard += wxString(definition.name) + " (" + pattern + ")|" + pattern + "|";
        types.emplace_back(definition.language);
    };
    addLanguage(SyntaxFor(currentLanguage), true);
    for (const auto& definition : SyntaxCatalog()) {
        if (definition.language != currentLanguage) addLanguage(definition, false);
    }
    wildcard += "All Files (*)|*";
    types.emplace_back(SaveType::AllFiles);
    const wxFileName current(path);
    wxFileDialog dialog(parent, title, current.GetPath(), current.GetFullName(), wildcard,
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    dialog.SetFilterIndex(0);
    if (dialog.ShowModal() != wxID_OK) return std::nullopt;
    return ChosenSaveFile{dialog.GetPath(), types.at(static_cast<size_t>(dialog.GetFilterIndex()))};
}
} // namespace

EditorFrame::EditorFrame(SettingsStore* settings)
    : wxFrame(nullptr, wxID_ANY, "Untitled - wxnotepad", wxDefaultPosition, wxSize(900, 600)),
      editor_(new EditorControl(this, settings ? settings->values.selectOption : OriginalSettings{}.selectOption)),
      settings_(settings), indentation_(settings ? settings->values : defaults_),
      document_(*editor_, settings ? settings->values.encoding : EncodingOptions{},
                settings ? SyntaxDetectionOptions{settings->values.autoSelect, settings->values.noCgiGuess}
                         : SyntaxDetectionOptions{}),
      search_(*editor_) {
    indentation_.Apply(*editor_, document_.Language());
    if (settings) {
        document_.SetSaveCleanup({settings->values.fixLineEndings, settings->values.fixTrailingBlanks});
        defaultEolMode_ = settings->values.defaultEOLMode;
        SearchRequest request;
        const int flags = settings->values.findReplaceFlag;
        request.options.matchCase = (flags & 4) != 0;
        request.options.wholeWord = (flags & 2) != 0;
        request.options.regularExpression = (flags & 0x20) != 0;
        request.options.transformBackslashes = (flags & (8 << 10)) != 0;
        request.wrapAround = (settings->values.findReplaceOption & (4 << 4)) == 0;
        request.bookmarkMatchedLines = (settings->values.findReplaceOption & 8) != 0;
        search_.Configure(std::move(request));
        search_.RestoreHistory(settings->values.recentFind, settings->values.recentReplace, (flags & (32 << 10)) != 0);
        editor_->SetLineSelectionMode(static_cast<LineSelectionMode>(settings->values.lineSelection));
        const auto mode = settings->values.defaultEOLMode == 0   ? Scintilla::EndOfLine::CrLf
                          : settings->values.defaultEOLMode == 1 ? Scintilla::EndOfLine::Lf
                                                                 : Scintilla::EndOfLine::Cr;
        document_.SetDefaultEolMode(mode);
        editor_->Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(mode));
        warnLineEndings_ = settings->values.warnLineEndings;
        editor_->Send(Scintilla::Message::SetZoom, settings->values.zoomLevel);
        editor_->SetLineNumbersVisible(settings->values.showLineNumbers);
        if (settings->values.wordWrap) editor_->ToggleWordWrap();
        editor_->Send(Scintilla::Message::SetViewWS,
                      static_cast<uintptr_t>(settings->values.viewWhiteSpace ? Scintilla::WhiteSpace::VisibleAlways
                                                                             : Scintilla::WhiteSpace::Invisible));
    }
    auto* file = new wxMenu;
    file->Append(wxID_NEW, "&New\tCtrl+N");
    file->Append(wxID_OPEN, "&Open...\tCtrl+O");
    const int openGbk = wxWindow::NewControlId();
    file->Append(openGbk, "Open As &GBK...");
    const int openUtf16 = wxWindow::NewControlId();
    file->Append(openUtf16, "Open As UTF-16LE...");
    file->Append(wxID_SAVE, "&Save\tCtrl+S");
    file->Append(wxID_SAVEAS, "Save &As...\tF6");
    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO, "&Undo\tCtrl+Z");
    edit->Append(wxID_REDO, "&Redo\tCtrl+Y");
    edit->AppendSeparator();
    edit->Append(wxID_CUT, "Cu&t\tCtrl+X");
    edit->Append(wxID_COPY, "&Copy\tCtrl+C");
    edit->Append(wxID_PASTE, "&Paste\tCtrl+V");
    edit->AppendSeparator();
    const int indent = wxWindow::NewControlId();
    edit->Append(indent, "&Indent");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->IndentLines(true); }, indent);
    const int unindent = wxWindow::NewControlId();
    edit->Append(unindent, "&Unindent");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->IndentLines(false); }, unindent);
    for (const int id : {indent, unindent})
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetReadOnly)); }, id);
    edit->AppendSeparator();
    edit->Append(wxID_FIND, "&Find...\tCtrl+F");
    edit->Append(wxID_REPLACE, "&Replace...\tCtrl+H");
    edit->Append(wxID_FORWARD, "Find &Next\tF3");
    edit->Append(wxID_BACKWARD, "Find &Previous\tShift+F3");
    const int selectNext = wxWindow::NewControlId();
    const int selectPrevious = wxWindow::NewControlId();
    edit->Append(selectNext, "Select to Ne&xt\tCtrl+Alt+F2");
    edit->Append(selectPrevious, "Select to Previou&s\tCtrl+Alt+Shift+F2");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Search(true, true); }, selectNext);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Search(false, true); }, selectPrevious);
    const int goToLine = wxWindow::NewControlId();
    edit->Append(goToLine, "&Go To Line...\tCtrl+G");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowGoToLineDialog(this, *editor_); }, goToLine);
    const int matchingBrace = wxWindow::NewControlId();
    edit->Append(matchingBrace, "Find Matching &Brace\tCtrl+B");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MatchBrace(); }, matchingBrace);
    const int selectMatchingBrace = wxWindow::NewControlId();
    edit->Append(selectMatchingBrace, "Select to Matching B&race\tCtrl+Shift+B");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MatchBrace(true); }, selectMatchingBrace);
    edit->AppendSeparator();
    const auto addEol = [this, edit](const wxString& label, Scintilla::EndOfLine mode) {
        const int id = wxWindow::NewControlId();
        // wxGTK suppresses activation of an already checked radio item. A
        // conversion must run again after Undo restores mixed line endings.
        edit->AppendCheckItem(id, "Convert to " + label);
        Bind(
            wxEVT_MENU,
            [this, mode](wxCommandEvent&) {
                editor_->Send(Scintilla::Message::ConvertEOLs, static_cast<uintptr_t>(mode));
                editor_->Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(mode));
                UpdateDocumentState();
                UpdateWindowUI(wxUPDATE_UI_RECURSE);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, mode](wxUpdateUIEvent& event) {
                event.Check(editor_->Send(Scintilla::Message::GetEOLMode) == static_cast<intptr_t>(mode));
            },
            id);
    };
    addEol("CRLF", Scintilla::EndOfLine::CrLf);
    addEol("LF", Scintilla::EndOfLine::Lf);
    addEol("CR", Scintilla::EndOfLine::Cr);
    const int lineEndingOptions = wxWindow::NewControlId();
    edit->Append(lineEndingOptions, "Default Line Ending...");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowLineEndingOptions(); }, lineEndingOptions);
    auto* view = new wxMenu;
    const int autoIndent = wxWindow::NewControlId();
    view->AppendCheckItem(autoIndent, "Auto &Indent");
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& preferences = settings_ ? settings_->values : defaults_;
            preferences.autoIndent = !preferences.autoIndent;
            editor_->SetAutoIndent(preferences.autoIndent);
        },
        autoIndent);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->AutoIndentEnabled()); }, autoIndent);
    const int spaces = wxWindow::NewControlId();
    view->AppendCheckItem(spaces, "Tabs as &Spaces");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { indentation_.ToggleSpaces(*editor_, document_.Language()); }, spaces);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(!editor_->Send(Scintilla::Message::GetUseTabs)); }, spaces);
    const int tabSettings = wxWindow::NewControlId();
    view->Append(tabSettings, "&Tab Settings...");
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& preferences = settings_ ? settings_->values : defaults_;
            if (ShowTabSettingsDialog(this, preferences, document_.Language(), indentation_)) {
                if (settings_) settings_->SaveSchemeTabs(document_.Language());
                indentation_.Apply(*editor_, document_.Language());
                UpdatePositionStatus();
            }
        },
        tabSettings);
    auto* lineSelection = new wxMenu;
    const char* lineModes[] = {"&None", "&Visual Studio", "&Normal", "&Old Visual Studio"};
    for (int value = 0; value < 4; ++value) {
        const auto mode = static_cast<LineSelectionMode>(value);
        const int id = wxWindow::NewControlId();
        lineSelection->AppendRadioItem(id, lineModes[value]);
        Bind(wxEVT_MENU, [this, mode](wxCommandEvent&) { editor_->SetLineSelectionMode(mode); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, mode](wxUpdateUIEvent& event) { event.Check(editor_->GetLineSelectionMode() == mode); }, id);
    }
    edit->AppendSubMenu(lineSelection, "Line Selection &Mode");
    auto* menus = new wxMenuBar;
    menus->Append(file, "&File");
    menus->Append(edit, "&Edit");
    auto* languages = new wxMenu;
    const auto addLanguage = [this, languages](const wxString& label, std::optional<SyntaxLanguage> language) {
        const int id = wxWindow::NewControlId();
        languages->AppendRadioItem(id, label);
        Bind(
            wxEVT_MENU,
            [this, id, language](wxCommandEvent&) {
                GetMenuBar()->Check(id, true);
                ChooseLanguage(language);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, language](wxUpdateUIEvent& event) { event.Check(document_.LanguageOverride() == language); }, id);
        return id;
    };
    automaticLanguageId_ = addLanguage("&Automatic", std::nullopt);
    for (const auto& definition : SyntaxCatalog())
        addLanguage("&" + wxString(definition.name), definition.language);
    menus->Append(languages, "&Language");
    view->AppendSeparator();
    auto* zoom = new wxMenu;
    const auto addZoom = [this, zoom](const wxString& label, Scintilla::Message message, uintptr_t value = 0) {
        const int id = wxWindow::NewControlId();
        zoom->Append(id, label);
        Bind(wxEVT_MENU, [this, message, value](wxCommandEvent&) { editor_->Send(message, value); }, id);
    };
    addZoom("Zoom &In\tCtrl++", Scintilla::Message::ZoomIn);
    addZoom("Zoom &Out\tCtrl+-", Scintilla::Message::ZoomOut);
    const int zoomLevel = wxWindow::NewControlId();
    zoom->Append(zoomLevel, "Zoom &Level...");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowZoomLevelDialog(this, *editor_); }, zoomLevel);
    addZoom("&Reset Zoom\tCtrl+\\", Scintilla::Message::SetZoom, 100);
    view->AppendSubMenu(zoom, "&Zoom");
    view->AppendSeparator();
    const int wordWrap = wxWindow::NewControlId();
    view->AppendCheckItem(wordWrap, "&Word Wrap\tCtrl+Shift+W");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->ToggleWordWrap(); }, wordWrap);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetWrapMode) != 0); }, wordWrap);
    const int lineNumbers = wxWindow::NewControlId();
    view->AppendCheckItem(lineNumbers, "Line &Numbers\tCtrl+Shift+N");
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetLineNumbersVisible(!editor_->LineNumbersVisible()); },
        lineNumbers);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->LineNumbersVisible()); }, lineNumbers);
    const int whitespace = wxWindow::NewControlId();
    view->AppendCheckItem(whitespace, "Show &Whitespace\tCtrl+Shift+8");
    Bind(
        wxEVT_MENU,
        [this, whitespace](wxCommandEvent&) {
            const bool visible = editor_->Send(Scintilla::Message::GetViewWS) == 0;
            editor_->Send(Scintilla::Message::SetViewWS,
                          static_cast<uintptr_t>(visible ? Scintilla::WhiteSpace::VisibleAlways
                                                         : Scintilla::WhiteSpace::Invisible));
            GetMenuBar()->Check(whitespace, visible);
        },
        whitespace);
    Bind(wxEVT_CHAR_HOOK, [this, whitespace](wxKeyEvent& event) {
        if ((event.GetKeyCode() == '8' || event.GetKeyCode() == '*') && event.ControlDown() && event.ShiftDown() &&
            !event.AltDown() && !event.MetaDown()) {
            wxCommandEvent command(wxEVT_MENU, whitespace);
            GetEventHandler()->ProcessEvent(command);
        } else {
            event.Skip();
        }
    });
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetViewWS) != 0); }, whitespace);
    view->AppendSeparator();
    const auto addFoldCommand = [this, view](const wxString& label, uintptr_t action) {
        const int id = wxWindow::NewControlId();
        view->Append(id, label);
        Bind(
            wxEVT_MENU,
            [this, action](wxCommandEvent&) {
                if (LanguageCanFold(document_.Language())) editor_->Send(Scintilla::Message::FoldAll, action);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(LanguageCanFold(document_.Language())); },
            id);
    };
    addFoldCommand("&Collapse All", static_cast<uintptr_t>(Scintilla::FoldAction::Contract) |
                                        static_cast<uintptr_t>(Scintilla::FoldAction::ContractEveryLevel));
    addFoldCommand("&Expand All", static_cast<uintptr_t>(Scintilla::FoldAction::Expand));
    if (settings_) {
        view->AppendSeparator();
        const int saveOnExit = wxWindow::NewControlId();
        view->AppendCheckItem(saveOnExit, "Save Settings On E&xit");
        Bind(
            wxEVT_MENU, [this](wxCommandEvent&) { settings_->values.saveSettings = !settings_->values.saveSettings; },
            saveOnExit);
        Bind(
            wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(settings_->values.saveSettings); },
            saveOnExit);
        const int saveNow = wxWindow::NewControlId();
        view->Append(saveNow, "Save Settings &Now");
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                if (SaveSettings(true))
                    SetStatusText("Settings saved");
                else
                    wxMessageBox("Could not write " + settings_->Path(), "Could not save settings", wxOK | wxICON_ERROR,
                                 this);
            },
            saveNow);
        const int saveFind = wxWindow::NewControlId();
        view->AppendCheckItem(saveFind, "Save Find/Replace");
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) { settings_->values.saveFindReplace = !settings_->values.saveFindReplace; },
            saveFind);
        Bind(
            wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(settings_->values.saveFindReplace); },
            saveFind);
        for (const auto& [label, bit] :
             {std::pair{"Copy Selection As Find Text", 2}, std::pair{"Copy Paste Buffer As Find Text", 4}}) {
            const int id = wxWindow::NewControlId();
            view->AppendCheckItem(id, label);
            Bind(wxEVT_MENU, [this, bit](wxCommandEvent&) { settings_->values.selectOption ^= bit; }, id);
            Bind(
                wxEVT_UPDATE_UI,
                [this, bit](wxUpdateUIEvent& event) { event.Check((settings_->values.selectOption & bit) != 0); }, id);
        }
    }
    menus->Append(view, "&View");
    SetMenuBar(menus);
    CreateStatusBar(3);
    search_.SetWrapConfirmation([this](SearchDirection direction) { return SearchMessage(direction); });
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
        UpdatePositionStatus();
        event.Skip();
    });
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFind(); }, wxID_FIND);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFind(true); }, wxID_REPLACE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNext(); }, wxID_FORWARD);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindPrevious(); }, wxID_BACKWARD);
    Bind(wxEVT_CHAR_HOOK, &EditorFrame::OnSearchKey, this);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = OpenAs(FileEncoding::Gbk);
            if (result.status == OpenStatus::Failed)
                wxMessageBox(result.error, "Could not open file", wxOK | wxICON_ERROR, this);
        },
        openGbk);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { New(); }, wxID_NEW);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = Open();
            if (result.status == OpenStatus::Failed)
                wxMessageBox(result.error, "Could not open file", wxOK | wxICON_ERROR, this);
        },
        wxID_OPEN);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = OpenAs(FileEncoding::Utf16Le);
            if (result.status == OpenStatus::Failed)
                wxMessageBox(result.error, "Could not open file", wxOK | wxICON_ERROR, this);
        },
        openUtf16);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = Save();
            if (result.status == SaveStatus::Failed)
                wxMessageBox(result.error, "Could not save file", wxOK | wxICON_ERROR, this);
        },
        wxID_SAVE);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, wxID_SAVE);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = SaveAs();
            if (result.status == SaveStatus::Failed)
                wxMessageBox(result.error, "Could not save file", wxOK | wxICON_ERROR, this);
        },
        wxID_SAVEAS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Undo); }, wxID_UNDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Redo); }, wxID_REDO);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanUndo)); },
        wxID_UNDO);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanRedo)); },
        wxID_REDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Copy(); }, wxID_COPY);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Cut(); }, wxID_CUT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Paste); }, wxID_PASTE);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); }, wxID_COPY);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Enable(!editor_->Send(Scintilla::Message::GetReadOnly) &&
                         editor_->Send(Scintilla::Message::GetLength) != 0);
        },
        wxID_CUT);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanPaste)); },
        wxID_PASTE);
    Bind(wxEVT_EDITOR_STATE_CHANGED, [this](wxCommandEvent&) { UpdateDocumentState(); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (event.CanVeto() && !ConfirmDiscardChanges("closing")) {
            event.Veto();
            return;
        }
        SaveSettings(false);
        Destroy();
    });
    UpdateDocumentState();
    editor_->SetFocus();
}

bool EditorFrame::SaveSettings(bool explicitSave) {
    if (!settings_) return true;
    settings_->values.wordWrap = editor_->Send(Scintilla::Message::GetWrapMode) != 0;
    settings_->values.showLineNumbers = editor_->LineNumbersVisible();
    settings_->values.viewWhiteSpace = editor_->Send(Scintilla::Message::GetViewWS) != 0;
    settings_->values.warnLineEndings = warnLineEndings_;
    settings_->values.defaultEOLMode = defaultEolMode_;
    settings_->values.fixLineEndings = document_.SaveCleanup().fixLineEndings;
    settings_->values.fixTrailingBlanks = document_.SaveCleanup().fixTrailingBlanks;
    settings_->values.zoomLevel = static_cast<int>(editor_->Send(Scintilla::Message::GetZoom));
    const auto& request = search_.Request();
    // Preserve settings bits owned by other original options while updating
    // this dialog's implemented flags. Regex is SCFIND_REGEXP (0x20).
    auto& flags = settings_->values.findReplaceFlag;
    flags &= ~(4 | 2 | 0x20 | 0x40 | 0x80 | (8 << 10) | (32 << 10));
    flags |=
        (request.options.matchCase ? 4 : 0) | (request.options.wholeWord ? 2 : 0) |
        (request.options.regularExpression ? (0x20 | ((settings_->values.findReplaceOption & 1) ? 0x80 : 0x40)) : 0) |
        (request.options.transformBackslashes ? (8 << 10) : 0) | (search_.LastReplaceEmpty() ? (32 << 10) : 0);
    settings_->values.findReplaceOption = (settings_->values.findReplaceOption & ~((4 << 4) | 8)) |
                                          (request.wrapAround ? 0 : (4 << 4)) | (request.bookmarkMatchedLines ? 8 : 0);
    settings_->values.recentFind = search_.RecentFind();
    settings_->values.recentReplace = search_.RecentReplace();
    settings_->values.lineSelection = static_cast<int>(editor_->GetLineSelectionMode());
    return settings_->Save(explicitSave);
}

void EditorFrame::ShowLineEndingOptions() {
    // Original Dialogs.cpp::SelectDefLineEndingDlgProc and IDD_DEFEOLMODE.
    wxDialog dialog(this, wxID_ANY, "Line Endings");
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY, "&Default line ending mode:"), 0, wxALL, dialog.FromDIP(10));
    auto* mode = new wxChoice(&dialog, wxID_ANY);
    mode->SetName("Default line ending mode");
    mode->Append("Windows (CR+LF)");
    mode->Append("Unix/macOS (LF)");
    mode->Append("Classic Mac OS (CR)");
    mode->SetSelection(defaultEolMode_);
    contents->Add(mode, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    const auto option = [&](const char* label, bool value) {
        auto* check = new wxCheckBox(&dialog, wxID_ANY, label);
        check->SetValue(value);
        contents->Add(check, 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
        return check;
    };
    auto* warn = option("Show inconsistent line endings warning on loading.", warnLineEndings_);
    auto* fix = option("&Ensure consistent line endings when saving.", document_.SaveCleanup().fixLineEndings);
    auto* strip = option("&Strip trailing blanks when saving.", document_.SaveCleanup().fixTrailingBlanks);
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(contents);
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return;
    defaultEolMode_ = mode->GetSelection();
    const auto defaultMode = defaultEolMode_ == 0   ? Scintilla::EndOfLine::CrLf
                             : defaultEolMode_ == 1 ? Scintilla::EndOfLine::Lf
                                                    : Scintilla::EndOfLine::Cr;
    document_.SetDefaultEolMode(defaultMode);
    warnLineEndings_ = warn->GetValue();
    document_.SetSaveCleanup({fix->GetValue(), strip->GetValue()});
}

void EditorFrame::ShowFind(bool replace) {
    if (!findDialog_)
        findDialog_ = new FindReplaceDialog(
            this, *editor_, search_, [this](const FindResult& result) { ReportFind(result); },
            [this](wxKeyEvent& event) { OnSearchKey(event); }, settings_ ? settings_->values.selectOption : 15);
    findDialog_->Present(replace, settings_ ? settings_->values.selectOption : 15);
}

void EditorFrame::OnSearchKey(wxKeyEvent& event) {
    if (event.GetKeyCode() == 'B' && event.ControlDown() && !event.AltDown() && !event.MetaDown())
        editor_->MatchBrace(event.ShiftDown());
    else if (event.GetKeyCode() == 'W' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
             !event.MetaDown())
        editor_->ToggleWordWrap();
    else if (event.GetKeyCode() == 'N' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
             !event.MetaDown())
        editor_->SetLineNumbersVisible(!editor_->LineNumbersVisible());
    else if (event.GetKeyCode() == 'F' && event.ControlDown() && !event.AltDown() && !event.ShiftDown())
        ShowFind();
    else if (event.GetKeyCode() == 'H' && event.ControlDown() && !event.AltDown() && !event.ShiftDown())
        ShowFind(true);
    else if (event.GetKeyCode() == WXK_F2 && event.ControlDown() && event.AltDown() && !event.MetaDown())
        Search(!event.ShiftDown(), true);
    else if (event.GetKeyCode() == WXK_F3 && !event.HasAnyModifiers())
        FindNext();
    else if (event.GetKeyCode() == WXK_F3 && event.ShiftDown() && !event.ControlDown() && !event.AltDown() &&
             !event.MetaDown())
        FindPrevious();
    else if (event.GetKeyCode() == WXK_F6) {
        // GTK Wayland can consume Ctrl while matching F6 accelerators. Route
        // the original exact modifiers before native accelerator matching.
        if (!event.HasAnyModifiers()) {
            wxCommandEvent command(wxEVT_MENU, wxID_SAVEAS);
            GetEventHandler()->ProcessEvent(command);
        }
    } else
        event.Skip();
}

void EditorFrame::FindNext() {
    Search(true);
}

void EditorFrame::FindPrevious() {
    Search(false);
}

void EditorFrame::Search(bool forward, bool extendSelection) {
    if (!search_.HasFindText()) {
        ShowFind();
        return;
    }
    ReportFind(search_.Find(forward ? SearchDirection::Forward : SearchDirection::Backward, extendSelection));
}

void EditorFrame::ReportFind(const FindResult& result) {
    switch (result.status) {
    case FindStatus::Found:
        SetStatusText(result.wrapped ? "Search wrapped" : "Match found", 2);
        break;
    case FindStatus::NotFound:
        SetStatusText("No match found", 2);
        ShowSearchMessage("MsgNotFound", "The specified text was not found.", false);
        break;
    case FindStatus::InvalidPattern:
        SetStatusText("Invalid regular expression", 2);
        break;
    case FindStatus::EmptyQuery:
        ShowSearchMessage("MsgNotFound", "The specified text was not found.", false);
        break;
    case FindStatus::Cancelled:
        break;
    }
}

bool EditorFrame::SearchMessage(SearchDirection direction) {
    return direction == SearchDirection::Forward
               ? ShowSearchMessage("MsgFindWrap1",
                                   "Reached the end of the document, restarting search at the beginning.", true)
               : ShowSearchMessage("MsgFindWrap2",
                                   "Reached the beginning of the document, restarting search at the end.", true);
}

bool EditorFrame::ShowSearchMessage(const wxString& key, const wxString& message, bool confirm) {
    // Pinned Dialogs.cpp::InfoBox: 0 shows the choice, 1 returns OK without
    // showing it, and 2 shows it with message suppression disabled.
    const size_t index = key == "MsgFindWrap1" ? 0 : key == "MsgFindWrap2" ? 1 : 2;
    int& mode = suppressedSearchMessages_[index];
    if (settings_) mode = settings_->MessageSuppression(key);
    if (mode == 1) return true;
    auto* focused = wxWindow::FindFocus();
    wxWindow* owner = findDialog_ && findDialog_->IsShown() ? static_cast<wxWindow*>(findDialog_) : this;
    wxDialog dialog(owner, wxID_ANY, "wxnotepad");
    dialog.SetName("Search feedback");
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* contents = new wxBoxSizer(wxHORIZONTAL);
    contents->Add(
        new wxStaticBitmap(&dialog, wxID_ANY,
                           wxArtProvider::GetBitmap(confirm ? wxART_INFORMATION : wxART_WARNING, wxART_MESSAGE_BOX)),
        0, wxALL, 16);
    auto* text = new wxStaticText(&dialog, wxID_ANY, message);
    text->SetName(key);
    contents->Add(text, 0, wxALL | wxALIGN_CENTER_VERTICAL, 16);
    layout->Add(contents);
    auto* suppress = new wxCheckBox(&dialog, wxID_ANY, "&Don't show this message again.");
    suppress->SetName("Suppress search message");
    suppress->Enable(mode != 2);
    layout->Add(suppress, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);
    layout->Add(dialog.CreateStdDialogButtonSizer(confirm ? wxOK | wxCANCEL : wxOK), 0, wxALL | wxALIGN_RIGHT, 8);
    dialog.SetSizerAndFit(layout);
    dialog.CentreOnParent();
    const int response = dialog.ShowModal();
    if (suppress->GetValue()) {
        mode = 1;
        if (settings_) settings_->SuppressMessage(key);
    }
    if (focused && focused->IsShownOnScreen()) focused->SetFocus();
    return response == wxID_OK;
}

void EditorFrame::UpdateDocumentState() {
    const wxString name = document_.Path().empty() ? wxString("Untitled") : document_.Path();
    SetTitle((editor_->Send(Scintilla::Message::GetModify) ? "*" : "") + name + " - wxnotepad");
    const wxString encoding = document_.Encoding() == FileEncoding::Utf16Le ? "UTF-16LE"
                              : document_.Encoding() == FileEncoding::Gbk   ? "GBK (CP936)"
                                                                            : "UTF-8";
    const auto eolMode = static_cast<Scintilla::EndOfLine>(editor_->Send(Scintilla::Message::GetEOLMode));
    const wxString eol = eolMode == Scintilla::EndOfLine::CrLf ? "CRLF"
                         : eolMode == Scintilla::EndOfLine::Cr ? "CR"
                                                               : "LF";
    SetStatusText(encoding + (document_.HasBom() ? " (BOM)" : "") + " | " + LanguageName(document_.Language()) +
                  (document_.LanguageOverride() ? " (override)" : "") + " | EOL: " + eol);
    UpdatePositionStatus();
}

void EditorFrame::UpdatePositionStatus() {
    const auto caret = editor_->Send(Scintilla::Message::GetCurrentPos);
    const auto line = editor_->Send(Scintilla::Message::LineFromPosition, caret);
    const auto column = editor_->Send(Scintilla::Message::GetColumn, caret, line);
    wxString position = wxString::Format("Ln %llu, Col %llu", static_cast<unsigned long long>(line) + 1,
                                         static_cast<unsigned long long>(column) + 1);
    const auto selected = editor_->SelectionCharacterCount();
    if (selected) position += wxString::Format(" | Sel %lld", static_cast<long long>(selected));
    SetStatusText(position, 1);
    auto* status = GetStatusBar();
    const int metadataWidth = status->GetTextExtent(status->GetStatusText()).x + FromDIP(20);
    const int positionWidth = status->GetTextExtent(position).x + FromDIP(20);
    const int widths[] = {metadataWidth, positionWidth, -1};
    if (status->GetStatusWidth(0) != metadataWidth || status->GetStatusWidth(1) != positionWidth)
        status->SetStatusWidths(3, widths);
}

void EditorFrame::ChooseLanguage(std::optional<SyntaxLanguage> language) {
    document_.SelectLanguage(language);
    indentation_.Apply(*editor_, document_.Language());
    UpdateDocumentState();
}

SaveResult EditorFrame::SaveFirst(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss,
                                  SaveFileType fileType) {
    const auto result =
        document_.SaveFirst(destination, decideLoss ? decideLoss : [this] { return ConfirmGbkLoss(); }, fileType);
    if (result.status == SaveStatus::Saved) indentation_.Apply(*editor_, document_.Language());
    UpdateWindowUI(wxUPDATE_UI_RECURSE);
    UpdateDocumentState();
    return result;
}

SaveResult EditorFrame::Save(const GbkLossDecision& decideLoss) {
    if (!document_.Path().empty()) {
        const auto result = document_.Save(decideLoss ? decideLoss : [this] { return ConfirmGbkLoss(); });
        UpdateDocumentState();
        return result;
    }
    const auto chosen = ChooseSaveFile(this, "Save file", document_.Language(), document_.Path());
    if (!chosen) return SaveFirst(std::nullopt, decideLoss);
    return SaveFirst(chosen->path, decideLoss, chosen->type);
}

SaveResult EditorFrame::SaveAs(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss,
                               SaveFileType fileType) {
    const auto result =
        document_.SaveAs(destination, decideLoss ? decideLoss : [this] { return ConfirmGbkLoss(); }, fileType);
    if (result.status == SaveStatus::Saved) indentation_.Apply(*editor_, document_.Language());
    UpdateWindowUI(wxUPDATE_UI_RECURSE);
    UpdateDocumentState();
    return result;
}

SaveResult EditorFrame::SaveAs() {
    const auto chosen = ChooseSaveFile(this, "Save As", document_.Language(), document_.Path());
    if (!chosen) return SaveAs(std::nullopt);
    return SaveAs(chosen->path, {}, chosen->type);
}

OpenResult EditorFrame::Open(const std::optional<wxString>& source, std::optional<FileEncoding> encoding) {
    const auto result = document_.Open(
        source, [this] { return ConfirmDiscardChanges("opening another file"); }, encoding, std::nullopt,
        [this] { indentation_.Load(*editor_, document_.Language(), document_.FileVariables()); });
    if (result.status == OpenStatus::Opened) {
        if (result.lineEndings.Mixed() && warnLineEndings_)
            ConfirmLineEndingConversion(result.lineEndings, result.unknownLanguage && document_.IsDefaultEncoding());
        UpdateWindowUI(wxUPDATE_UI_RECURSE);
    }
    UpdateDocumentState();
    return result;
}

OpenResult EditorFrame::Open() {
    wxFileDialog dialog(this, "Open file", {}, {}, "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return Open(std::nullopt);
    return Open(dialog.GetPath());
}

OpenResult EditorFrame::OpenAs(FileEncoding encoding) {
    wxFileDialog dialog(this, "Open file", {}, {}, "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return Open(std::nullopt, encoding);
    return Open(dialog.GetPath(), encoding);
}

bool EditorFrame::New() {
    if (!ConfirmDiscardChanges("creating a new document")) return false;
    document_.New();
    indentation_.Reset(*editor_, document_.Language());
    GetMenuBar()->Check(automaticLanguageId_, true);
    UpdateDocumentState();
    return true;
}

bool EditorFrame::ConfirmDiscardChanges(const wxString& action) {
    if (!editor_->Send(Scintilla::Message::GetModify)) return true;
    // Pinned Notepad4 FileSave/IsASpace: only short ASCII-whitespace Untitled
    // documents bypass the lifecycle prompt; explicit Save still writes them.
    if (document_.Path().empty()) {
        const auto length = editor_->Send(Scintilla::Message::GetLength);
        if (length < 2048) {
            const auto* bytes =
                reinterpret_cast<const char*>(editor_->Send(Scintilla::Message::GetRangePointer, 0, length));
            bool whitespaceOnly = true;
            for (intptr_t index = 0; bytes && index < length; ++index) {
                const char ch = bytes[index];
                if (!(ch == ' ' || (ch >= 0x09 && ch <= 0x0d))) {
                    whitespaceOnly = false;
                    break;
                }
            }
            if (whitespaceOnly) return true;
        }
    }
    const wxString name = document_.Path().empty() ? wxString("Untitled") : document_.Path();
    wxMessageDialog dialog(this, "Save changes to " + name + " before " + action + "?", "Save changes?",
                           wxYES_NO | wxCANCEL | wxICON_QUESTION);
    dialog.SetYesNoCancelLabels("&Save", "&Discard", "&Cancel");
    const int choice = dialog.ShowModal();
    if (choice == wxID_NO) return true;
    if (choice != wxID_YES) return false;
    const auto result = Save();
    if (result.status == SaveStatus::Failed)
        wxMessageBox(result.error, "Could not save file", wxOK | wxICON_ERROR, this);
    return result.status == SaveStatus::Saved;
}

GbkLossChoice EditorFrame::ConfirmGbkLoss() {
    wxMessageDialog dialog(
        this,
        "Certain characters in the current text are not supported by the selected encoding, and may be replaced "
        "by default placeholders when saving. It's recommended to choose another file encoding. Continue?",
        "GBK character loss", wxOK | wxCANCEL | wxICON_WARNING);
    return dialog.ShowModal() == wxID_OK ? GbkLossChoice::Replace : GbkLossChoice::Cancel;
}

void EditorFrame::ConfirmLineEndingConversion(const LineEndingCounts& counts, bool defaultNo) {
    wxDialog dialog(this, wxID_ANY, "Inconsistent Line Endings");
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY, "The line endings in this file are not consistent:"), 0, wxALL,
                  dialog.FromDIP(10));
    contents->Add(new wxStaticText(&dialog, wxID_ANY,
                                   wxString::Format("CR+LF: %llu line(s).\nLF: %llu line(s).\nCR: %llu line(s).",
                                                    static_cast<unsigned long long>(counts.crlf),
                                                    static_cast<unsigned long long>(counts.lf),
                                                    static_cast<unsigned long long>(counts.cr))),
                  0, wxLEFT | wxRIGHT, dialog.FromDIP(10));
    contents->Add(new wxStaticText(&dialog, wxID_ANY, "&Do you want to normalize the line endings?"), 0, wxALL,
                  dialog.FromDIP(10));
    const wxString names[] = {"Windows (CR+LF)", "Unix/macOS (LF)", "Classic Mac OS (CR)"};
    auto* mode = new wxChoice(&dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, 3, names);
    mode->SetName("Line ending conversion");
    const auto current = static_cast<Scintilla::EndOfLine>(editor_->Send(Scintilla::Message::GetEOLMode));
    mode->SetSelection(current == Scintilla::EndOfLine::CrLf ? 0 : current == Scintilla::EndOfLine::Lf ? 1 : 2);
    contents->Add(mode, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    auto* always = new wxCheckBox(&dialog, wxID_ANY, "&Always show this dialog.");
    always->SetValue(warnLineEndings_);
    contents->Add(always, 0, wxALL, dialog.FromDIP(10));
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* yes = new wxButton(&dialog, wxID_OK, "&Yes");
    auto* no = new wxButton(&dialog, wxID_CANCEL, "&No");
    buttons->Add(yes, 0, wxRIGHT, dialog.FromDIP(5));
    buttons->Add(no);
    contents->Add(buttons, 0, wxALIGN_RIGHT | wxALL, dialog.FromDIP(10));
    (defaultNo ? no : yes)->SetDefault();
    dialog.SetSizerAndFit(contents);
    dialog.CentreOnParent();
    const bool convert = dialog.ShowModal() == wxID_OK;
    warnLineEndings_ = always->GetValue();
    if (convert) {
        const auto selected = mode->GetSelection() == 0   ? Scintilla::EndOfLine::CrLf
                              : mode->GetSelection() == 1 ? Scintilla::EndOfLine::Lf
                                                          : Scintilla::EndOfLine::Cr;
        editor_->Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(selected));
        editor_->Send(Scintilla::Message::ConvertEOLs, static_cast<uintptr_t>(selected));
    }
}
