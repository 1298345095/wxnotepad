#include "app/editor_frame.h"
#include "app/command_ids.h"
#include "app/dialogs/document_dialogs.h"
#include "app/editor_printing.h"
#include "app/editor_print_job.h"
#include "app/localization.h"
#include "editor/editor_control.h"
#include "editor/platform/native_encoding.h"
#include "editor/platform/popup_placement.h"
#include "settings/settings_store.h"
#include "syntax/syntax_scheme.h"
#include <array>
#include <memory>
#include <utility>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/translation.h>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#endif

void EditorFrame::CreateMenus() {
    auto* menus = new wxMenuBar;
    menus->Append(CreateFileMenu(), _("&File"));
    menus->Append(CreateEditMenu(), _("&Edit"));
    menus->Append(CreateSearchMenu(), _("&Search"));
    menus->Append(CreateViewMenu(), _("&View"));
    menus->Append(CreateLanguageMenu(), _("Sche&me"));
    menus->Append(CreateSettingsMenu(), _("Sett&ings"));
    menus->Append(CreateToolsMenu(), _("&Tools"));
    SetMenuBar(menus);
    // As in Notepad4.cpp::WM_CONTEXTMENU, the application owns the popup.
    editor_->Bind(wxEVT_CONTEXT_MENU, &EditorFrame::ShowEditorContextMenu, this);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Clear); }, wxID_DELETE);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); },
        wxID_DELETE);
}

void EditorFrame::ShowEditorContextMenu(wxContextMenuEvent& event) {
    // Original Notepad4 89b8b36 IDR_POPUPMENU: preserve the command groups,
    // hit-test only an empty selection, and anchor keyboard invocation at caret.
    using enum Scintilla::Message;
    auto point = event.GetPosition();
    if (point == wxDefaultPosition) {
        const auto caret = editor_->Send(GetCurrentPos);
        point = wxPoint(static_cast<int>(editor_->Send(PointXFromPosition, 0, caret)),
                        static_cast<int>(editor_->Send(PointYFromPosition, 0, caret)));
    } else {
        point = editor_->ScreenToClient(point);
        if (editor_->Send(GetSelectionEmpty))
            editor_->Send(GotoPos, editor_->Send(PositionFromPoint, point.x, point.y));
    }
    wxMenu menu;
    // TRANSLATORS: IDR_POPUPMENU undo editing command.
    menu.Append(wxID_UNDO, _("&Undo"));
    // TRANSLATORS: IDR_POPUPMENU redo editing command.
    menu.Append(wxID_REDO, _("&Redo"));
    menu.AppendSeparator();
    // TRANSLATORS: IDR_POPUPMENU cut selection to the clipboard.
    menu.Append(wxID_CUT, _("Cu&t"));
    // TRANSLATORS: IDR_POPUPMENU copy selection to the clipboard.
    menu.Append(wxID_COPY, _("&Copy"));
    // TRANSLATORS: IDR_POPUPMENU paste clipboard text.
    menu.Append(wxID_PASTE, _("&Paste"));
    // TRANSLATORS: IDR_POPUPMENU delete selection or the character at the caret.
    menu.Append(wxID_DELETE, _("&Delete"));
    menu.AppendSeparator();
    // TRANSLATORS: IDR_POPUPMENU select the complete document.
    menu.Append(wxID_SELECTALL, _("Select &All"));
    menu.AppendSeparator();
    // TRANSLATORS: IDR_POPUPMENU toggle the current fold block.
    menu.Append(currentBlockFoldId_, _("Toggle &Fold"));
    menu.AppendSeparator();
    // TRANSLATORS: IDR_POPUPMENU open the selected or caret-local file, folder or link.
    menu.Append(openSelectionId_, _("&Open"));
    // Keep the editor as the native popup's invoking window and focus owner.
    auto screen = editor_->ClientToScreen(point + wxPoint(1, 1));
#ifdef __WXGTK__
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(menu.m_menu))) {
        // Some compositors leave even constrained xdg_popups beyond the edge.
        // Measure the native menu inside its unmapped GTK parent, then reuse
        // the completion/CallTip work-area placement. No popup is mapped here.
        gtk_widget_show(menu.m_menu);
        GtkRequisition natural{};
        gtk_widget_get_preferred_size(menu.m_menu, nullptr, &natural);
        gtk_widget_hide(menu.m_menu);
        screen = NativePopup::FitToWorkArea(*this, screen, wxSize(natural.width, natural.height)).GetPosition();
    }
#endif
    editor_->PopupMenu(&menu, editor_->ScreenToClient(screen));
}

wxMenu* EditorFrame::CreateToolsMenu() {
    auto* tools = new wxMenu;
    AppendDocumentTools(tools);
    AppendRunCommand(tools);
    tools->AppendSeparator();
    auto* selection = new wxMenu;
    AppendOpenTargetCommands(selection);
    selection->AppendSeparator();
    // TRANSLATORS: CMD_ONLINE_SEARCH_GOOGLE/BING/WIKI and CMD_CUSTOM_ACTION1/2, in original menu order.
    const std::array selectionActions{std::pair{SelectionAction::Google, _("Search with &Google")},
                                      std::pair{SelectionAction::Bing, _("Search with &Bing")},
                                      std::pair{SelectionAction::Wikipedia, _("Search on &Wikipedia")},
                                      std::pair{SelectionAction::Custom1, _("Custom Action &1\tCtrl+Shift+1")},
                                      std::pair{SelectionAction::Custom2, _("Custom Action &2\tCtrl+Shift+2")}};
    for (const auto& [action, label] : selectionActions) {
        if (action == SelectionAction::Custom1) selection->AppendSeparator();
        const int id = wxWindow::NewControlId();
        // TRANSLATORS: Selection action status help; the command receives the selected text.
        selection->Append(id, label, _("Open the selected text using this action's command."));
        Bind(wxEVT_MENU, [this, action](wxCommandEvent&) { RunSelectionAction(action); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    // TRANSLATORS: Original Action on Selection submenu; actions use selected or caret-local text.
    tools->AppendSubMenu(selection, _("Action on &Selection"));
    auto* base64 = new wxMenu;
    for (const auto& [label, command] :
         {std::pair{wxTRANSLATE("Standard &Encode"), Base64Conversion::Encode},
          std::pair{wxTRANSLATE("URL &Safe Encode"), Base64Conversion::UrlSafeEncode},
          std::pair{wxTRANSLATE("Encode as HTML Embedded &Image"), Base64Conversion::HtmlEmbeddedImage},
          std::pair{wxTRANSLATE("&Decode"), Base64Conversion::Decode},
          std::pair{wxTRANSLATE("Decode as &Hex"), Base64Conversion::DecodeAsHex}}) {
        const int id = wxWindow::NewControlId();
        base64->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, command](wxCommandEvent&) {
                if (!editor_->ConvertBase64(command, document_.Path(), document_.IsDefaultEncoding()))
                    wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    tools->AppendSubMenu(base64, _("&Base64"));
    return tools;
}

void EditorFrame::AppendReloadEncodingCommands(wxMenu* reload) {
    const auto append = [this, reload](const wxString& label, auto load) {
        const int id = wxWindow::NewControlId();
        reload->Append(id, label);
        Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(!document_.Path().empty()); }, id);
        Bind(
            wxEVT_MENU,
            [this, load](wxCommandEvent&) {
                const auto result = load();
                if (result.status == OpenStatus::Failed)
                    wxMessageBox(result.error, _("Could not reload file"), wxOK | wxICON_ERROR, this);
            },
            id);
    };
    // TRANSLATORS: Reopen the file with UTF-8 as the source encoding (CMD_RELOADUTF8).
    append(_("As &UTF-8\tShift+F8"), [this] { return ReloadAsUtf8(); });
    // TRANSLATORS: Reopen the file with the system ANSI source encoding (CMD_RELOADANSI).
    append(_("As &ANSI\tCtrl+Shift+A"), [this] { return ReloadQuickEncoding(QuickReloadEncoding::Ansi); });
    // TRANSLATORS: Reopen the file with the system OEM source encoding (CMD_RELOADOEM).
    append(_("As &OEM\tCtrl+Shift+O"), [this] { return ReloadQuickEncoding(QuickReloadEncoding::Oem); });
    reload->AppendSeparator();
    // TRANSLATORS: Reopen with the configured default source encoding (CMD_RECODEDEFAULT).
    append(_("&Default Encoding\tCtrl+Alt+F"), [this] { return ReloadQuickEncoding(QuickReloadEncoding::Default); });
    // TRANSLATORS: Open the source encoding chooser for this file (IDM_RECODE_SELECT).
    append(_("With &Encoding...\tF8"), [this] { return ReloadWithEncoding(); });
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
    const int more = std::to_underlying(AppCommand::ChooseEncoding);
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
    const int newWindow = std::to_underlying(AppCommand::NewWindow);
    file->Append(newWindow, _("New &Window\tAlt+N"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!NewWindow())
                wxMessageBox(_("Could not start another wxnotepad process."), _("Could not open new window"),
                             wxOK | wxICON_ERROR, this);
        },
        newWindow);
    file->Append(wxID_OPEN, _("&Open...\tCtrl+O"));
    auto* openAs = new wxMenu;
    const int openGbk = wxWindow::NewControlId();
    openAs->Append(openGbk, _("Open As &GBK..."));
    const int openUtf16 = wxWindow::NewControlId();
    openAs->Append(openUtf16, _("Open As UTF-16LE..."));
    for (const auto& [label, encoding] : {std::pair{wxTRANSLATE("Open As Windows-1252..."), FileEncoding::Windows1252},
                                          std::pair{wxTRANSLATE("Open As DOS-437..."), FileEncoding::Dos437}}) {
        const int id = wxWindow::NewControlId();
        openAs->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, encoding](wxCommandEvent&) {
                const auto result = OpenAs(encoding);
                if (result.status == OpenStatus::Failed)
                    wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, this);
            },
            id);
    }
    // TRANSLATORS: File submenu grouping explicit-codec Open commands; extension to IDR_MAINWND.
    file->AppendSubMenu(openAs, _("Open &As"));
    file->Append(wxID_SAVE, _("&Save\tCtrl+S"));
    file->Append(wxID_SAVEAS, _("Save &As...\tF6"));
    const int saveBackup = wxWindow::NewControlId();
    // TRANSLATORS: Saves a separate backup without changing the current file (IDM_FILE_SAVEBACKUP).
    file->Append(saveBackup, _("Save Back&up"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { document_.SaveBackup(); }, saveBackup);
    file->Append(std::to_underlying(AppCommand::SaveCopy), _("Save Cop&y...\tCtrl+F6"));
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
    const int readOnlyFile = wxWindow::NewControlId();
#ifdef __WXMSW__
    // TRANSLATORS: IDM_FILE_READONLY_FILE help; distinguish the disk attribute from F10 editor protection.
    const wxString fileProtectionHelp = _("Change the disk file's read-only attribute. F10 controls editing only.");
#else
    const wxString fileProtectionHelp =
        // TRANSLATORS: Linux IDM_FILE_READONLY_FILE help; owner-write changes affect the owner's other processes.
        _("Change the file owner's write permission for this user's other programs too. F10 controls editing only.");
#endif
    // TRANSLATORS: IDM_FILE_READONLY_FILE menu item; changes disk-file protection.
    auto* readOnlyFileItem = mode->AppendCheckItem(readOnlyFile, _("Read Only &File"), fileProtectionHelp);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto error = document_.ToggleReadOnlyFile();
            UpdateDocumentState();
            UpdateWindowUI(wxUPDATE_UI_RECURSE);
            if (!error.empty())
                wxMessageBox(
                    // TRANSLATORS: IDM_FILE_READONLY_FILE failure; first %s is the path, second %s is the native error.
                    wxString::Format(_("Could not change read-only protection for %s. %s"), document_.Path(), error),
                    // TRANSLATORS: IDM_FILE_READONLY_FILE error-dialog title.
                    _("Could not change file protection"), wxOK | wxICON_ERROR, this);
        },
        readOnlyFile);
    Bind(
        wxEVT_UPDATE_UI,
        [this, readOnlyFileItem, fileProtectionHelp](wxUpdateUIEvent& event) {
            const bool previous = document_.IsReadOnlyFile();
            document_.RefreshReadOnlyFile();
            event.Check(document_.ReadOnlyFileFlag());
            event.Enable(document_.ReadOnlyFileChangeError().empty());
            readOnlyFileItem->SetHelp(
                document_.ReadOnlyFileChangeError().empty() ? fileProtectionHelp : document_.ReadOnlyFileChangeError());
            if (previous != document_.IsReadOnlyFile()) UpdateDocumentState();
        },
        readOnlyFile);
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
        std::to_underlying(AppCommand::SaveCopy));
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, std::to_underlying(AppCommand::SaveCopy));
    auto* endings = new wxMenu;
    AppendLineEndingCommands(endings);
    file->AppendSubMenu(endings, _("Line Endin&gs"));
    file->AppendSeparator();
    // TRANSLATORS: Opens paper, margin and print style settings (IDM_FILE_PAGESETUP).
    file->Append(wxID_PAGE_SETUP, _("Page Se&tup..."));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            EditorPrinting::PageSetup(*this, (settings_ ? settings_->values : defaults_).printing, printerData_);
        },
        wxID_PAGE_SETUP);
    // TRANSLATORS: Opens the native print dialog for the current buffer (IDM_FILE_PRINT).
    file->Append(wxID_PRINT, _("&Print...\tCtrl+P"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            EditorPrinting::Print(*this, *editor_, document_.FileNameText(FileNamePart::FileName),
                                  (settings_ ? settings_->values : defaults_).printing, printerData_);
        },
        wxID_PRINT);
    file->AppendSeparator();
    const int containingFolder = wxWindow::NewControlId();
    // TRANSLATORS: Open the current document's containing directory in the native file manager.
    file->Append(containingFolder, _("Open Containing &Folder"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!OpenContainingFolder())
                // TRANSLATORS: Native containing-folder launch error and its dialog title; no parameters.
                wxMessageBox(_("Could not open the containing folder."), _("Could not open target"),
                             wxOK | wxICON_ERROR, this);
        },
        containingFolder);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(!document_.Path().empty()); }, containingFolder);
    file->Append(updateTime, _("Update File Time"));
    file->AppendSeparator();
    AppendRecentFilesCommand(file);
    file->AppendSeparator();
    // TRANSLATORS: IDR_MAINWND IDM_FILE_EXIT; close through the existing unsaved-document workflow.
    file->Append(wxID_EXIT, _("E&xit\tAlt+F4"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
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

void EditorFrame::AppendLineEndingCommands(wxMenu* edit) {
    const auto addEol = [this, edit](int id, const wxString& label, Scintilla::EndOfLine mode) {
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
    addEol(std::to_underlying(AppCommand::EolCrLf), _("&Windows (CR+LF)"), Scintilla::EndOfLine::CrLf);
    addEol(std::to_underlying(AppCommand::EolLf), _("&Unix/macOS (LF)"), Scintilla::EndOfLine::Lf);
    addEol(std::to_underlying(AppCommand::EolCr), _("Classic &Mac OS (CR)"), Scintilla::EndOfLine::Cr);
    edit->AppendSeparator();
    const int lineEndingOptions = wxWindow::NewControlId();
    edit->Append(lineEndingOptions, _("&Default..."));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowLineEndingOptions(); }, lineEndingOptions);
}

wxMenu* EditorFrame::CreateLanguageMenu() {
    auto* languages = new wxMenu;
    // TRANSLATORS: IDM_VIEW_SCHEME / IDM_VIEW_SCHEME_FAVORITE, before code-style options.
    const int selectScheme = std::to_underlying(AppCommand::SyntaxScheme);
    languages->Append(selectScheme, _("&Syntax Scheme...\tF12"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
#ifdef __WXGTK__
            // GTK's window key hash ignores consumed modifiers. Wayland can
            // mark Alt on F12 as consumed and select this bare-F12 accelerator.
            // Preserve the actual event state and dispatch the exact registered
            // accelerator, including Alt+F12 font and Shift+F12 inheritance.
            const std::unique_ptr<GdkEvent, decltype(&gdk_event_free)> key(gtk_get_current_event(), gdk_event_free);
            if (key && key->type == GDK_KEY_PRESS && key->key.keyval == GDK_KEY_F12) {
                const auto modifiers =
                    static_cast<GdkModifierType>(key->key.state & gtk_accelerator_get_default_mod_mask());
                if (modifiers != 0) {
                    gtk_accel_groups_activate(G_OBJECT(GetHandle()), GDK_KEY_F12, modifiers);
                    return;
                }
            }
#endif
            ShowSyntaxScheme();
        },
        selectScheme);
    const int customize = std::to_underlying(AppCommand::CustomizeSchemes);
    languages->Append(customize, _("&Customize Schemes...\tCtrl+F12"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CustomizeStyles(); }, customize);
    const int favoriteSchemes = wxWindow::NewControlId();
    languages->Append(favoriteSchemes, _("&Favorite Schemes..."));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowSyntaxScheme(true); }, favoriteSchemes);
    languages->AppendSeparator();
    const int useCode = wxWindow::NewControlId();
    languages->AppendCheckItem(useCode, _("Use Default &Code Style	Shift+F12"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& fonts = (settings_ ? settings_->values : defaults_).fonts;
            auto& scheme = fonts.Scheme(document_.Language());
            scheme.useCode = !scheme.useCode;
            editor_->SetFontSettings(fonts);
            ApplySyntaxLanguage(*editor_, document_.Language(), document_.EffectiveLanguage());
            UpdatePositionStatus();
            UpdateWindowUI(wxUPDATE_UI_RECURSE);
        },
        useCode);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Check((settings_ ? settings_->values : defaults_).fonts.Scheme(document_.Language()).useCode);
        },
        useCode);
    auto* themes = new wxMenu;
    for (const auto& [theme, label] :
         {std::pair{StyleTheme::Default, _("&Default")}, std::pair{StyleTheme::Dark, _("Dar&k")}}) {
        const int id = wxWindow::NewControlId();
        themes->AppendCheckItem(id, label);
        Bind(
            wxEVT_MENU,
            [this, theme](wxCommandEvent&) {
                if (!settings_ || settings_->values.fonts.theme == theme) return;
                auto& preferences = settings_->values;
                const bool saved = settings_->SwitchStyleTheme(theme);
                editor_->SetFontSettings(preferences.fonts);
                ApplySyntaxStyles(*editor_, document_.Language());
                UpdatePositionStatus();
                UpdateWindowUI(wxUPDATE_UI_RECURSE);
                if (!saved)
                    wxMessageBox(wxString::Format(_("Could not write %s"), settings_->Path()),
                                 _("Could not save settings"), wxOK | wxICON_ERROR, this);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, theme](wxUpdateUIEvent& event) {
                event.Check((settings_ ? settings_->values : defaults_).fonts.theme == theme);
            },
            id);
    }
    languages->AppendSubMenu(themes, _("St&yle Theme"));
    languages->AppendSeparator();
    const auto addLanguage = [this](wxMenu* menu, const wxString& label, std::optional<SyntaxLanguage> language) {
        const int id = wxWindow::NewControlId();
        // Notepad4 checks the active scheme across all families. Native radio
        // groups are local to each submenu and would select inactive families.
        menu->AppendCheckItem(id, label);
        Bind(
            wxEVT_MENU,
            [this, language](wxCommandEvent&) {
                if (language == SyntaxLanguage::Csv)
                    ChooseCsvLanguage();
                else
                    ChooseLanguage(language);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, language](wxUpdateUIEvent& event) { event.Check(document_.LanguageOverride() == language); }, id);
        return id;
    };
    automaticLanguageId_ = addLanguage(languages, _("&Automatic"), std::nullopt);
    for (const auto& group : SyntaxMenuFamilies()) {
        const auto members = SyntaxMenuMembers(group.family);
        if (members.empty()) continue;
        auto* family = new wxMenu;
        for (const auto* definition : members)
            addLanguage(family, "&" + wxGetTranslation(definition->name), definition->language);
        languages->AppendSubMenu(family, wxGetTranslation(group.label));
    }
    languages->AppendSeparator();
    for (const auto* definition : SyntaxMenuMembers(SyntaxMenuFamily::Standalone))
        addLanguage(languages,
                    "&" + wxGetTranslation(definition->name) +
                        (definition->language == SyntaxLanguage::Csv ? "..." : ""),
                    definition->language);
    return languages;
}

void EditorFrame::ToggleReadOnlyMode() {
    editor_->Send(Scintilla::Message::SetReadOnly, !editor_->Send(Scintilla::Message::GetReadOnly));
    UpdateDocumentState();
    UpdateWindowUI(wxUPDATE_UI_RECURSE);
}
