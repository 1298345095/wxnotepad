#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "app/dialogs/go_to_line_dialog.h"
#include "app/dialogs/enclose_selection_dialog.h"
#include "app/dialogs/tab_settings_dialog.h"
#include "settings/settings_store.h"
#include <wx/accel.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <cstdlib>

namespace {
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
} // namespace

void EditorFrame::CreateMenus() {
    auto* menus = new wxMenuBar;
    menus->Append(CreateFileMenu(), "&File");
    menus->Append(CreateEditMenu(), "&Edit");
    menus->Append(CreateLanguageMenu(), "&Language");
    menus->Append(CreateViewMenu(), "&View");
    SetMenuBar(menus);
}

wxMenu* EditorFrame::CreateFileMenu() {
    auto* file = new wxMenu;
    file->Append(wxID_NEW, "&New\tCtrl+N");
    file->Append(wxID_OPEN, "&Open...\tCtrl+O");
    const int openGbk = wxWindow::NewControlId();
    file->Append(openGbk, "Open As &GBK...");
    const int openUtf16 = wxWindow::NewControlId();
    file->Append(openUtf16, "Open As UTF-16LE...");
    file->Append(wxID_SAVE, "&Save\tCtrl+S");
    file->Append(wxID_SAVEAS, "Save &As...\tF6");
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
    return file;
}

wxMenu* EditorFrame::CreateEditMenu() {
    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO, "&Undo\tCtrl+Z");
    edit->Append(wxID_REDO, "&Redo\tCtrl+Y");
    edit->AppendSeparator();
    edit->Append(wxID_CUT, "Cu&t\tCtrl+X");
    edit->Append(wxID_COPY, "&Copy\tCtrl+C");
    edit->Append(wxID_PASTE, "&Paste\tCtrl+V");
    edit->AppendSeparator();
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
    AppendEditingCommands(edit);
    edit->AppendSeparator();
    AppendSearchCommands(edit);
    AppendNavigationCommands(edit);
    edit->AppendSeparator();
    AppendLineEndingCommands(edit);
    AppendLineSelectionCommands(edit);
    return edit;
}

void EditorFrame::AppendEditingCommands(wxMenu* edit) {
    auto* conversion = new wxMenu;
    // Pinned Notepad4's case commands use CustomCaseMapping values upper=1, lower=2.
    for (const auto& [label, mapping] :
         {std::pair{"&Upper Case\tCtrl+Shift+U", 1}, std::pair{"&Lower Case\tCtrl+U", 2}}) {
        const int id = wxWindow::NewControlId();
        conversion->Append(id, label);
        Bind(
            wxEVT_MENU,
            [this, mapping](wxCommandEvent&) { editor_->Send(Scintilla::Message::CustomCaseMapping, mapping); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    edit->AppendSubMenu(conversion, "Con&vert");
    auto* enclosure = new wxMenu;
    const int customEnclosure = wxWindow::NewControlId();
    enclosure->Append(customEnclosure, "&With...\tAlt+Q");
    enclosure->AppendSeparator();
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) { ShowEncloseSelectionDialog(this, *editor_, enclosureBefore_, enclosureAfter_); },
        customEnclosure);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, customEnclosure);
    const auto addEnclosure = [this, enclosure](const wxString& label, const wxString& delimiter) {
        const int id = wxWindow::NewControlId();
        enclosure->Append(id, label);
        Bind(
            wxEVT_MENU,
            [this, delimiter](wxCommandEvent&) {
                if (!editor_->EncloseSelection(delimiter, delimiter))
                    wxMessageBox("This operation can't be performed within a rectangular selection.", "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
            },
            id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    };
    addEnclosure("Triple &Single Quotes\tCtrl+3", "'''");
    addEnclosure("Triple &Double Quotes\tCtrl+6", "\"\"\"");
    addEnclosure("Triple &Backticks\tCtrl+9", "```");
    edit->AppendSubMenu(enclosure, "&Enclose Selection");
    const int stripLeading = wxWindow::NewControlId();
    const int stripTrailing = wxWindow::NewControlId();
    edit->Append(stripTrailing, "Strip &Trailing Blanks\tAlt+T");
    edit->Append(stripLeading, "Strip Leading Blan&ks");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->StripLeadingBlanks(); }, stripLeading);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->StripTrailingBlanks(); }, stripTrailing);
    for (const int id : {stripLeading, stripTrailing})
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    const int mergeBlankLines = wxWindow::NewControlId();
    const int removeBlankLines = wxWindow::NewControlId();
    edit->Append(mergeBlankLines, "Merge Blank Lines\tAlt+B");
    edit->Append(removeBlankLines, "Remove Blank Lines\tAlt+R");
    for (const int id : {mergeBlankLines, removeBlankLines}) {
        Bind(
            wxEVT_MENU,
            [this, id, mergeBlankLines](wxCommandEvent&) {
                if (!editor_->RemoveBlankLines(id == mergeBlankLines))
                    wxMessageBox("This operation can't be performed within a rectangular selection.", "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
            },
            id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    }
    edit->AppendSeparator();
    const int mergeDuplicates = wxWindow::NewControlId();
    const int removeDuplicates = wxWindow::NewControlId();
    edit->Append(mergeDuplicates, "Merge Duplicate Lines");
    edit->Append(removeDuplicates, "Remove Duplicate Lines");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MergeDuplicateLines(); }, mergeDuplicates);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->RemoveDuplicateLines(); }, removeDuplicates);
    for (const int id : {mergeDuplicates, removeDuplicates})
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) {
                using Scintilla::Message;
                event.Enable(editor_->Send(Message::LineFromPosition, editor_->Send(Message::GetSelectionEnd)) >
                             editor_->Send(Message::LineFromPosition, editor_->Send(Message::GetSelectionStart)));
            },
            id);
    const int padSpaces = wxWindow::NewControlId();
    edit->Append(padSpaces, "&Pad With Spaces\tAlt+P");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->PadWithSpaces(); }, padSpaces);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, padSpaces);
    const int joinLines = wxWindow::NewControlId();
    const int joinParagraphs = wxWindow::NewControlId();
    edit->Append(joinLines, "&Join Lines\tCtrl+J");
    edit->Append(joinParagraphs, "Join &Paragraphs\tCtrl+Shift+J");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->JoinLines(); }, joinLines);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->JoinParagraphs())
                wxMessageBox("This operation can't be performed within a rectangular selection.", "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        joinParagraphs);
    for (const int id : {joinLines, joinParagraphs})
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    struct Conversion {
        const char* label;
        bool toTabs;
        bool onlyIndent;
    };
    for (const auto& command : {Conversion{"Tabify Selection\tCtrl+Shift+T", true, false},
                                Conversion{"Untabify Selection\tCtrl+Shift+S", false, false},
                                Conversion{"Tabify Indent\tCtrl+Alt+T", true, true},
                                Conversion{"Untabify Indent\tCtrl+Alt+S", false, true}}) {
        const int id = wxWindow::NewControlId();
        edit->Append(id, command.label);
        Bind(wxEVT_MENU, [this, command](wxCommandEvent&) { ConvertTabs(command.toTabs, command.onlyIndent); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
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
    const int lineComment = wxWindow::NewControlId();
    auto* lineCommentItem = new wxMenuItem(edit, lineComment, "T&oggle Line Comment\tCtrl+/");
    lineCommentItem->AddExtraAccel(wxAcceleratorEntry(wxACCEL_CTRL, WXK_NUMPAD_DIVIDE, lineComment));
    lineCommentItem->AddExtraAccel(wxAcceleratorEntry(wxACCEL_CTRL, WXK_DIVIDE, lineComment));
    // wxMSW collects the item's extra accelerators when it is appended.
    edit->Append(lineCommentItem);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto* prefix = SyntaxFor(document_.Language()).lineComment;
            if (!prefix || editor_->Send(Scintilla::Message::GetReadOnly)) return;
            const auto& preferences = settings_ ? settings_->values : defaults_;
            if (!editor_->ToggleLineComments(prefix, (preferences.autoInsertMask & 256) != 0))
                wxMessageBox("This operation can't be performed within a rectangular selection.", "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        lineComment);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Enable(!editor_->Send(Scintilla::Message::GetReadOnly) &&
                         SyntaxFor(document_.Language()).lineComment != nullptr);
        },
        lineComment);
    edit->AppendSeparator();
    const int moveUp = wxWindow::NewControlId();
    const int moveDown = wxWindow::NewControlId();
    edit->Append(moveUp, "Move &Up\tAlt+Up");
    edit->Append(moveDown, "Move &Down\tAlt+Down");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MoveLines(true); }, moveUp);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MoveLines(false); }, moveDown);
    for (const int id : {moveUp, moveDown})
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetReadOnly)); }, id);
    const int compressWhitespace = wxWindow::NewControlId();
    edit->Append(compressWhitespace, "Compress &Whitespace\tAlt+W");
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->CompressWhitespace())
                wxMessageBox("This operation can't be performed within a rectangular selection.", "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        compressWhitespace);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, compressWhitespace);
    const int stripFirst = wxWindow::NewControlId();
    edit->Append(stripFirst, "Strip &First Character\tAlt+Z");
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->StripFirstCharacter())
                wxMessageBox("This operation can't be performed within a rectangular selection.", "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        stripFirst);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, stripFirst);
    const int stripLast = wxWindow::NewControlId();
    edit->Append(stripLast, "Strip &Last Character\tAlt+L");
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->StripLastCharacter())
                wxMessageBox("This operation can't be performed within a rectangular selection.", "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        stripLast);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, stripLast);
    Bind(wxEVT_CHAR_HOOK, [this, stripFirst, stripLast](wxKeyEvent& event) {
        const int key = event.GetKeyCode();
        if (!event.AltDown() || (key != 'Z' && key != 'L')) {
            event.Skip();
            return;
        }
        // Route Alt+L before the native Language-menu mnemonic and retain exact modifiers.
        if (!event.ControlDown() && !event.ShiftDown() && !event.MetaDown()) {
            wxCommandEvent command(wxEVT_MENU, key == 'Z' ? stripFirst : stripLast);
            GetEventHandler()->ProcessEvent(command);
        }
    });
}

void EditorFrame::AppendSearchCommands(wxMenu* edit) {
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
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFind(); }, wxID_FIND);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFind(true); }, wxID_REPLACE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNext(); }, wxID_FORWARD);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindPrevious(); }, wxID_BACKWARD);
}

void EditorFrame::AppendNavigationCommands(wxMenu* edit) {
    const int goToLine = wxWindow::NewControlId();
    edit->Append(goToLine, "&Go To Line...\tCtrl+G");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowGoToLineDialog(this, *editor_); }, goToLine);
    const int matchingBrace = wxWindow::NewControlId();
    edit->Append(matchingBrace, "Find Matching &Brace\tCtrl+B");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MatchBrace(); }, matchingBrace);
    const int selectMatchingBrace = wxWindow::NewControlId();
    edit->Append(selectMatchingBrace, "Select to Matching B&race\tCtrl+Shift+B");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MatchBrace(true); }, selectMatchingBrace);
    auto* bookmarks = new wxMenu;
    const int bookmarkToggle = wxWindow::NewControlId();
    const int bookmarkNext = wxWindow::NewControlId();
    const int bookmarkPrevious = wxWindow::NewControlId();
    const int bookmarkSelect = wxWindow::NewControlId();
    const int bookmarkClear = wxWindow::NewControlId();
    bookmarks->Append(bookmarkToggle, "&Toggle\tCtrl+F2");
    bookmarks->AppendSeparator();
    bookmarks->Append(bookmarkNext, "Goto &Next\tF2");
    bookmarks->Append(bookmarkPrevious, "Goto &Previous\tShift+F2");
    bookmarks->AppendSeparator();
    bookmarks->Append(bookmarkSelect, "&Select All\tAlt+F6");
    bookmarks->Append(bookmarkClear, "&Clear All\tAlt+F2");
    edit->AppendSubMenu(bookmarks, "Boo&kmarks");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->ToggleBookmark(); }, bookmarkToggle);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->GoToBookmark(true); }, bookmarkNext);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->GoToBookmark(false); }, bookmarkPrevious);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SelectAllBookmarks(); }, bookmarkSelect);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::MarkerDeleteAll, 0); }, bookmarkClear);
}

void EditorFrame::AppendLineEndingCommands(wxMenu* edit) {
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
}

void EditorFrame::AppendLineSelectionCommands(wxMenu* edit) {
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
}

wxMenu* EditorFrame::CreateLanguageMenu() {
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
    return languages;
}

wxMenu* EditorFrame::CreateViewMenu() {
    auto* view = new wxMenu;
    AppendIndentationOptions(view);
    view->AppendSeparator();
    AppendZoomCommands(view);
    view->AppendSeparator();
    AppendDisplayCommands(view);
    view->AppendSeparator();
    AppendFoldCommands(view);
    AppendSettingsCommands(view);
    return view;
}

void EditorFrame::AppendIndentationOptions(wxMenu* view) {
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
}

void EditorFrame::AppendZoomCommands(wxMenu* view) {
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
}

void EditorFrame::AppendDisplayCommands(wxMenu* view) {
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
    // One frame hook owns dispatch; the dialog retains its narrower callback.
    Bind(wxEVT_CHAR_HOOK, [this, whitespace](wxKeyEvent& event) {
        if (!HandleCommandKey(event) && !HandleWhitespaceKey(event, whitespace)) event.Skip();
    });
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetViewWS) != 0); }, whitespace);
}

void EditorFrame::AppendFoldCommands(wxMenu* view) {
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
}

void EditorFrame::AppendSettingsCommands(wxMenu* view) {
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
}

void EditorFrame::OnSearchKey(wxKeyEvent& event) {
    if (!HandleCommandKey(event)) event.Skip();
}

bool EditorFrame::HandleCommandKey(const wxKeyEvent& event) {
    if (HandleTabConversionKey(event)) return true;
    if (event.GetKeyCode() == 'T' && event.AltDown()) {
        if (!event.ControlDown() && !event.ShiftDown() && !event.MetaDown()) editor_->StripTrailingBlanks();
        return true;
    }
    return HandleSearchKey(event) || HandleNavigationKey(event) || HandleViewKey(event) || HandleDocumentKey(event);
}

void EditorFrame::ConvertTabs(bool toTabs, bool onlyIndent) {
    if (!(toTabs ? editor_->SpacesToTabs(onlyIndent) : editor_->TabsToSpaces(onlyIndent)))
        wxMessageBox("This operation can't be performed within a rectangular selection.", "wxnotepad",
                     wxOK | wxICON_WARNING, this);
}

bool EditorFrame::HandleTabConversionKey(const wxKeyEvent& event) {
    if ((event.GetKeyCode() != 'T' && event.GetKeyCode() != 'S') || !event.ControlDown() ||
        (!event.ShiftDown() && !event.AltDown()))
        return false;
    if (event.ShiftDown() != event.AltDown() && !event.MetaDown())
        ConvertTabs(event.GetKeyCode() == 'T', event.AltDown());
    // Consume extra modifiers before native menu lookup can drop them.
    return true;
}

bool EditorFrame::HandleSearchKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() == 'F' && event.ControlDown() && !event.AltDown() && !event.ShiftDown())
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
    else
        return false;
    return true;
}

bool EditorFrame::HandleNavigationKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() == 'B' && event.ControlDown() && !event.AltDown() && !event.MetaDown())
        editor_->MatchBrace(event.ShiftDown());
    else if (event.GetKeyCode() == WXK_F2 && !event.MetaDown()) {
        if (event.ControlDown() && !event.ShiftDown() && !event.AltDown())
            editor_->ToggleBookmark();
        else if (event.AltDown() && !event.ControlDown() && !event.ShiftDown())
            editor_->Send(Scintilla::Message::MarkerDeleteAll, 0);
        else if (!event.ControlDown() && !event.AltDown())
            editor_->GoToBookmark(!event.ShiftDown());
        // Consume unmatched modifiers so native menu lookup cannot drop them.
    } else if (event.GetKeyCode() == WXK_F6 && event.AltDown() && !event.ControlDown() && !event.ShiftDown() &&
               !event.MetaDown())
        editor_->SelectAllBookmarks();
    else
        return false;
    return true;
}

bool EditorFrame::HandleViewKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() == 'W' && event.ControlDown() && event.ShiftDown() && !event.AltDown() && !event.MetaDown())
        editor_->ToggleWordWrap();
    else if (event.GetKeyCode() == 'N' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
             !event.MetaDown())
        editor_->SetLineNumbersVisible(!editor_->LineNumbersVisible());
    else
        return false;
    return true;
}

bool EditorFrame::HandleWhitespaceKey(const wxKeyEvent& event, int commandId) {
    if ((event.GetKeyCode() != '8' && event.GetKeyCode() != '*') || !event.ControlDown() || !event.ShiftDown() ||
        event.AltDown() || event.MetaDown())
        return false;
    wxCommandEvent command(wxEVT_MENU, commandId);
    GetEventHandler()->ProcessEvent(command);
    return true;
}

bool EditorFrame::HandleDocumentKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() != WXK_F6) return false;
    // GTK Wayland can consume Ctrl while matching F6 accelerators. Route
    // the original exact modifiers before native accelerator matching.
    if (!event.HasAnyModifiers()) {
        wxCommandEvent command(wxEVT_MENU, wxID_SAVEAS);
        GetEventHandler()->ProcessEvent(command);
    }
    return true;
}
