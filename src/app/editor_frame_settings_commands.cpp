#include "app/editor_frame.h"
#include "app/command_ids.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include <utility>
#include <wx/menu.h>
#include <wx/translation.h>
#include "app/dialogs/auto_completion_settings_dialog.h"
#include "app/dialogs/auto_save_settings_dialog.h"
#include "app/dialogs/long_line_settings_dialog.h"
#include "app/dialogs/tab_settings_dialog.h"
#include "app/status_fields.h"
#include "syntax/syntax_scheme.h"
#include <memory>
#include <wx/msgdlg.h>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

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

void EditorFrame::AppendSettingsCommands(wxMenu* view) {
    if (settings_) {
#ifndef __WXMSW__
        view->AppendSeparator();
#endif
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
                if (SaveSettings(true)) {
                    SetStatusText(_("Settings saved"), StatusField::Feedback);
                    UpdateStatusWidths();
                } else
                    wxMessageBox(wxString::Format(_("Could not write %s"), settings_->Path()),
                                 _("Could not save settings"), wxOK | wxICON_ERROR, this);
            },
            saveNow);
    }
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
    AppendIndentationOptions(view);
    const int wordWrapSettings = wxWindow::NewControlId();
    view->Append(wordWrapSettings, _("Word Wrap Settings..."));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowWordWrapSettings(); }, wordWrapSettings);
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
    AppendAppearanceOptions(view);
    auto* windowOptions = new wxMenu;
    const int alwaysOnTop = std::to_underlying(AppCommand::AlwaysOnTop);
    // TRANSLATORS: Original IDM_VIEW_ALWAYSONTOP, first in Window Options.
    auto* topmostItem = windowOptions->AppendCheckItem(alwaysOnTop, _("Always On &Top\tAlt+Shift+T"));
    if (!SupportsAlwaysOnTop()) {
        // TRANSLATORS: This desktop backend has no supported client window-stacking interface.
        topmostItem->SetHelp(_("Always On Top is unavailable on this desktop."));
        topmostItem->Enable(false);
        // TRANSLATORS: Explain why the adjacent Always On Top command is disabled.
        windowOptions->Append(wxID_ANY, _("Always On Top is unavailable on this desktop."))->Enable(false);
    }
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ToggleAlwaysOnTop(); }, alwaysOnTop);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Enable(SupportsAlwaysOnTop());
            event.Check(AlwaysOnTopActive());
        },
        alwaysOnTop);
    windowOptions->AppendSeparator();
    const auto appendRouting = [this](wxMenu* menu, const wxString& label, bool reuse) {
        const int id = wxWindow::NewControlId();
        menu->AppendCheckItem(id, label);
        Bind(
            wxEVT_MENU,
            [this, reuse](wxCommandEvent&) {
                auto& values = settings_ ? settings_->values : defaults_;
                const bool nextReuse = reuse ? !values.reuseWindow : values.reuseWindow;
                const bool nextSingle = reuse ? values.singleFileInstance : !values.singleFileInstance;
                if (settings_) {
                    if (!settings_->SaveWindowRouting(nextReuse, nextSingle))
                        // TRANSLATORS: wxnotepad settings-write failure; %s is the literal settings-file pathname.
                        wxMessageBox(wxString::Format(_("Could not write %s"), settings_->Path()),
                                     // TRANSLATORS: wxnotepad title for a settings-file write failure.
                                     _("Could not save settings"), wxOK | wxICON_ERROR, this);
                } else {
                    values.reuseWindow = nextReuse;
                    values.singleFileInstance = nextSingle;
                }
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, reuse](wxUpdateUIEvent& event) {
                const auto& values = settings_ ? settings_->values : defaults_;
                event.Check(reuse ? values.reuseWindow : values.singleFileInstance);
            },
            id);
    };
    // TRANSLATORS: Original IDM_VIEW_REUSEWINDOW menu caption under Window Options.
    appendRouting(windowOptions, _("&Reuse Window"), true);
    // TRANSLATORS: Original Notepad4.rc Window Options submenu under Settings.
    view->AppendSubMenu(windowOptions, _("Window O&ptions"));
    AppendWindowTitleOptions(view);
    view->AppendSeparator();
    // TRANSLATORS: Original IDM_VIEW_SINGLEFILEINSTANCE menu caption under Settings.
    appendRouting(view, _("Single &File Instance"), false);
    const int fileChanges = wxWindow::NewControlId();
    view->AppendCheckItem(fileChanges, _("File &Change Notification...\tAlt+F5"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFileChangeNotificationOptions(); }, fileChanges);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(fileWatchingOptions_.mode != FileWatchingMode::None); },
        fileChanges);
    const int autoSave = wxWindow::NewControlId();
    // TRANSLATORS: IDD_AUTOSAVE settings menu command; configure periodic backup copies.
    view->Append(autoSave, _("AutoSave Settings..."));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& preferences = settings_ ? settings_->values : defaults_;
            if (ShowAutoSaveSettingsDialog(this, preferences.autoSave))
                document_.ConfigureAutoSave(preferences.autoSave);
        },
        autoSave);
    auto escape = std::make_unique<wxMenu>();
    // Notepad4 89b8b36 IDM_VIEW_NOESCFUNC..IDM_VIEW_ESCEXIT, after AutoSave.
    for (const auto& [label, action] : {std::pair{wxTRANSLATE("&None"), EscapeFunction::None},
                                        std::pair{wxTRANSLATE("&Minimize wxnotepad"), EscapeFunction::Minimize},
                                        std::pair{wxTRANSLATE("E&xit wxnotepad"), EscapeFunction::Exit}}) {
        const int id = wxWindow::NewControlId();
        escape->AppendRadioItem(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, action](wxCommandEvent&) {
                (settings_ ? settings_->values : defaults_).escapeFunction = action;
                UpdateWindowUI(wxUPDATE_UI_RECURSE);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, action](wxUpdateUIEvent& event) {
                event.Check((settings_ ? settings_->values : defaults_).escapeFunction == action);
            },
            id);
    }
    const wxString escapeLabel = _("Extra Esc &Key Function");
    // Hand submenu ownership to wx only after all caller-side setup is complete.
    view->AppendSubMenu(escape.release(), escapeLabel);
    if (settings_) {
        auto* other = new wxMenu;
        const int saveRecent = wxWindow::NewControlId();
        other->AppendCheckItem(saveRecent, _("Remember &Recent Files"));
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) { settings_->values.saveRecentFiles = !settings_->values.saveRecentFiles; },
            saveRecent);
        Bind(
            wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(settings_->values.saveRecentFiles); },
            saveRecent);
        const int saveFind = wxWindow::NewControlId();
        other->AppendCheckItem(saveFind, _("Remember &Search Options"));
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) { settings_->values.saveFindReplace = !settings_->values.saveFindReplace; },
            saveFind);
        Bind(
            wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(settings_->values.saveFindReplace); },
            saveFind);
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
    auto* qualityMenu = new wxMenu;
    for (const auto& [label, quality] :
         {// TRANSLATORS: Original IDM_VIEW_FONTQUALITY_DEFAULT; default Windows font antialiasing.
          std::pair{wxTRANSLATE("&Default"), Scintilla::FontQuality::QualityDefault},
          // TRANSLATORS: Original IDM_VIEW_FONTQUALITY_NONE; disable Windows font antialiasing.
          std::pair{wxTRANSLATE("&None"), Scintilla::FontQuality::QualityNonAntialiased},
          // TRANSLATORS: Original IDM_VIEW_FONTQUALITY_STANDARD; grayscale Windows font antialiasing.
          std::pair{wxTRANSLATE("&Standard"), Scintilla::FontQuality::QualityAntialiased},
          // TRANSLATORS: Original IDM_VIEW_FONTQUALITY_CLEARTYPE; ClearType is the Windows rendering-mode name.
          std::pair{wxTRANSLATE("&ClearType"), Scintilla::FontQuality::QualityLcdOptimized}}) {
        const int id = wxWindow::NewControlId();
        qualityMenu->AppendRadioItem(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, quality](wxCommandEvent&) {
                editor_->Send(Scintilla::Message::SetFontQuality, std::to_underlying(quality));
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, quality](wxUpdateUIEvent& event) {
                event.Check(editor_->Send(Scintilla::Message::GetFontQuality) == std::to_underlying(quality));
            },
            id);
    }
    // TRANSLATORS: Original Notepad4.rc Font Quality submenu under Settings / Advanced Settings.
    advanced->AppendSubMenu(qualityMenu, _("Font &Quality"));
    view->AppendSubMenu(advanced, _("A&dvanced Settings"));
#endif
    AppendSettingsCommands(view);
    return view;
}

void EditorFrame::AppendWindowTitleOptions(wxMenu* settings) {
    auto title = std::make_unique<wxMenu>();
    const auto appendChoice = [menu = title.get()](int id, const wxString& label) {
        auto* item = menu->AppendCheckItem(id, label);
        // Keep the original radio marks, while allowing an already selected
        // excerpt command to run again through GTK's native activate signal.
#ifdef __WXMSW__
        MENUITEMINFOW information{};
        information.cbSize = sizeof(information);
        information.fMask = MIIM_FTYPE;
        information.fType = MFT_RADIOCHECK;
        SetMenuItemInfoW(static_cast<HMENU>(menu->GetHMenu()), static_cast<UINT>(item->GetMSWId()), FALSE,
                         &information);
#elif defined(__WXGTK__)
        gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(item->GetMenuItem()), TRUE);
#endif
        return item;
    };
    for (const auto& [label, mode] :
         {std::pair{wxTRANSLATE("&File Name Only"), TitlePathNameFormat::NameOnly},
          std::pair{wxTRANSLATE("File Name and &Directory"), TitlePathNameFormat::NameFirst},
          std::pair{wxTRANSLATE("Full &Path Name"), TitlePathNameFormat::FullPath}}) {
        const int id = wxWindow::NewControlId();
        // Check items allow choosing the already selected command again on GTK,
        // including recapturing Text Excerpt. Update UI keeps one item checked.
        appendChoice(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, mode](wxCommandEvent&) {
                (settings_ ? settings_->values : defaults_).pathNameFormat = mode;
                titleExcerpt_.clear();
                UpdateDocumentState();
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, mode](wxUpdateUIEvent& event) {
                event.Check(titleExcerpt_.empty() &&
                            (settings_ ? settings_->values : defaults_).pathNameFormat == mode);
            },
            id);
    }
    const int excerpt = wxWindow::NewControlId();
    appendChoice(excerpt, _("&Text Excerpt"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            titleExcerpt_ = editor_->CaptureTitleExcerpt(document_.IsDefaultEncoding());
            UpdateDocumentState();
        },
        excerpt);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(!titleExcerpt_.empty()); }, excerpt);
    settings->AppendSubMenu(title.release(), _("Window Title Displa&y"));
}

void EditorFrame::AppendAppearanceOptions(wxMenu* view) {
    auto* appearance = new wxMenu;
    for (const auto& [label, member] :
         {std::pair{wxTRANSLATE("Show &Menu	Alt+F11"), &OriginalSettings::showMenu},
          std::pair{wxTRANSLATE("Show &Toolbar	Ctrl+F11"), &OriginalSettings::showToolbar},
          std::pair{wxTRANSLATE("Show Stat&usbar	Shift+F11"), &OriginalSettings::showStatusbar}}) {
        const int id = wxWindow::NewControlId();
        appearance->AppendCheckItem(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, member](wxCommandEvent&) {
                auto& preferences = settings_ ? settings_->values : defaults_;
                preferences.*member = !(preferences.*member);
                ApplyBarVisibility();
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, member](wxUpdateUIEvent& event) {
                event.Check((settings_ ? settings_->values : defaults_).*member);
            },
            id);
        if (member == &OriginalSettings::showToolbar) AppendToolbarCommands(appearance);
    }
    appearance->AppendSeparator();
    auto* fullScreen = new wxMenu;
    const auto fullScreenOptions = {
        // TRANSLATORS: Enter full screen when starting the editor (IDM_VIEW_FULLSCREEN_ON_START).
        std::pair{wxTRANSLATE("On &Startup"), FullScreenMode_OnStartup},
        // TRANSLATORS: Hide the title bar in full screen (IDM_VIEW_FULLSCREEN_HIDE_TITLE).
        std::pair{wxTRANSLATE("Hide &Title"), FullScreenMode_HideTitle}};
    for (const auto& [label, bit] : fullScreenOptions) {
        const int id = wxWindow::NewControlId();
        fullScreen->AppendCheckItem(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, bit](wxCommandEvent&) {
                auto& preferences = settings_ ? settings_->values : defaults_;
                preferences.fullScreenMode ^= bit;
#ifdef __WXMSW__
                if (bit == FullScreenMode_HideTitle) RefreshFullScreen();
#endif
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, bit](wxUpdateUIEvent& event) {
                event.Check(((settings_ ? settings_->values : defaults_).fullScreenMode & bit) != 0);
            },
            id);
    }
    // TRANSLATORS: Appearance submenu for full screen preferences (IDM_VIEW_FULLSCREEN_*).
    appearance->AppendSubMenu(fullScreen, _("Full Scree&n Mode"));
    auto* scrollPastLastLine = new wxMenu;
    // Keep Notepad4's full EndAtLastLine integer contract (One Page is zero).
    for (const auto& [label, mode] : {std::pair{wxTRANSLATE("&No"), 1}, std::pair{wxTRANSLATE("&One Page"), 0},
                                      std::pair{wxTRANSLATE("&Half Page"), 2}, std::pair{wxTRANSLATE("1/&3 Page"), 3},
                                      std::pair{wxTRANSLATE("1/&4 Page"), 4}}) {
        const int id = wxWindow::NewControlId();
        scrollPastLastLine->AppendRadioItem(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU, [this, mode](wxCommandEvent&) { editor_->Send(Scintilla::Message::SetEndAtLastLine, mode); },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, mode](wxUpdateUIEvent& event) {
                event.Check(editor_->Send(Scintilla::Message::GetEndAtLastLine) == mode);
            },
            id);
    }
    appearance->AppendSubMenu(scrollPastLastLine, _("Scroll Past &Last Line"));
    const int shrinkScrollWidth = wxWindow::NewControlId();
    appearance->AppendCheckItem(shrinkScrollWidth, _("Auto-Shrink Horizontal Scrollbar"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetAutoShrinkScrollWidth(!editor_->AutoShrinkScrollWidth()); },
        shrinkScrollWidth);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->AutoShrinkScrollWidth()); },
        shrinkScrollWidth);
    appearance->AppendSeparator();
    AppendUILanguageCommands(appearance);
    view->AppendSubMenu(appearance, _("Appea&rance"));
}
