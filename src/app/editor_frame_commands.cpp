#include "app/dialogs/auto_completion_settings_dialog.h"
#include <utility>
#include "app/editor_frame.h"
#include "app/localization.h"
#include "editor/display_units.h"
#include "syntax/syntax_scheme.h"
#include "app/dialogs/long_line_settings_dialog.h"
#include "app/dialogs/column_wrap_dialog.h"
#include "editor/editor_control.h"
#include "editor/platform/clipboard.h"
#include "editor/platform/native_encoding.h"
#include "app/dialogs/document_dialogs.h"
#include "editor/platform/custom_case.h"
#include "app/dialogs/go_to_line_dialog.h"
#include "app/dialogs/enclose_selection_dialog.h"
#include "app/dialogs/insert_tag_dialog.h"
#include "app/dialogs/align_lines_dialog.h"
#include "app/dialogs/sort_lines_dialog.h"
#include "app/dialogs/tab_settings_dialog.h"
#include "settings/settings_store.h"
#include <wx/accel.h>
#include <wx/translation.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/strconv.h>
#include <cstdlib>
#include "app/dialogs/modify_lines_dialog.h"

namespace {
void ShowZoomLevelDialog(wxWindow* parent, EditorControl& editor) {
    using Scintilla::Message;
    // Original Dialogs.cpp::ZoomLevelDlgProc and InitZoomLevelComboBox.
    wxDialog dialog(parent, wxID_ANY, _("Zoom Level"));
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY,
                                   wxString::Format(_("Zoom level (%d%% to %d%%):"), Scintilla::MinZoomLevel,
                                                    Scintilla::MaxZoomLevel)),
                  0, wxALL, dialog.FromDIP(10));
    auto* input =
        new wxComboBox(&dialog, wxID_ANY, wxString::Format("%d%%", static_cast<int>(editor.Send(Message::GetZoom))));
    input->SetName("Zoom level");
    AppLocalization::SetAccessibleName(*input, _("Zoom level"));
    input->SetMaxLength(8);
    for (int level : {500, 450, 400, 350, 300, 250, 200, 175, 150, 125, 100, 75, 50, 25})
        input->Append(wxString::Format("%d%%", level));
    contents->Add(input, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL | wxAPPLY), 0, wxEXPAND | wxALL,
                  dialog.FromDIP(10));
    const auto apply = [input, &editor, &dialog](wxCommandEvent& event) {
        const auto value = input->GetValue().ToStdString(wxConvUTF8);
        char* end = nullptr;
        const long level = std::strtol(value.c_str(), &end, 10);
        if (end != value.c_str() && level >= Scintilla::MinZoomLevel && level <= Scintilla::MaxZoomLevel)
            editor.Send(Message::SetZoom, level);
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
    menus->Append(CreateFileMenu(), _("&File"));
    menus->Append(CreateEditMenu(), _("&Edit"));
    menus->Append(CreateSearchMenu(), _("&Search"));
    menus->Append(CreateViewMenu(), _("&View"));
    menus->Append(CreateLanguageMenu(), _("Sche&me"));
    menus->Append(CreateSettingsMenu(), _("Sett&ings"));
    SetMenuBar(menus);
}

void EditorFrame::AppendReloadEncodingCommands(wxMenu* reload) {
    reloadEncodingId_ = wxWindow::NewControlId();
    reloadUtf8Id_ = wxWindow::NewControlId();
    reload->Append(reloadUtf8Id_, _("As &UTF-8\tShift+F8"));
    reload->AppendSeparator();
    reload->Append(reloadEncodingId_, _("With &Encoding...\tF8"));
    for (const int id : {reloadEncodingId_, reloadUtf8Id_})
        Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(!document_.Path().empty()); }, id);
    for (const int id : {reloadEncodingId_, reloadUtf8Id_})
        Bind(
            wxEVT_MENU,
            [this, id](wxCommandEvent&) {
                const auto result = id == reloadEncodingId_ ? ReloadWithEncoding() : ReloadAsUtf8();
                if (result.status == OpenStatus::Failed)
                    wxMessageBox(result.error, _("Could not reload file"), wxOK | wxICON_ERROR, this);
            },
            id);
}

void EditorFrame::AppendEncodingCommands(wxMenu* file) {
    auto* encoding = new wxMenu;
    struct Command {
        const char* label;
        EncodingChoice choice;
    };
    // Original Notepad4.rc direct encoding commands, in their original order.
    if (const auto native = NativeEncodingChoice()) {
        const int id = wxWindow::NewControlId();
        encoding->AppendCheckItem(id, "&ANSI");
        Bind(wxEVT_MENU, [this, choice = *native](wxCommandEvent&) { SelectEncoding(choice); }, id);
        Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(document_.IsDefaultEncoding()); }, id);
    }
    for (const auto& command : {Command{"UTF-&8", EncodingChoice(FileEncoding::Utf8)},
                                Command{"UTF-8 B&OM", EncodingChoice(FileEncoding::Utf8, true)},
                                Command{"UTF-16&LE BOM", EncodingChoice(FileEncoding::Utf16Le, true)},
                                Command{"UTF-16&BE BOM", EncodingChoice(FileEncoding::Utf16Be, true)}}) {
        const int id = wxWindow::NewControlId();
        encoding->AppendCheckItem(id, command.label);
        Bind(wxEVT_MENU, [this, command](wxCommandEvent&) { SelectEncoding(command.choice); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, command](wxUpdateUIEvent& event) { event.Check(document_.CurrentEncoding() == command.choice); },
            id);
    }
    const int more = wxWindow::NewControlId();
    encoding->Append(more, _("&More...\tF9"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (const auto choice = ChooseCurrentEncoding(this, document_.CurrentEncoding())) SelectEncoding(*choice);
        },
        more);
    const int defaults = wxWindow::NewControlId();
    encoding->AppendSeparator();
    encoding->Append(defaults, _("&Default..."));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& options = settings_ ? settings_->values.encoding : defaults_.encoding;
            ChooseDefaultEncoding(this, options);
            document_.SetEncodingOptions(options);
        },
        defaults);
    file->AppendSeparator();
    file->AppendSubMenu(encoding, _("&Encoding"));
}

wxMenu* EditorFrame::CreateFileMenu() {
    auto* file = new wxMenu;
    file->Append(wxID_NEW, _("&New\tCtrl+N"));
    const int newWindow = wxWindow::NewControlId();
    file->Append(newWindow, _("New &Window\tAlt+N"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!NewWindow())
                wxMessageBox(_("Could not start another wxnotepad process."), _("Could not open new window"),
                             wxOK | wxICON_ERROR, this);
        },
        newWindow);
    const int restart = wxWindow::NewControlId();
    file->Append(restart, _("&Restart"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Restart(); }, restart);
    file->Append(wxID_OPEN, _("&Open...\tCtrl+O"));
    const int openGbk = wxWindow::NewControlId();
    file->Append(openGbk, _("Open As &GBK..."));
    const int openUtf16 = wxWindow::NewControlId();
    file->Append(openUtf16, _("Open As UTF-16LE..."));
    for (const auto& [label, encoding] : {std::pair{wxTRANSLATE("Open As Windows-1252..."), FileEncoding::Windows1252},
                                          std::pair{wxTRANSLATE("Open As DOS-437..."), FileEncoding::Dos437}}) {
        const int id = wxWindow::NewControlId();
        file->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, encoding](wxCommandEvent&) {
                const auto result = OpenAs(encoding);
                if (result.status == OpenStatus::Failed)
                    wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, this);
            },
            id);
    }
    file->Append(wxID_SAVE, _("&Save\tCtrl+S"));
    file->Append(wxID_SAVEAS, _("Save &As...\tF6"));
    saveCopyId_ = wxWindow::NewControlId();
    file->Append(saveCopyId_, _("Save Cop&y...\tCtrl+F6"));
    const int saveOriginalTime = wxWindow::NewControlId();
    file->Append(saveOriginalTime, _("Save with Original Time"));
    const int updateTime = wxWindow::NewControlId();
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = Save({}, SaveTimeMode::Original);
            if (result.status == SaveStatus::Failed)
                wxMessageBox(result.error, _("Could not save file"), wxOK | wxICON_ERROR, this);
        },
        saveOriginalTime);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(document_.IsModified()); }, saveOriginalTime);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { UpdateFileTime(); }, updateTime);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(!document_.Path().empty()); }, updateTime);
    file->AppendSeparator();
    auto* mode = new wxMenu;
    const int readOnly = wxWindow::NewControlId();
    mode->AppendCheckItem(readOnly, _("&Read Only Mode\tF10"));
    file->AppendSubMenu(mode, _("File &Mode"));
    revertId_ = wxWindow::NewControlId();
    file->Append(revertId_, _("&Revert\tF5"));
    auto* reload = new wxMenu;
    AppendReloadEncodingCommands(reload);
    file->AppendSubMenu(reload, _("&Reload"));
    AppendEncodingCommands(file);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = Revert();
            if (result.status == OpenStatus::Failed)
                wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, this);
        },
        revertId_);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(!document_.Path().empty()); }, revertId_);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ToggleReadOnlyMode(); }, readOnly);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetReadOnly) != 0); }, readOnly);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = OpenAs(FileEncoding::Gbk);
            if (result.status == OpenStatus::Failed)
                wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, this);
        },
        openGbk);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { NewDocument(); }, wxID_NEW);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = Open();
            if (result.status == OpenStatus::Failed)
                wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, this);
        },
        wxID_OPEN);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = OpenAs(FileEncoding::Utf16Le);
            if (result.status == OpenStatus::Failed)
                wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, this);
        },
        openUtf16);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = Save();
            if (result.status == SaveStatus::Failed)
                wxMessageBox(result.error, _("Could not save file"), wxOK | wxICON_ERROR, this);
        },
        wxID_SAVE);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(document_.IsModified()); }, wxID_SAVE);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = SaveAs();
            if (result.status == SaveStatus::Failed)
                wxMessageBox(result.error, _("Could not save file"), wxOK | wxICON_ERROR, this);
        },
        wxID_SAVEAS);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto result = SaveCopy();
            if (result.status == SaveStatus::Failed)
                wxMessageBox(result.error, _("Could not save file"), wxOK | wxICON_ERROR, this);
        },
        saveCopyId_);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, saveCopyId_);
    file->AppendSeparator();
    auto* endings = new wxMenu;
    AppendLineEndingCommands(endings);
    file->AppendSubMenu(endings, _("Line Endin&gs"));
    file->AppendSeparator();
    file->Append(updateTime, _("Update File Time"));
    AppendRecentFilesCommand(file);
    return file;
}

void EditorFrame::AppendRecentFilesCommand(wxMenu* file) {
    recentFilesId_ = wxWindow::NewControlId();
    file->Append(recentFilesId_, _("Recent (&History)...\tAlt+H"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowRecentFiles(); }, recentFilesId_);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Enable(!(settings_ ? settings_->values : defaults_).recentFiles.empty());
        },
        recentFilesId_);
}

wxMenu* EditorFrame::CreateEditMenu() {
    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO, _("&Undo\tCtrl+Z"));
    edit->Append(wxID_REDO, _("&Redo\tCtrl+Y"));
    edit->AppendSeparator();
    edit->Append(wxID_CUT, _("Cu&t\tCtrl+X"));
    edit->Append(wxID_COPY, _("&Copy\tCtrl+C"));
    edit->Append(wxID_PASTE, _("&Paste\tCtrl+V"));
    const int swap = wxWindow::NewControlId();
    edit->Append(swap, _("S&wap\tCtrl+K"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SwapClipboard(document_.IsDefaultEncoding()); }, swap);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty) ||
                         editor_->Send(Scintilla::Message::CanPaste));
        },
        swap);
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
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); }, wxID_CUT);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanPaste)); },
        wxID_PASTE);
    const int clearClipboard = wxWindow::NewControlId();
    edit->Append(clearClipboard, _("Clear Clip&board"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { EditorClipboard::Clear(*this); }, clearClipboard);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(EditorClipboard::HasData(*this)); },
        clearClipboard);
    auto* clipboard = new wxMenu;
    AppendFileNameCopyCommands(clipboard);
    clipboard->AppendSeparator();
    const int copyAll = wxWindow::NewControlId();
    clipboard->Append(copyAll, _("Copy &All\tAlt+A"));
    const int copyAdd = wxWindow::NewControlId();
    clipboard->Append(copyAdd, _("Copy A&dd\tCtrl+E"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            editor_->Send(Scintilla::Message::CopyRange, 0, editor_->Send(Scintilla::Message::GetLength));
        },
        copyAll);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); }, copyAll);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->CopyAdd())
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        copyAdd);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
        copyAdd);
    edit->AppendSubMenu(clipboard, _("Cop&y to Clipboard"));
    edit->AppendSeparator();
    AppendEditingCommands(edit);
    return edit;
}

void EditorFrame::AppendEditingCommands(wxMenu* edit) {
    auto* selection = new wxMenu;
    AppendSelectionCommands(selection);
    edit->AppendSubMenu(selection, _("&Selection"));
    AppendEnclosureCommands(edit);
    auto* lines = new wxMenu;
    AppendLineCommands(lines);
    edit->AppendSubMenu(lines, _("&Lines"));
    AppendConversionCommands(edit);
    AppendDateTimeCommands(edit);
    auto* special = new wxMenu;
    AppendSpecialCommands(special);
    edit->AppendSubMenu(special, _("Spec&ial"));
}

void EditorFrame::AppendConversionCommands(wxMenu* edit) {
    auto* conversion = new wxMenu;
    for (const auto& [label, mapping] :
         {std::pair{wxTRANSLATE("&Upper Case\tCtrl+Shift+U"), CustomCaseCommand::Upper},
          std::pair{wxTRANSLATE("&Lower Case\tCtrl+U"), CustomCaseCommand::Lower},
          std::pair{wxTRANSLATE("In&vert Case\tCtrl+Alt+U"), CustomCaseCommand::Invert},
          std::pair{wxTRANSLATE("Title &Case\tCtrl+Alt+I"), CustomCaseCommand::Title},
          std::pair{wxTRANSLATE("&Sentence Case\tCtrl+Alt+O"), CustomCaseCommand::Sentence}}) {
        const int id = wxWindow::NewControlId();
        conversion->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, mapping](wxCommandEvent&) { editor_->Send(Scintilla::Message::CustomCaseMapping, mapping); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    conversion->AppendSeparator();
    struct Conversion {
        const char* label;
        bool toTabs;
        bool onlyIndent;
    };
    for (const auto& command : {Conversion{wxTRANSLATE("Tabify Selection\tCtrl+Shift+T"), true, false},
                                Conversion{wxTRANSLATE("Untabify Selection\tCtrl+Shift+S"), false, false},
                                Conversion{wxTRANSLATE("Tabify Indent\tCtrl+Alt+T"), true, true},
                                Conversion{wxTRANSLATE("Untabify Indent\tCtrl+Alt+S"), false, true}}) {
        const int id = wxWindow::NewControlId();
        conversion->Append(id, wxGetTranslation(command.label));
        Bind(wxEVT_MENU, [this, command](wxCommandEvent&) { ConvertTabs(command.toTabs, command.onlyIndent); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    conversion->AppendSeparator();
    for (const auto& [label, radix] :
         {std::pair{wxTRANSLATE("To &Hexadecimal\tCtrl+Alt+H"), 16u},
          std::pair{wxTRANSLATE("To &Decimal\tCtrl+Alt+D"), 10u}, std::pair{wxTRANSLATE("To &Binary\tCtrl+Alt+B"), 2u},
          std::pair{wxTRANSLATE("To &Octet"), 8u}}) {
        const int id = wxWindow::NewControlId();
        conversion->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, radix](wxCommandEvent&) {
                if (!editor_->ConvertNumberRadix(radix))
                    wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    conversion->AppendSeparator();
    // Delivered context conversions remain available in Convert.
    for (const auto& [label, mapping] :
         {std::pair{wxTRANSLATE("Escape HT&ML/XML Chars\tAlt+Shift+X"), CustomCaseCommand::EscapeHtml},
          std::pair{wxTRANSLATE("Unescape HTML/&XML Chars\tAlt+Shift+H"), CustomCaseCommand::UnescapeHtml}}) {
        const int id = wxWindow::NewControlId();
        conversion->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, mapping](wxCommandEvent&) { editor_->Send(Scintilla::Message::CustomCaseMapping, mapping); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    conversion->AppendSeparator();
    for (const auto& [label, command] : {std::pair{wxTRANSLATE("URL &Encode\tCtrl+Shift+E"), UrlConversion::Encode},
                                         std::pair{wxTRANSLATE("URL Component Encode"), UrlConversion::ComponentEncode},
                                         std::pair{wxTRANSLATE("URL &Decode\tCtrl+Shift+R"), UrlConversion::Decode}}) {
        const int id = wxWindow::NewControlId();
        conversion->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, command](wxCommandEvent&) {
                if (!editor_->ConvertUrl(command))
                    wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    edit->AppendSubMenu(conversion, _("Con&vert"));
}

void EditorFrame::AppendEnclosureCommands(wxMenu* edit) {
    auto* enclosure = new wxMenu;
    const int customEnclosure = wxWindow::NewControlId();
    enclosure->Append(customEnclosure, _("&With...\tAlt+Q"));
    const int encloseTag = wxWindow::NewControlId();
    enclosure->Append(encloseTag, _("&HTML/XML Tag...\tAlt+X"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowInsertTagDialog(this, *editor_); }, encloseTag);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, encloseTag);
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
                    wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
            },
            id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    };
    addEnclosure(_("Triple &Single Quotes\tCtrl+3"), "'''");
    addEnclosure(_("Triple &Double Quotes\tCtrl+6"), "\"\"\"");
    addEnclosure(_("Triple &Backticks\tCtrl+9"), "```");
    edit->AppendSubMenu(enclosure, _("&Enclose Selection"));
}

void EditorFrame::AppendCommentCommands(wxMenu* edit) {
    const auto comments = [this] {
        return SyntaxCommentsFor(document_.Language(),
                                 static_cast<int>(editor_->Send(Scintilla::Message::GetStyleIndexAt,
                                                                editor_->Send(Scintilla::Message::GetCurrentPos))));
    };
    const int lineComment = wxWindow::NewControlId();
    auto* lineCommentItem = new wxMenuItem(edit, lineComment, _("T&oggle Line Comment\tCtrl+/"));
    lineCommentItem->AddExtraAccel(wxAcceleratorEntry(wxACCEL_CTRL, WXK_NUMPAD_DIVIDE, lineComment));
    lineCommentItem->AddExtraAccel(wxAcceleratorEntry(wxACCEL_CTRL, WXK_DIVIDE, lineComment));
    // wxMSW collects the item's extra accelerators when it is appended.
    edit->Append(lineCommentItem);
    Bind(
        wxEVT_MENU,
        [this, comments](wxCommandEvent&) {
            const auto* prefix = comments().line;
            if (!prefix) return;
            const auto& preferences = settings_ ? settings_->values : defaults_;
            if (!editor_->ToggleLineComments(prefix,
                                             (preferences.autoInsertMask & AutoInsertMask_SpaceAfterComment) != 0))
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        lineComment);
    Bind(
        wxEVT_UPDATE_UI, [comments](wxUpdateUIEvent& event) { event.Enable(comments().line != nullptr); }, lineComment);
    const int blockComment = wxWindow::NewControlId();
    edit->Append(blockComment, _("Toggle Block &Comment\tCtrl+Q"));
    Bind(
        wxEVT_MENU,
        [this, comments](wxCommandEvent&) {
            const auto& syntax = SyntaxFor(document_.Language());
            const auto rules = comments();
            if (rules.open && !editor_->ToggleBlockComments(rules.open, rules.close, syntax.commentStyleLast))
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        blockComment);
    Bind(
        wxEVT_UPDATE_UI, [comments](wxUpdateUIEvent& event) { event.Enable(comments().open != nullptr); },
        blockComment);
}

void EditorFrame::AppendSelectionCommands(wxMenu* edit) {
    const int duplicateSelection = wxWindow::NewControlId();
    edit->Append(duplicateSelection, _("&Duplicate\tAlt+D"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::SelectionDuplicate); },
        duplicateSelection);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, duplicateSelection);
    edit->AppendSeparator();
    AppendCommentCommands(edit);
    const int indent = wxWindow::NewControlId();
    edit->Append(indent, _("&Indent"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->IndentLines(true); }, indent);
    const int unindent = wxWindow::NewControlId();
    edit->Append(unindent, _("&Unindent"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->IndentLines(false); }, unindent);
    for (const int id : {indent, unindent})
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    edit->AppendSeparator();
    const int stripLeading = wxWindow::NewControlId();
    const int stripTrailing = wxWindow::NewControlId();
    edit->Append(stripTrailing, _("Strip &Trailing Blanks\tAlt+T"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->StripLeadingBlanks(); }, stripLeading);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->StripTrailingBlanks(); }, stripTrailing);
    for (const int id : {stripLeading, stripTrailing})
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    const int stripFirst = wxWindow::NewControlId();
    edit->Append(stripFirst, _("Strip &First Character\tAlt+Z"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->StripFirstCharacter())
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        stripFirst);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, stripFirst);
    const int stripLast = wxWindow::NewControlId();
    edit->Append(stripLast, _("Strip &Last Character\tAlt+L"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->StripLastCharacter())
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
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
        // Retain the exact modifiers for the original Alt+Z / Alt+L shortcuts.
        if (!event.ControlDown() && !event.ShiftDown() && !event.MetaDown()) {
            wxCommandEvent command(wxEVT_MENU, key == 'Z' ? stripFirst : stripLast);
            GetEventHandler()->ProcessEvent(command);
        }
    });
    edit->Append(stripLeading, _("Strip Leading Blan&ks"));
    edit->AppendSeparator();
    const int mergeBlankLines = wxWindow::NewControlId();
    const int removeBlankLines = wxWindow::NewControlId();
    edit->Append(mergeBlankLines, _("Merge Blank Lines\tAlt+B"));
    edit->Append(removeBlankLines, _("Remove Blank Lines\tAlt+R"));
    for (const int id : {mergeBlankLines, removeBlankLines}) {
        Bind(
            wxEVT_MENU,
            [this, id, mergeBlankLines](wxCommandEvent&) {
                if (!editor_->RemoveBlankLines(id == mergeBlankLines))
                    wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
            },
            id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    }
    const int mergeDuplicates = wxWindow::NewControlId();
    const int removeDuplicates = wxWindow::NewControlId();
    edit->Append(mergeDuplicates, _("Merge Duplicate Lines"));
    edit->Append(removeDuplicates, _("Remove Duplicate Lines"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MergeDuplicateLines(); }, mergeDuplicates);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->RemoveDuplicateLines(); }, removeDuplicates);
    for (const int id : {mergeDuplicates, removeDuplicates})
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) {
                using enum Scintilla::Message;
                event.Enable(editor_->Send(LineFromPosition, editor_->Send(GetSelectionEnd)) >
                             editor_->Send(LineFromPosition, editor_->Send(GetSelectionStart)));
            },
            id);
    const int padSpaces = wxWindow::NewControlId();
    edit->Append(padSpaces, _("&Pad With Spaces\tAlt+P"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->PadWithSpaces(); }, padSpaces);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, padSpaces);
    const int compressWhitespace = wxWindow::NewControlId();
    edit->Append(compressWhitespace, _("Compress &Whitespace\tAlt+W"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->CompressWhitespace())
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        compressWhitespace);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, compressWhitespace);
}

void EditorFrame::AppendLineCommands(wxMenu* edit) {
    const int moveUp = wxWindow::NewControlId();
    const int moveDown = wxWindow::NewControlId();
    edit->Append(moveUp, _("Move &Up\tAlt+Up"));
    edit->Append(moveDown, _("Move &Down\tAlt+Down"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MoveLines(true); }, moveUp);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MoveLines(false); }, moveDown);
    for (const int id : {moveUp, moveDown})
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    const int duplicateLine = wxWindow::NewControlId();
    const int transposeLines = wxWindow::NewControlId();
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::LineDuplicate); }, duplicateLine);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::LineTranspose); }, transposeLines);
    for (const int id : {duplicateLine, transposeLines})
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    edit->Append(transposeLines, _("T&ranspose\tAlt+S"));
    edit->AppendSeparator();
    const int sortLines = wxWindow::NewControlId();
    edit->Append(sortLines, _("S&ort Lines...\tAlt+O"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (ShowSortLinesDialog(this, sortOptions_, editor_->Send(Scintilla::Message::SelectionIsRectangle)))
                editor_->SortLines(sortOptions_);
            editor_->SetFocus();
        },
        sortLines);
    for (const int id : {sortLines})
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) {
                using enum Scintilla::Message;
                event.Enable(editor_->Send(LineFromPosition, editor_->Send(GetSelectionEnd)) >
                             editor_->Send(LineFromPosition, editor_->Send(GetSelectionStart)));
            },
            id);
    const int modifyLines = wxWindow::NewControlId();
    edit->Append(modifyLines, _("&Modify Lines...\tAlt+M"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) { ShowModifyLinesDialog(this, *editor_, linePrefix_, lineSuffix_, skipEmptyLines_); },
        modifyLines);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, modifyLines);
    const int alignLines = wxWindow::NewControlId();
    edit->Append(alignLines, _("Alig&n Lines...\tAlt+J"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (ShowAlignLinesDialog(this, alignmentMode_)) {
                const auto result = editor_->AlignLines(alignmentMode_);
                if (result == AlignmentResult::RectangularSelection)
                    wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
                else if (result == AlignmentResult::LinesTooLong)
                    wxMessageBox(_("This operation can't be performed (lines too long)."), "wxnotepad",
                                 wxOK | wxICON_INFORMATION, this);
            }
            editor_->SetFocus();
        },
        alignLines);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, alignLines);
    edit->AppendSeparator();
    edit->Append(duplicateLine, _("&Duplicate Line\tCtrl+D"));
    const int cutLine = wxWindow::NewControlId();
    const int copyLine = wxWindow::NewControlId();
    const int deleteLine = wxWindow::NewControlId();
    edit->Append(cutLine, _("Cu&t Line\tCtrl+Shift+X"));
    edit->Append(copyLine, _("&Copy Line\tCtrl+Shift+C"));
    edit->Append(deleteLine, _("D&elete Line\tCtrl+Shift+D"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->CutLine(); }, cutLine);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->CopyLine(); }, copyLine);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->DeleteLine(); }, deleteLine);
    for (const int id : {cutLine, copyLine, deleteLine})
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    edit->AppendSeparator();
    AppendLineWrappingCommands(edit);
    Bind(wxEVT_CHAR_HOOK, [this, modifyLines, transposeLines](wxKeyEvent& event) {
        const int key = event.GetKeyCode();
        if (!event.AltDown() || (key != 'M' && key != 'S')) {
            event.Skip();
            return;
        }
        // Original command accelerators take priority over Scheme/Search mnemonics.
        if (!event.ControlDown() && !event.ShiftDown() && !event.MetaDown()) {
            wxCommandEvent command(wxEVT_MENU, key == 'M' ? modifyLines : transposeLines);
            GetEventHandler()->ProcessEvent(command);
        }
    });
}

void EditorFrame::AppendLineWrappingCommands(wxMenu* edit) {
    const int joinLines = wxWindow::NewControlId();
    const int splitLines = wxWindow::NewControlId();
    const int columnWrap = wxWindow::NewControlId();
    edit->Append(joinLines, _("&Join Lines\tCtrl+J"));
    edit->Append(splitLines, _("&Split Lines\tCtrl+I"));
    edit->Append(columnWrap, _("Column &Wrap..."));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const int boundary = static_cast<int>(editor_->Send(Scintilla::Message::GetEdgeColumn));
            if (ShowColumnWrapDialog(this, wrapColumn_, boundary) && !editor_->WrapToColumn(wrapColumn_))
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
            editor_->SetFocus();
        },
        columnWrap);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SplitLines(); }, splitLines);
    const int joinParagraphs = wxWindow::NewControlId();
    edit->Append(joinParagraphs, _("Join &Paragraphs\tCtrl+Shift+J"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->JoinLines(); }, joinLines);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->JoinParagraphs())
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        joinParagraphs);
    for (const int id : {splitLines, columnWrap, joinLines, joinParagraphs})
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
}

void EditorFrame::AppendSearchCommands(wxMenu* edit) {
    edit->Append(wxID_FIND, _("&Find...\tCtrl+F"));
    edit->Append(wxID_FORWARD, _("Find &Next\tF3"));
    edit->Append(wxID_BACKWARD, _("Find &Previous\tShift+F3"));
    edit->Append(wxID_REPLACE, _("R&eplace...\tCtrl+H"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFind(); }, wxID_FIND);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFind(true); }, wxID_REPLACE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNext(); }, wxID_FORWARD);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindPrevious(); }, wxID_BACKWARD);
}

void EditorFrame::AppendNavigationCommands(wxMenu* edit) {
    edit->AppendSeparator();
    const int matchingBrace = wxWindow::NewControlId();
    edit->Append(matchingBrace, _("Find Matching &Brace\tCtrl+B"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MatchBrace(); }, matchingBrace);
    const int selectMatchingBrace = wxWindow::NewControlId();
    edit->Append(selectMatchingBrace, _("Select to Matching B&race\tCtrl+Shift+B"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MatchBrace(true); }, selectMatchingBrace);
    const int selectBlock = wxWindow::NewControlId();
    edit->Append(selectBlock, _("Select Lines in &Current Block\tAlt+Shift+]"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SelectCurrentBlockLines(); }, selectBlock);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); },
        selectBlock);
    edit->AppendSeparator();
    const int selectNext = wxWindow::NewControlId();
    const int selectPrevious = wxWindow::NewControlId();
    edit->Append(selectNext, _("Select to Ne&xt\tCtrl+Alt+F2"));
    edit->Append(selectPrevious, _("Select to Previou&s\tCtrl+Alt+Shift+F2"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Search(true, true); }, selectNext);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Search(false, true); }, selectPrevious);
    edit->AppendSeparator();
    auto* bookmarks = new wxMenu;
    const int bookmarkToggle = wxWindow::NewControlId();
    const int bookmarkNext = wxWindow::NewControlId();
    const int bookmarkPrevious = wxWindow::NewControlId();
    const int bookmarkSelect = wxWindow::NewControlId();
    const int bookmarkClear = wxWindow::NewControlId();
    bookmarks->Append(bookmarkToggle, _("&Toggle\tCtrl+F2"));
    bookmarks->AppendSeparator();
    bookmarks->Append(bookmarkNext, _("Goto &Next\tF2"));
    bookmarks->Append(bookmarkPrevious, _("Goto &Previous\tShift+F2"));
    bookmarks->AppendSeparator();
    bookmarks->Append(bookmarkSelect, _("&Select All\tAlt+F6"));
    bookmarks->Append(bookmarkClear, _("&Clear All\tAlt+F2"));
    edit->AppendSubMenu(bookmarks, _("Boo&kmarks"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->ToggleBookmark(); }, bookmarkToggle);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->GoToBookmark(true); }, bookmarkNext);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->GoToBookmark(false); }, bookmarkPrevious);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SelectAllBookmarks(); }, bookmarkSelect);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::MarkerDeleteAll, 0); }, bookmarkClear);
    auto* goTo = new wxMenu;
    const int goToLine = wxWindow::NewControlId();
    goTo->Append(goToLine, _("&Goto Line...\tCtrl+G"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowGoToLineDialog(this, *editor_); }, goToLine);
    goTo->AppendSeparator();
    const auto addBlock = [this, goTo](const wxString& label, BlockNavigation command) {
        const int id = wxWindow::NewControlId();
        goTo->Append(id, label);
        Bind(wxEVT_MENU, [this, command](wxCommandEvent&) { editor_->GoToBlock(command); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); }, id);
    };
    addBlock(_("Goto Blo&ck Start\tAlt+["), BlockNavigation::Start);
    addBlock(_("Goto Bloc&k End\tAlt+]"), BlockNavigation::End);
    addBlock(_("Goto &Previous Block\tAlt+,"), BlockNavigation::Previous);
    addBlock(_("Goto &Next Block\tAlt+."), BlockNavigation::Next);
    addBlock(_("Goto P&revious Sibling Block\tCtrl+Alt+,"), BlockNavigation::PreviousSibling);
    addBlock(_("Goto N&ext Sibling Block\tCtrl+Alt+."), BlockNavigation::NextSibling);
    goTo->AppendSeparator();
    for (const auto& [label, start] : {std::pair{wxTRANSLATE("Goto &Selection Start\tCtrl+Shift+,"), true},
                                       std::pair{wxTRANSLATE("Goto Selec&tion End\tCtrl+Shift+."), false}}) {
        const int id = wxWindow::NewControlId();
        goTo->Append(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, start](wxCommandEvent&) { editor_->FocusSelectionBoundary(start); }, id);
    }
    edit->AppendSubMenu(goTo, _("&Goto"));
}

void EditorFrame::AppendLineEndingCommands(wxMenu* edit) {
    const auto addEol = [this, edit](const wxString& label, Scintilla::EndOfLine mode) {
        const int id = wxWindow::NewControlId();
        // wxGTK suppresses activation of an already checked radio item. A
        // conversion must run again after Undo restores mixed line endings.
        edit->AppendCheckItem(id, label);
        Bind(
            wxEVT_MENU,
            [this, mode](wxCommandEvent&) {
                editor_->Send(Scintilla::Message::ConvertEOLs, static_cast<uintptr_t>(std::to_underlying(mode)));
                editor_->Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(std::to_underlying(mode)));
                UpdateDocumentState();
                UpdateWindowUI(wxUPDATE_UI_RECURSE);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, mode](wxUpdateUIEvent& event) {
                event.Check(editor_->Send(Scintilla::Message::GetEOLMode) ==
                            static_cast<intptr_t>(std::to_underlying(mode)));
            },
            id);
    };
    addEol(_("&Windows (CR+LF)"), Scintilla::EndOfLine::CrLf);
    addEol(_("&Unix/macOS (LF)"), Scintilla::EndOfLine::Lf);
    addEol(_("Classic &Mac OS (CR)"), Scintilla::EndOfLine::Cr);
    edit->AppendSeparator();
    const int lineEndingOptions = wxWindow::NewControlId();
    edit->Append(lineEndingOptions, _("&Default..."));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowLineEndingOptions(); }, lineEndingOptions);
}

void EditorFrame::AppendLineSelectionCommands(wxMenu* edit) {
    using enum LineSelectionMode;
    auto* lineSelection = new wxMenu;
    for (const auto& [label, mode] :
         {std::pair{wxTRANSLATE("&None"), None}, std::pair{wxTRANSLATE("&Visual Studio"), VisualStudio},
          std::pair{wxTRANSLATE("&Old Visual Studio"), OldVisualStudio}, std::pair{wxTRANSLATE("&Normal"), Normal}}) {
        const int id = wxWindow::NewControlId();
        lineSelection->AppendRadioItem(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, mode](wxCommandEvent&) { editor_->SetLineSelectionMode(mode); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, mode](wxUpdateUIEvent& event) { event.Check(editor_->GetLineSelectionMode() == mode); }, id);
    }
    edit->AppendSubMenu(lineSelection, _("Line Selection &Mode"));
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
    automaticLanguageId_ = addLanguage(_("&Automatic"), std::nullopt);
    for (const auto& definition : SyntaxCatalog())
        addLanguage("&" + LanguageName(definition.language), definition.language);
    return languages;
}

wxMenu* EditorFrame::CreateViewMenu() {
    auto* view = new wxMenu;
    AppendCaretCommands(view);
    view->AppendSeparator();
    AppendDisplayCommands(view);
    view->AppendSeparator();
    const int matchBraces = wxWindow::NewControlId();
    view->AppendCheckItem(matchBraces, _("Visual &Brace Matching\tCtrl+Shift+V"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetVisualBraceMatching(!editor_->VisualBraceMatching()); },
        matchBraces);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->VisualBraceMatching()); }, matchBraces);
    AppendCurrentLineCommands(view);
    auto* occurrences = new wxMenu;
    for (const auto& [label, bit] :
         {std::pair{wxTRANSLATE("&Off"), 1}, std::pair{wxTRANSLATE("Match &Case"), 2},
          std::pair{wxTRANSLATE("Match &Whole Word Only"), 4}, std::pair{wxTRANSLATE("&Bookmark Matched Line"), 8}}) {
        const int id = wxWindow::NewControlId();
        occurrences->AppendCheckItem(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, bit](wxCommandEvent&) { editor_->Occurrences().SetOptions(editor_->Occurrences().Options() ^ bit); },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, bit](wxUpdateUIEvent& event) {
                const int options = editor_->Occurrences().Options();
                event.Check(bit == 1 ? !(options & 1) : (options & bit) != 0);
                event.Enable(bit == 1 || (options & 1));
            },
            id);
    }
    view->AppendSubMenu(occurrences, _("Mark &Occurrences"));
    view->AppendSeparator();
    AppendMarginCommands(view);
    view->AppendSeparator();
    AppendFoldCommands(view);
    view->AppendSeparator();
    AppendZoomCommands(view);
    return view;
}

void EditorFrame::AppendIndentationOptions(wxMenu* view) {
    const int spaces = wxWindow::NewControlId();
    view->AppendCheckItem(spaces, _("Insert Tabs as &Spaces"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { indentation_.ToggleSpaces(*editor_, document_.Language()); }, spaces);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(!editor_->Send(Scintilla::Message::GetUseTabs)); }, spaces);
    const int tabSettings = wxWindow::NewControlId();
    view->Append(tabSettings, _("&Tab Settings..."));
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
    addZoom(_("Zoom &In\tCtrl++"), Scintilla::Message::ZoomIn);
    addZoom(_("Zoom &Out\tCtrl+-"), Scintilla::Message::ZoomOut);
    const int zoomLevel = wxWindow::NewControlId();
    zoom->Append(zoomLevel, _("Zoom &Level..."));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowZoomLevelDialog(this, *editor_); }, zoomLevel);
    addZoom(_("&Reset Zoom\tCtrl+\\"), Scintilla::Message::SetZoom, DisplayUnits::ZoomPercentScale);
    view->AppendSubMenu(zoom, _("&Zoom"));
}

void EditorFrame::AppendDisplayCommands(wxMenu* view) {
    const int wordWrap = wxWindow::NewControlId();
    view->AppendCheckItem(wordWrap, _("&Word Wrap\tCtrl+Shift+W"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->ToggleWordWrap(); }, wordWrap);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetWrapMode) != 0); }, wordWrap);
    const int marker = wxWindow::NewControlId();
    view->AppendCheckItem(marker, _("&Long Line Marker\tCtrl+Shift+L"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ToggleLongLineMarker(); }, marker);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetEdgeMode) != 0); }, marker);
    const int guides = wxWindow::NewControlId();
    view->AppendCheckItem(guides, _("Indentation &Guides\tCtrl+Shift+G"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ToggleIndentationGuides(); }, guides);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetIndentationGuides) != 0); },
        guides);
    view->AppendSeparator();
    const int whitespace = wxWindow::NewControlId();
    view->AppendCheckItem(whitespace, _("Show &Whitespace\tCtrl+Shift+8"));
    Bind(
        wxEVT_MENU,
        [this, whitespace](wxCommandEvent&) {
            const bool visible = editor_->Send(Scintilla::Message::GetViewWS) == 0;
            editor_->Send(Scintilla::Message::SetViewWS,
                          static_cast<uintptr_t>(std::to_underlying(visible ? Scintilla::WhiteSpace::VisibleAlways
                                                                            : Scintilla::WhiteSpace::Invisible)));
            GetMenuBar()->Check(whitespace, visible);
        },
        whitespace);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetViewWS) != 0); }, whitespace);
    const int eol = wxWindow::NewControlId();
    view->AppendCheckItem(eol, _("Show Line &Endings\tCtrl+Shift+9"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            editor_->Send(Scintilla::Message::SetViewEOL, !editor_->Send(Scintilla::Message::GetViewEOL));
        },
        eol);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetViewEOL) != 0); }, eol);
    const int symbols = wxWindow::NewControlId();
    view->AppendCheckItem(symbols, _("Show Wrap S&ymbols\tCtrl+Shift+0"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            showWordWrapSymbols_ = !showWordWrapSymbols_;
            ApplyWrapSymbols();
        },
        symbols);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(showWordWrapSymbols_); }, symbols);
    // One frame hook owns dispatch; the dialog retains its narrower callback.
    Bind(wxEVT_CHAR_HOOK, [this, whitespace, eol, symbols](wxKeyEvent& event) {
        if (!HandleCommandKey(event) && !HandleDisplayKey(event, whitespace, eol, symbols)) event.Skip();
    });
}

void EditorFrame::ApplyWrapSymbols() {
    // Notepad4.cpp::SetWrapVisualFlags at 89b8b36; see licenses/Notepad4.txt.
    using Scintilla::Message;
    if (!showWordWrapSymbols_) {
        editor_->Send(Message::SetWrapVisualFlags, 0);
        return;
    }
    if (wordWrapSymbols_ == 0) wordWrapSymbols_ = 2;
    uintptr_t flags = 0;
    uintptr_t location = 0;
    switch (wordWrapSymbols_ % 10) {
    case 1:
        location |= static_cast<uintptr_t>(std::to_underlying(Scintilla::WrapVisualLocation::EndByText));
        [[fallthrough]];
    case 2:
        flags |= static_cast<uintptr_t>(std::to_underlying(Scintilla::WrapVisualFlag::End));
        break;
    default:
        break;
    }
    switch (wordWrapSymbols_ / 10) {
    case 1:
        location |= static_cast<uintptr_t>(std::to_underlying(Scintilla::WrapVisualLocation::StartByText));
        [[fallthrough]];
    case 2:
        flags |= static_cast<uintptr_t>(std::to_underlying(Scintilla::WrapVisualFlag::Start));
        break;
    case 3:
        flags |= static_cast<uintptr_t>(std::to_underlying(Scintilla::WrapVisualFlag::Margin));
        if (!editor_->LineNumbersVisible()) editor_->SetLineNumbersVisible(true);
        break;
    default:
        break;
    }
    editor_->Send(Message::SetWrapVisualFlagsLocation, location);
    editor_->Send(Message::SetWrapVisualFlags, flags);
}

void EditorFrame::AppendFoldCommands(wxMenu* view) {
    const int show = wxWindow::NewControlId();
    view->AppendCheckItem(show, _("Show Code &Folding\tCtrl+Shift+Alt+F"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetCodeFoldingVisible(!editor_->CodeFoldingVisible()); }, show);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->CodeFoldingVisible()); }, show);
    auto* folds = new wxMenu;
    const auto addFoldCommand = [this, folds](const wxString& label, uintptr_t action) {
        const int id = wxWindow::NewControlId();
        folds->Append(id, label);
        Bind(
            wxEVT_MENU,
            [this, action](wxCommandEvent&) {
                if (editor_->CodeFoldingVisible()) editor_->Send(Scintilla::Message::FoldAll, action);
            },
            id);
    };
    addFoldCommand(_("&Collapse All"),
                   static_cast<uintptr_t>(std::to_underlying(Scintilla::FoldAction::Contract)) |
                       static_cast<uintptr_t>(std::to_underlying(Scintilla::FoldAction::ContractEveryLevel)));
    addFoldCommand(_("&Expand All"), static_cast<uintptr_t>(std::to_underlying(Scintilla::FoldAction::Expand)));
    view->AppendSubMenu(folds, _("&Toggle Folds"));
}

void EditorFrame::AppendCurrentLineCommands(wxMenu* view) {
    const int highlight = wxWindow::NewControlId();
    view->AppendCheckItem(highlight, _("Highlight Current &Block"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) { editor_->SetCurrentBlockHighlighted(!editor_->CurrentBlockHighlighted()); },
        highlight);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->CurrentBlockHighlighted()); },
        highlight);
    auto* currentLine = new wxMenu;
    const auto addMode = [this, currentLine](const wxString& label, LineHighlightMode mode) {
        const int id = wxWindow::NewControlId();
        currentLine->AppendRadioItem(id, label);
        Bind(wxEVT_MENU, [this, mode](wxCommandEvent&) { editor_->SetCurrentLineHighlightMode(mode); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, mode](wxUpdateUIEvent& event) { event.Check(editor_->GetCurrentLineHighlightMode() == mode); }, id);
    };
    addMode(_("&No Highlight"), LineHighlightMode::None);
    addMode(_("Background &Color\tCtrl+Shift+I"), LineHighlightMode::BackgroundColor);
    addMode(_("Outline &Frame\tCtrl+Shift+F"), LineHighlightMode::OutlineFrame);
    currentLine->AppendSeparator();
    const int subline = wxWindow::NewControlId();
    currentLine->AppendCheckItem(subline, _("Highlight &Subline"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            editor_->Send(Scintilla::Message::SetCaretLineHighlightSubLine,
                          !editor_->Send(Scintilla::Message::GetCaretLineHighlightSubLine));
        },
        subline);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Check(editor_->Send(Scintilla::Message::GetCaretLineHighlightSubLine) != 0);
        },
        subline);
    view->AppendSubMenu(currentLine, _("Highlight C&urrent Line"));
}

void EditorFrame::AppendCaretCommands(wxMenu* view) {
    auto* caret = new wxMenu;
    const int overstrike = wxWindow::NewControlId();
    caret->AppendCheckItem(overstrike, _("Block (&OVR Mode)"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto appearance = editor_->GetCaretAppearance();
            appearance.overstrikeBlock = !appearance.overstrikeBlock;
            editor_->SetCaretAppearance(appearance);
        },
        overstrike);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->GetCaretAppearance().overstrikeBlock); },
        overstrike);
    caret->AppendSeparator();
    const char* modes[] = {wxTRANSLATE("Block (&INS Mode)"), wxTRANSLATE("Line Width &1"), wxTRANSLATE("Line Width &2"),
                           wxTRANSLATE("Line Width &3")};
    for (int width = 0; width <= 3; ++width) {
        const int id = wxWindow::NewControlId();
        caret->AppendRadioItem(id, wxGetTranslation(modes[width]));
        Bind(
            wxEVT_MENU,
            [this, width](wxCommandEvent&) {
                auto appearance = editor_->GetCaretAppearance();
                appearance.lineWidth = width;
                editor_->SetCaretAppearance(appearance);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, width](wxUpdateUIEvent& event) { event.Check(editor_->GetCaretAppearance().lineWidth == width); },
            id);
    }
    caret->AppendSeparator();
    const int blink = wxWindow::NewControlId();
    caret->AppendCheckItem(blink, _("&No Blink"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto appearance = editor_->GetCaretAppearance();
            appearance.blinkPeriod = appearance.blinkPeriod == 0 ? -1 : 0;
            editor_->SetCaretAppearance(appearance);
        },
        blink);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->GetCaretAppearance().blinkPeriod == 0); }, blink);
    const int selection = wxWindow::NewControlId();
    caret->AppendCheckItem(selection, _("Draw Block Caret in &Selection"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto appearance = editor_->GetCaretAppearance();
            appearance.blockAfterSelection = !appearance.blockAfterSelection;
            editor_->SetCaretAppearance(appearance);
        },
        selection);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(!editor_->GetCaretAppearance().blockAfterSelection); }, selection);
    view->AppendSubMenu(caret, _("C&aret Style"));
}

void EditorFrame::AppendSettingsCommands(wxMenu* view) {
    if (settings_) {
        view->AppendSeparator();
        const int saveOnExit = wxWindow::NewControlId();
        view->AppendCheckItem(saveOnExit, _("Save Settings On E&xit"));
        Bind(
            wxEVT_MENU, [this](wxCommandEvent&) { settings_->values.saveSettings = !settings_->values.saveSettings; },
            saveOnExit);
        Bind(
            wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(settings_->values.saveSettings); },
            saveOnExit);
        const int saveNow = wxWindow::NewControlId();
        view->Append(saveNow, _("Save Settings &Now"));
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                if (SaveSettings(true))
                    SetStatusText(_("Settings saved"));
                else
                    wxMessageBox(wxString::Format(_("Could not write %s"), settings_->Path()),
                                 _("Could not save settings"), wxOK | wxICON_ERROR, this);
            },
            saveNow);
    }
}

void EditorFrame::OnSearchKey(wxKeyEvent& event) {
    if (!HandleCommandKey(event)) event.Skip();
}

bool EditorFrame::HandleCommandKey(const wxKeyEvent& event) {
    if (HandleInsertionKey(event)) return true;
    if (event.GetKeyCode() == 'I' && event.ControlDown() && !event.AltDown()) {
        if (event.ShiftDown() && !event.MetaDown())
            editor_->SetCurrentLineHighlightMode(LineHighlightMode::BackgroundColor);
        else if (!event.ShiftDown() && !event.MetaDown() && !editor_->Send(Scintilla::Message::GetSelectionEmpty))
            editor_->SplitLines();
        return true;
    }
    if (HandleTabConversionKey(event) || HandleCaseConversionKey(event)) return true;
    if (event.GetKeyCode() == 'T' && event.AltDown()) {
        if (!event.ControlDown() && !event.ShiftDown() && !event.MetaDown()) editor_->StripTrailingBlanks();
        return true;
    }
    return HandleSearchKey(event) || HandleNavigationKey(event) || HandleViewKey(event) || HandleDocumentKey(event);
}

bool EditorFrame::HandleInsertionKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_F8 && event.ControlDown()) {
        if (!event.AltDown() && !event.ShiftDown() && !event.MetaDown()) InsertEncodingIdentifier();
        return true;
    }
    if (event.GetKeyCode() == WXK_F9 && event.ControlDown()) {
        if (!event.AltDown() && !event.MetaDown())
            InsertFileName(event.ShiftDown() ? FileNamePart::FullPath : FileNamePart::FileName);
        return true;
    }
    if (event.GetKeyCode() == WXK_F9 && event.AltDown() && event.ShiftDown() && !event.ControlDown()) {
        if (!event.MetaDown()) CopyFileName(FileNamePart::FullPath);
        return true;
    }
    if (event.GetKeyCode() == 'X' && event.AltDown() && !event.ControlDown() && !event.ShiftDown() &&
        !event.MetaDown()) {
        ShowInsertTagDialog(this, *editor_);
        return true;
    }
    if (event.GetKeyCode() == WXK_F5 && event.ControlDown()) {
        if (!event.AltDown() && !event.MetaDown()) InsertDateTime(!event.ShiftDown());
        return true;
    }
    return false;
}

bool EditorFrame::HandleCaseConversionKey(const wxKeyEvent& event) {
    const int key = event.GetKeyCode();
    if (event.AltDown() && event.ShiftDown() && (key == 'X' || key == 'H')) {
        if (!event.ControlDown() && !event.MetaDown())
            editor_->Send(Scintilla::Message::CustomCaseMapping,
                          key == 'X' ? CustomCaseCommand::EscapeHtml : CustomCaseCommand::UnescapeHtml);
        return true;
    }
    if (!event.ControlDown() || !event.AltDown()) return false;
    int mapping;
    switch (key) {
    case 'X':
        mapping = CustomCaseCommand::CharacterToHex;
        break;
    case 'C':
        mapping = CustomCaseCommand::HexToCharacter;
        break;
    case 'E':
        mapping = CustomCaseCommand::EscapeC;
        break;
    case 'R':
        mapping = CustomCaseCommand::UnescapeC;
        break;
    case 'U':
        mapping = CustomCaseCommand::Invert;
        break;
    case 'I':
        mapping = CustomCaseCommand::Title;
        break;
    case 'O':
        mapping = CustomCaseCommand::Sentence;
        break;
    default:
        return false;
    }
    if (!event.ShiftDown() && !event.MetaDown()) editor_->Send(Scintilla::Message::CustomCaseMapping, mapping);
    return true;
}

void EditorFrame::InsertDateTime(bool shortDate) {
    const auto& preferences = settings_ ? settings_->values : defaults_;
    editor_->InsertDateTime(shortDate ? DateTimeInsertion::Short : DateTimeInsertion::Long,
                            shortDate ? preferences.dateTimeShort : preferences.dateTimeLong);
}

void EditorFrame::InsertFileName(FileNamePart part) {
    auto text = document_.FileNameText(part);
    if (document_.IsDefaultEncoding()) text = NativeDefaultEncodingText(text);
    const auto bytes = text.ToStdString(wxConvUTF8);
    editor_->Send(Scintilla::Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(bytes.c_str()));
}

void EditorFrame::CopyFileName(FileNamePart part) {
    const auto text = document_.FileNameText(part);
    const auto bytes = text.ToStdString(wxConvUTF8);
    editor_->Send(Scintilla::Message::CopyText, bytes.size(), reinterpret_cast<intptr_t>(bytes.c_str()));
}

void EditorFrame::AppendFileNameCopyCommands(wxMenu* copy) {
    using enum FileNamePart;
    for (const auto& [label, part] : {std::pair{wxTRANSLATE("&File Name"), BaseName},
                                      std::pair{wxTRANSLATE("File Name and &Ext.\tShift+F9"), FileName},
                                      std::pair{wxTRANSLATE("Full &Path Name\tAlt+Shift+F9"), FullPath}}) {
        const int id = wxWindow::NewControlId();
        copy->Append(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, part](wxCommandEvent&) { CopyFileName(part); }, id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    }
}

void EditorFrame::AppendDateTimeCommands(wxMenu* edit) {
    auto* insert = new wxMenu;
    const int insertTag = wxWindow::NewControlId();
    insert->Append(insertTag, _("&HTML/XML Tag...\tAlt+X"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowInsertTagDialog(this, *editor_); }, insertTag);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, insertTag);
    const int guid = wxWindow::NewControlId();
    insert->Append(guid, _("New &GUID"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->InsertGuid(); }, guid);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, guid);
    auto* controls = new wxMenu;
    for (size_t index = 0; index < UnicodeControlInsertions.size(); ++index) {
        const int id = wxWindow::NewControlId();
        controls->Append(id, wxGetTranslation(wxString::FromUTF8(UnicodeControlInsertions[index].label)));
        Bind(
            wxEVT_MENU,
            [this, index](wxCommandEvent&) {
                editor_->InsertUnicodeControlCharacter(static_cast<UnicodeControlCharacter>(index),
                                                       document_.IsDefaultEncoding());
            },
            id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    }
    insert->AppendSubMenu(controls, _("Unicode &Control Character"));
    insert->AppendSeparator();
    for (const auto& [label, part] :
         {std::pair{wxTRANSLATE("&File Name"), FileNamePart::BaseName},
          std::pair{wxTRANSLATE("File Name and E&xtension\tCtrl+F9"), FileNamePart::FileName},
          std::pair{wxTRANSLATE("Full &Path Name\tCtrl+Shift+F9"), FileNamePart::FullPath}}) {
        const int id = wxWindow::NewControlId();
        insert->Append(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, part](wxCommandEvent&) { InsertFileName(part); }, id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    }
    insert->AppendSeparator();
    auto* timestamps = new wxMenu;
    const auto add = [this](wxMenu* menu, const char* label, DateTimeInsertion command) {
        const int id = wxWindow::NewControlId();
        menu->Append(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, command](wxCommandEvent&) { editor_->InsertDateTime(command); }, id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    };
    add(insert, wxTRANSLATE("Current &Date"), DateTimeInsertion::LocalDate);
    add(insert, wxTRANSLATE("Current Date &Time"), DateTimeInsertion::LocalDateTime);
    add(insert, wxTRANSLATE("&UTC Date Time"), DateTimeInsertion::UtcDateTime);
    add(insert, wxTRANSLATE("Unix Timesta&mp"), DateTimeInsertion::UnixSeconds);
    add(timestamps, wxTRANSLATE("&Millisecond (ms)"), DateTimeInsertion::UnixMilliseconds);
    add(timestamps, wxTRANSLATE("Micro&second (us)"), DateTimeInsertion::UnixMicroseconds);
    add(timestamps, wxTRANSLATE("&Nanosecond (ns)"), DateTimeInsertion::UnixNanoseconds);
    insert->AppendSubMenu(timestamps, _("&Other Timestamps"));
    for (const auto& [label, shortDate] : {std::pair{wxTRANSLATE("Time/Date (&Short Form)\tCtrl+F5"), true},
                                           std::pair{wxTRANSLATE("Time/Date (&Long Form)\tCtrl+Shift+F5"), false}}) {
        const int id = wxWindow::NewControlId();
        insert->Append(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, shortDate](wxCommandEvent&) { InsertDateTime(shortDate); }, id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    }
    insert->AppendSeparator();
    const int encoding = wxWindow::NewControlId();
    insert->Append(encoding, _("&Encoding Identifier\tCtrl+F8"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { InsertEncodingIdentifier(); }, encoding);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, encoding);
    const int shebang = wxWindow::NewControlId();
    insert->Append(shebang, _("Script She&bang Line"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->InsertScriptShebangLine(SyntaxFor(document_.Language())); },
        shebang);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, shebang);
    edit->AppendSubMenu(insert, _("I&nsert"));
}

void EditorFrame::InsertEncodingIdentifier() {
    editor_->InsertEncodingIdentifier(OriginalEncodingIdentifier(document_.CurrentEncoding()),
                                      SyntaxFor(document_.Language()));
}

void EditorFrame::ConvertTabs(bool toTabs, bool onlyIndent) {
    if (!(toTabs ? editor_->SpacesToTabs(onlyIndent) : editor_->TabsToSpaces(onlyIndent)))
        wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
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
    if (HandleBlockNavigationKey(event)) return true;
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

bool EditorFrame::HandleBlockNavigationKey(const wxKeyEvent& event) {
    // The original OEM accelerators distinguish Shift and Ctrl exactly. Consume
    // other Alt punctuation variants before native lookup can drop modifiers.
    const int rawKey = event.GetKeyCode();
    const int key = rawKey == '{' ? '[' : rawKey == '}' ? ']' : rawKey == '<' ? ',' : rawKey == '>' ? '.' : rawKey;
    const bool punctuation = key == '[' || key == ']' || key == ',' || key == '.';
    if (!punctuation || event.MetaDown()) return false;
    if (event.AltDown()) {
        HandleAltBlockNavigationKey(event, key);
        return true;
    }
    if ((key == ',' || key == '.') && event.ControlDown() && event.ShiftDown()) {
        // Original CMD_JUMP2SELSTART / CMD_JUMP2SELEND: change the active
        // endpoint without changing the selected text or line-selection mode.
        editor_->FocusSelectionBoundary(key == ',');
        return true;
    }
    return false;
}

void EditorFrame::HandleAltBlockNavigationKey(const wxKeyEvent& event, int key) {
    using enum BlockNavigation;
    if (key == '[' && !event.ControlDown() && !event.ShiftDown())
        editor_->GoToBlock(Start);
    else if (key == ']' && !event.ControlDown()) {
        if (event.ShiftDown())
            editor_->SelectCurrentBlockLines();
        else
            editor_->GoToBlock(End);
    } else if ((key == ',' || key == '.') && !event.ShiftDown()) {
        if (key == ',')
            editor_->GoToBlock(event.ControlDown() ? PreviousSibling : Previous);
        else
            editor_->GoToBlock(event.ControlDown() ? NextSibling : Next);
    }
}

bool EditorFrame::HandleViewKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() == 'V' && event.ControlDown() && event.ShiftDown()) {
        if (!event.AltDown() && !event.MetaDown()) editor_->SetVisualBraceMatching(!editor_->VisualBraceMatching());
        return true;
    }
    if (event.GetKeyCode() == 'L' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
        !event.MetaDown()) {
        ToggleLongLineMarker();
        return true;
    }
    if (event.GetKeyCode() == 'G' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
        !event.MetaDown()) {
        ToggleIndentationGuides();
        return true;
    }
    if (event.GetKeyCode() == 'F' && event.ControlDown() && event.ShiftDown() && !event.AltDown() && !event.MetaDown())
        editor_->SetCurrentLineHighlightMode(LineHighlightMode::OutlineFrame);
    else if (event.GetKeyCode() == 'W' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
             !event.MetaDown())
        editor_->ToggleWordWrap();
    else if (event.GetKeyCode() == 'N' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
             !event.MetaDown())
        editor_->SetLineNumbersVisible(!editor_->LineNumbersVisible());
    else if (event.GetKeyCode() == 'M' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
             !event.MetaDown())
        editor_->SetBookmarkMarginVisible(!editor_->BookmarkMarginVisible());
    else
        return false;
    return true;
}

void EditorFrame::ToggleIndentationGuides() {
    const auto& preferences = settings_ ? settings_->values : defaults_;
    ApplyIndentationGuides(*editor_, document_.Language(), editor_->Send(Scintilla::Message::GetIndentationGuides) == 0,
                           preferences.simpleIndentGuides);
}

void EditorFrame::ToggleLongLineMarker() {
    const auto& preferences = settings_ ? settings_->values : defaults_;
    const bool show = editor_->Send(Scintilla::Message::GetEdgeMode) == 0;
    editor_->Send(Scintilla::Message::SetEdgeMode, show ? preferences.longLineMode : 0);
    if (show) ApplyLongLineColour(*editor_);
}

bool EditorFrame::HandleDisplayKey(const wxKeyEvent& event, int whitespace, int eol, int symbols) {
    if (!event.ControlDown() || !event.ShiftDown() || event.AltDown() || event.MetaDown()) return false;
    const int key = event.GetKeyCode();
    const int commandId = key == '8' || key == '*'   ? whitespace
                          : key == '9' || key == '(' ? eol
                          : key == '0' || key == ')' ? symbols
                                                     : wxID_NONE;
    if (commandId == wxID_NONE) return false;
    wxCommandEvent command(wxEVT_MENU, commandId);
    GetEventHandler()->ProcessEvent(command);
    return true;
}

bool EditorFrame::HandleDocumentKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() == 'H' && event.AltDown() && !event.ControlDown() && !event.ShiftDown() &&
        !event.MetaDown()) {
        ShowRecentFiles();
        return true;
    }
    if (event.GetKeyCode() == WXK_F5 && !event.HasAnyModifiers()) {
        wxCommandEvent command(wxEVT_MENU, revertId_);
        GetEventHandler()->ProcessEvent(command);
        return true;
    }
    if (event.GetKeyCode() == WXK_F10) {
        if (!event.HasAnyModifiers()) ToggleReadOnlyMode();
        return true;
    }
    if (event.GetKeyCode() != WXK_F6) return false;
    // GTK Wayland can consume Ctrl while matching F6 accelerators. Route
    // the original exact modifiers before native accelerator matching.
    if (!event.HasAnyModifiers()) {
        wxCommandEvent command(wxEVT_MENU, wxID_SAVEAS);
        GetEventHandler()->ProcessEvent(command);
    } else if (event.ControlDown() && !event.ShiftDown() && !event.AltDown() && !event.MetaDown()) {
        wxCommandEvent command(wxEVT_MENU, saveCopyId_);
        GetEventHandler()->ProcessEvent(command);
    }
    return true;
}

void EditorFrame::ToggleReadOnlyMode() {
    editor_->Send(Scintilla::Message::SetReadOnly, !editor_->Send(Scintilla::Message::GetReadOnly));
    UpdateDocumentState();
    UpdateWindowUI(wxUPDATE_UI_RECURSE);
}

void EditorFrame::AppendSpecialCommands(wxMenu* edit) {
    const int completeWord = wxWindow::NewControlId();
    auto* completionItem = edit->Append(completeWord, _("Complete &Word\tAlt+/"));
    completionItem->AddExtraAccel(wxAcceleratorEntry(wxACCEL_ALT, WXK_NUMPAD_DIVIDE, completeWord));
    completionItem->AddExtraAccel(wxAcceleratorEntry(wxACCEL_ALT, WXK_DIVIDE, completeWord));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto& preferences = settings_ ? settings_->values : defaults_;
            CompleteWord(*editor_, SyntaxFor(document_.EffectiveLanguage()), preferences.completion);
        },
        completeWord);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); },
        completeWord);
    edit->AppendSeparator();
    for (const auto& [label, mapping] :
         {std::pair{wxTRANSLATE("C&haracter to Hex\tCtrl+Alt+X"), CustomCaseCommand::CharacterToHex},
          std::pair{wxTRANSLATE("Hex to Cha&racter\tCtrl+Alt+C"), CustomCaseCommand::HexToCharacter},
          std::pair{wxTRANSLATE("Esca&pe C Chars\tCtrl+Alt+E"), CustomCaseCommand::EscapeC},
          std::pair{wxTRANSLATE("&Unescape C Chars\tCtrl+Alt+R"), CustomCaseCommand::UnescapeC}}) {
        if (mapping == CustomCaseCommand::EscapeC) edit->AppendSeparator();
        const int id = wxWindow::NewControlId();
        edit->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, mapping](wxCommandEvent&) { editor_->Send(Scintilla::Message::CustomCaseMapping, mapping); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
}

wxMenu* EditorFrame::CreateSearchMenu() {
    auto* search = new wxMenu;
    AppendSearchCommands(search);
    AppendNavigationCommands(search);
    return search;
}

void EditorFrame::AppendCompletionOptions(wxMenu* view) {
    const int autoCompletion = wxWindow::NewControlId();
    view->Append(autoCompletion, _("Auto &Completion Settings..."));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& preferences = settings_ ? settings_->values : defaults_;
            if (ShowAutoCompletionSettingsDialog(this, preferences)) {
                editor_->SetAutoInsertMask(preferences.autoInsertMask);
                editor_->SetCompletionSettings(preferences.completion);
                editor_->SetAutoIndent(preferences.autoIndent);
            }
        },
        autoCompletion);
    const int ignoreCase = wxWindow::NewControlId();
    view->AppendCheckItem(ignoreCase, _("Auto Completion I&gnore Case"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& preferences = settings_ ? settings_->values : defaults_;
            preferences.completion.ignoreCase = !preferences.completion.ignoreCase;
            editor_->SetCompletionSettings(preferences.completion);
            editor_->Send(Scintilla::Message::AutoCCancel);
        },
        ignoreCase);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            const auto& preferences = settings_ ? settings_->values : defaults_;
            event.Check(preferences.completion.ignoreCase);
        },
        ignoreCase);
}

void EditorFrame::AppendSelectionOptions(wxMenu* view) {
    auto* selection = new wxMenu;
    for (const auto& [label, bit] :
         {std::pair{wxTRANSLATE("Enable &Multiple Selection"), SelectOption_EnableMultipleSelection},
          std::pair{wxTRANSLATE("Copy Selection As Find Text"), SelectOption_CopySelectionAsFindText},
          std::pair{wxTRANSLATE("Copy Paste Buffer As Find Text"), SelectOption_CopyPasteBufferAsFindText},
          std::pair{wxTRANSLATE("Undo Redo &Remember Selection"), SelectOption_UndoRedoRememberSelection}}) {
        if (!settings_ &&
            (bit == SelectOption_CopySelectionAsFindText || bit == SelectOption_CopyPasteBufferAsFindText))
            continue;
        const int id = wxWindow::NewControlId();
        selection->AppendCheckItem(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, bit](wxCommandEvent&) {
                auto& preferences = settings_ ? settings_->values : defaults_;
                const auto options = static_cast<unsigned>(preferences.selectOption) ^ static_cast<unsigned>(bit);
                preferences.selectOption = static_cast<int>(options);
                const bool enabled = (options & static_cast<unsigned>(bit)) != 0;
                // Original Notepad4.cpp selection option commands preserve active selections and text history.
                if (bit == SelectOption_EnableMultipleSelection) {
                    editor_->Send(Scintilla::Message::SetMultipleSelection, enabled);
                } else if (bit == SelectOption_UndoRedoRememberSelection) {
                    const auto history =
                        enabled ? static_cast<uintptr_t>(
                                      std::to_underlying(Scintilla::UndoSelectionHistoryOption::Enabled)) |
                                      static_cast<uintptr_t>(
                                          std::to_underlying(Scintilla::UndoSelectionHistoryOption::Scroll))
                                : 0;
                    editor_->Send(Scintilla::Message::SetUndoSelectionHistory, history);
                }
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, bit](wxUpdateUIEvent& event) {
                const auto options = static_cast<unsigned>((settings_ ? settings_->values : defaults_).selectOption);
                event.Check((options & static_cast<unsigned>(bit)) != 0);
            },
            id);
    }
    AppendLineSelectionCommands(selection);
    view->AppendSubMenu(selection, _("Select and &Edit Options"));
}

wxMenu* EditorFrame::CreateSettingsMenu() {
    auto* view = new wxMenu;
    AppendUILanguageCommands(view);
    AppendIndentationOptions(view);
    const int longLineSettings = wxWindow::NewControlId();
    view->Append(longLineSettings, _("Long Line Settings..."));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            using enum Scintilla::Message;
            auto& preferences = settings_ ? settings_->values : defaults_;
            auto limit = static_cast<int>(editor_->Send(GetEdgeColumn));
            int mode = preferences.longLineMode;
            if (ShowLongLineSettingsDialog(this, limit, mode)) {
                preferences.longLinesLimit = limit;
                preferences.longLineMode = mode;
                editor_->Send(SetEdgeColumn, limit);
                editor_->Send(SetEdgeMode, mode);
                ApplyLongLineColour(*editor_);
            }
        },
        longLineSettings);
    AppendCompletionOptions(view);
    const int autoIndent = wxWindow::NewControlId();
    view->AppendCheckItem(autoIndent, _("Auto &Indent"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& preferences = settings_ ? settings_->values : defaults_;
            preferences.autoIndent = !preferences.autoIndent;
            editor_->SetAutoIndent(preferences.autoIndent);
        },
        autoIndent);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->AutoIndentEnabled()); }, autoIndent);
    AppendSelectionOptions(view);
    view->AppendSeparator();
    auto* appearance = new wxMenu;
    const int shrinkScrollWidth = wxWindow::NewControlId();
    appearance->AppendCheckItem(shrinkScrollWidth, _("Auto-Shrink Horizontal Scrollbar"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetAutoShrinkScrollWidth(!editor_->AutoShrinkScrollWidth()); },
        shrinkScrollWidth);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->AutoShrinkScrollWidth()); },
        shrinkScrollWidth);
    view->AppendSubMenu(appearance, _("Appea&rance"));
    const int fileChanges = wxWindow::NewControlId();
    view->AppendCheckItem(fileChanges, _("File &Change Notification...\tAlt+F5"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFileChangeNotificationOptions(); }, fileChanges);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(fileWatchingOptions_.mode != FileWatchingMode::None); },
        fileChanges);
    if (settings_) {
        auto* other = new wxMenu;
        const int saveFind = wxWindow::NewControlId();
        other->AppendCheckItem(saveFind, _("Remember &Search Options"));
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) { settings_->values.saveFindReplace = !settings_->values.saveFindReplace; },
            saveFind);
        Bind(
            wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(settings_->values.saveFindReplace); },
            saveFind);
        const int saveRecent = wxWindow::NewControlId();
        other->AppendCheckItem(saveRecent, _("Remember &Recent Files"));
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) { settings_->values.saveRecentFiles = !settings_->values.saveRecentFiles; },
            saveRecent);
        Bind(
            wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(settings_->values.saveRecentFiles); },
            saveRecent);
        view->AppendSubMenu(other, _("Other Sett&ings"));
    }
#ifdef __WXMSW__
    view->AppendSeparator();
    auto* advanced = new wxMenu;
    auto* rendering = new wxMenu;
    for (const auto& [label, technology] : {std::pair{wxTRANSLATE("&Legacy GDI"), Scintilla::Technology::Default},
                                            std::pair{wxTRANSLATE("&Direct2D"), Scintilla::Technology::DirectWrite}}) {
        const int id = wxWindow::NewControlId();
        rendering->AppendRadioItem(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, technology](wxCommandEvent&) {
                editor_->Send(Scintilla::Message::SetTechnology,
                              static_cast<uintptr_t>(std::to_underlying(technology)));
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, technology](wxUpdateUIEvent& event) {
                event.Check(editor_->Send(Scintilla::Message::GetTechnology) ==
                            static_cast<intptr_t>(std::to_underlying(technology)));
            },
            id);
    }
    advanced->AppendSubMenu(rendering, _("Ren&dering Technology"));
    view->AppendSubMenu(advanced, _("A&dvanced Settings"));
#endif
    AppendSettingsCommands(view);
    return view;
}

void EditorFrame::AppendMarginCommands(wxMenu* view) {
    const int lineNumbers = wxWindow::NewControlId();
    view->AppendCheckItem(lineNumbers, _("Line &Numbers\tCtrl+Shift+N"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetLineNumbersVisible(!editor_->LineNumbersVisible()); },
        lineNumbers);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->LineNumbersVisible()); }, lineNumbers);
    const int bookmarkMargin = wxWindow::NewControlId();
    view->AppendCheckItem(bookmarkMargin, _("&Bookmark Margin\tCtrl+Shift+M"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetBookmarkMarginVisible(!editor_->BookmarkMarginVisible()); },
        bookmarkMargin);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->BookmarkMarginVisible()); },
        bookmarkMargin);
    const int changeHistory = wxWindow::NewControlId();
    view->AppendCheckItem(changeHistory, _("Change &History Marker"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->ToggleChangeHistory(); }, changeHistory);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->ChangeHistoryOptions() != 0); },
        changeHistory);
}
