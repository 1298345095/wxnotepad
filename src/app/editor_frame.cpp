#include "app/editor_frame.h"
#ifdef __WXMSW__
#include "app/explorer_selections.h"
#endif
#include "app/app_icon.h"
#include <utility>
#include "syntax/syntax_scheme.h"
#include "editor/editor_control.h"
#include "app/dialogs/document_dialogs.h"
#include "app/dialogs/scheme_chooser.h"
#include "app/dialogs/csv_options_dialog.h"
#include "app/dialogs/style_customization.h"
#include "app/dialogs/find_replace_dialog.h"
#include "app/dialogs/search_feedback_dialog.h"
#include "app/dialogs/recent_files_dialog.h"
#include "settings/settings_store.h"
#include "settings/search_settings.h"
#include "settings/recent_files.h"
#include "diagnostics/trace.h"
#include <wx/menu.h>
#include <wx/toolbar.h>
#include "app/status_fields.h"
#include <wx/fontdlg.h>
#ifdef __WXGTK__
#include <wx/display.h>
#include <wx/fontutil.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#undef None
#endif
#include <wx/msgdlg.h>
#include <wx/statusbr.h>
#include <wx/filename.h>
#include <wx/translation.h>
#include <array>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

namespace {
wxString DocumentEncodingLabel(const DocumentSession& document) {
    const auto encoding = document.Encoding();
    const auto* info = FindFileEncodingInfo(encoding);
    const wxString name = info ? info->displayName : "UTF-8";
    if (document.IsDefaultEncoding()) {
        switch (encoding) {
            using enum FileEncoding;
        case Gbk:
        case Gb18030:
        case Big5:
        case Windows932:
        case Windows949:
            return wxString::Format("ANSI (CP%u)", info->codePage);
        default:
            return "ANSI (" + name + ")";
        }
    }
    if (document.IsOemEncoding()) return "OEM (" + name + ")";
    return name;
}
} // namespace

#ifdef __WXMSW__
struct EditorFrame::FullScreenPlacement {
    WINDOWPLACEMENT position{sizeof(WINDOWPLACEMENT)};
    HMONITOR monitor = nullptr;
    LONG style = 0;
};
#endif

EditorFrame::EditorFrame(SettingsStore* settings)
    : wxFrame(nullptr, wxID_ANY, wxString::Format(_("%s - wxnotepad"), _("Untitled")), wxDefaultPosition,
              wxSize(900, 600)),
      editor_(new EditorControl(this, settings ? settings->values.selectOption : OriginalSettings{}.selectOption)),
      settings_(settings), launchWorkingDirectory_(wxGetCwd()), normalWindowSize_(GetSize()),
      indentation_(settings ? settings->values : defaults_), wordWrap_(*editor_),
      document_(*editor_, settings ? settings->values.encoding : EncodingOptions{},
                settings ? SyntaxDetectionOptions{settings->values.autoSelect, settings->values.noCgiGuess,
                                                  settings->values.noHtmlGuess,
                                                  SyntaxFromOriginalSchemeId(settings->values.defaultScheme)
                                                      .value_or(SyntaxLanguage::PlainText),
                                                  settings->values.schemeExtensions, settings->values.favoriteSchemes}
                         : SyntaxDetectionOptions{},
                settings ? settings->values.autoSave : AutoSaveOptions{}),
      fileWatchingOptions_(settings ? settings->values.fileWatching : FileWatchingOptions{}),
      fileChangeMonitor_(*this, [this] { return HandleFileChange(); }), search_(*editor_) {
    // Establish the single editor child before preferences calculate wrapped geometry.
    Layout();
    const auto& preferences = settings ? settings->values : defaults_;
#ifdef __WXMSW__
    editor_->Send(Scintilla::Message::SetTechnology, preferences.renderingTechnology);
    editor_->Send(Scintilla::Message::SetFontQuality, preferences.fontQuality);
#endif
    Bind(wxEVT_SHOW, [this, firstShow = true](wxShowEvent& event) mutable {
        if (event.IsShown()) {
#ifdef __WXMSW__
            // Recalculate native bar metrics on the shown window before laying out
            // the editor; Layout alone retains the pre-show non-client geometry.
            CallAfter([this] { ApplyBarVisibility(); });
#endif
            const auto fullScreenMode = (settings_ ? settings_->values : defaults_).fullScreenMode;
            if (std::exchange(firstShow, false) && (fullScreenMode & FullScreenMode_OnStartup))
                CallAfter([this] { SetFullScreen(true); });
        }
        event.Skip();
    });
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        if (!FullScreenActive() && !IsMaximized() && !IsIconized()) normalWindowSize_ = GetSize();
        event.Skip();
    });
    editor_->SetFontSettings(preferences.fonts);
    editor_->SetAutoInsertMask(preferences.autoInsertMask);
    editor_->SetCompletionSettings(preferences.completion);
    editor_->SetColorCallTipMode(preferences.showCallTip);
    editor_->SetAutoShrinkScrollWidth(preferences.autoShrinkScrollWidth);
    editor_->Send(Scintilla::Message::SetEndAtLastLine, preferences.endAtLastLine);
    editor_->Occurrences().SetOptions(preferences.markOccurrences);
    editor_->SetCodeFoldingVisible(preferences.showCodeFolding);
    editor_->SetCurrentBlockHighlighted(preferences.highlightCurrentBlock);
    ApplyIndentationGuides(*editor_, document_.Language(), preferences.showIndentGuides,
                           preferences.simpleIndentGuides);
    editor_->Send(Scintilla::Message::SetEdgeColumn,
                  settings ? settings->values.longLinesLimit : defaults_.longLinesLimit);
    if (settings && settings->values.markLongLines) {
        editor_->Send(Scintilla::Message::SetEdgeMode, settings->values.longLineMode);
        ApplyLongLineColour(*editor_);
    }
    indentation_.Apply(*editor_, document_.Language());
    if (settings) {
        document_.SetSaveCleanup({settings->values.fixLineEndings, settings->values.fixTrailingBlanks});
        defaultEolMode_ = settings->values.defaultEOLMode;
        RestoreSearchSettings(search_, settings->values);
        editor_->SetLineSelectionMode(static_cast<LineSelectionMode>(settings->values.lineSelection));
        const auto mode = settings->values.defaultEOLMode == 0   ? Scintilla::EndOfLine::CrLf
                          : settings->values.defaultEOLMode == 1 ? Scintilla::EndOfLine::Lf
                                                                 : Scintilla::EndOfLine::Cr;
        document_.SetDefaultEolMode(mode);
        editor_->Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(std::to_underlying(mode)));
        warnLineEndings_ = settings->values.warnLineEndings;
        editor_->Send(Scintilla::Message::SetZoom, settings->values.zoomLevel);
        editor_->SetCaretAppearance(settings->values.caret);
        editor_->SetCurrentLineHighlightMode(
            static_cast<LineHighlightMode>(settings->values.highlightCurrentLine % 10));
        editor_->Send(Scintilla::Message::SetCaretLineHighlightSubLine, settings->values.highlightCurrentLine >= 10);
        editor_->SetLineNumbersVisible(settings->values.showLineNumbers);
        editor_->SetBookmarkMarginVisible(settings->values.showBookmarkMargin);
        editor_->SetChangeHistoryOptions(settings->values.changeHistoryMarker);
        editor_->SetVisualBraceMatching(settings->values.matchBraces);
        editor_->Send(Scintilla::Message::SetViewEOL, settings->values.viewEOLs);
        wordWrap_.Restore(settings->values);
        editor_->Send(Scintilla::Message::SetViewWS,
                      static_cast<uintptr_t>(std::to_underlying(settings->values.viewWhiteSpace
                                                                    ? Scintilla::WhiteSpace::VisibleAlways
                                                                    : Scintilla::WhiteSpace::Invisible)));
    }
    document_.New();
    indentation_.Apply(*editor_, document_.Language());
    CreateMenus();
    CreateDefaultToolbar();
    SetIcons(ApplicationIcons());
    CreatePositionStatus();
    ApplyBarVisibility();
    search_.SetWrapConfirmation([this](SearchDirection direction) { return SearchMessage(direction); });
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
        UpdatePositionStatus();
        CallAfter([this] { RebuildToolbar(); });
#ifdef __WXMSW__
        if (FullScreenActive()) CallAfter([this] { RefreshFullScreen(); });
#endif
        event.Skip();
    });
#ifdef __WXMSW__
    Bind(wxEVT_MOVE, [this](wxMoveEvent& event) {
        const auto window = static_cast<HWND>(GetHandle());
        if (fullScreenPlacement_ &&
            fullScreenPlacement_->monitor != ::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST))
            CallAfter([this] { RefreshFullScreen(); });
        event.Skip();
    });
#endif
    Bind(wxEVT_EDITOR_STATE_CHANGED, [this](wxCommandEvent&) {
        document_.DocumentChanged();
        UpdateDocumentState();
    });
    editor_->Bind(wxEVT_DROP_FILES, &EditorFrame::OnDropFiles, this);
    Bind(wxEVT_DROP_FILES, &EditorFrame::OnDropFiles, this);
    DragAcceptFiles(true);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        WXNOTE_TRACE("close.request", "\ttarget=%p\tcan_veto=%d\tdirty=%d", static_cast<void*>(this), event.CanVeto(),
                     document_.IsModified());
        if (event.CanVeto() && !ConfirmDiscardChanges(_("Save changes to %s before closing?"))) {
            event.Veto();
            WXNOTE_TRACE("close.veto", "\ttarget=%p", static_cast<void*>(this));
            return;
        }
        SaveSettings(false);
        fileChangeMonitor_.Stop();
        WXNOTE_TRACE("close.destroy-deferred", "\ttarget=%p", static_cast<void*>(this));
        Destroy();
    });
#ifdef WXNOTEPAD_DIAGNOSTICS
    Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent& event) {
        if (event.GetEventObject() == this) WXNOTE_TRACE("frame.destroy", "\ttarget=%p", static_cast<void*>(this));
        event.Skip();
    });
#endif
    UpdateDocumentState();
    editor_->SetFocus();
}

EditorFrame::~EditorFrame() {
#ifdef __WXMSW__
    // wxMSW leaves attached HMENUs for DestroyWindow to free. Restore native
    // ownership even when we hid the bar without detaching wx's accelerators.
    const auto window = static_cast<HWND>(GetHandle());
    if (!::GetMenu(window)) ::SetMenu(window, static_cast<HMENU>(GetMenuBar()->GetHMenu()));
#endif
}

#ifdef __WXMSW__
bool EditorFrame::Destroy() {
    if (explorerSelections_) explorerSelections_->Close();
    return wxFrame::Destroy();
}

bool EditorFrame::MSWTranslateMessage(WXMSG* message) {
    const bool keyDown = message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN;
    const auto handle = static_cast<HWND>(GetHandle());
    const bool windowModeKey = message->wParam == VK_F11 || message->wParam == VK_ESCAPE;
    if (keyDown && windowModeKey && (message->hwnd == handle || ::IsChild(handle, message->hwnd))) {
        const int modifiers = (::GetKeyState(VK_CONTROL) < 0 ? wxMOD_CONTROL : 0) |
                              (::GetKeyState(VK_MENU) < 0 ? wxMOD_ALT : 0) |
                              (::GetKeyState(VK_SHIFT) < 0 ? wxMOD_SHIFT : 0) |
                              ((::GetKeyState(VK_LWIN) < 0 || ::GetKeyState(VK_RWIN) < 0) ? wxMOD_META : 0);
        if (message->wParam == VK_F11) {
            HandleFullScreenKey(modifiers);
            return true;
        }
        if (message->wParam == VK_ESCAPE && HandleEscapeKey(modifiers)) return true;
    }
    // A hidden native menu receives no WM_INITMENU to refresh command state.
    // Refresh command gestures, leaving ordinary character input alone.
    if (!(settings_ ? settings_->values : defaults_).showMenu && keyDown) {
        const bool functionKey = message->wParam >= VK_F1 && message->wParam <= VK_F24;
        const bool modifiedCommand = ::GetKeyState(VK_CONTROL) < 0 || ::GetKeyState(VK_MENU) < 0;
        const bool shiftedEdit =
            (message->wParam == VK_INSERT || message->wParam == VK_DELETE) && ::GetKeyState(VK_SHIFT) < 0;
        if (functionKey || modifiedCommand || shiftedEdit) UpdateWindowUI(wxUPDATE_UI_RECURSE);
    }
    return wxFrame::MSWTranslateMessage(message);
}
#endif

bool EditorFrame::FullScreenActive() const {
#ifdef __WXMSW__
    return fullScreenPlacement_ != nullptr;
#else
    return IsFullScreen();
#endif
}

void EditorFrame::SetFullScreen(bool active) {
    if (active == FullScreenActive()) return;
#ifdef __WXMSW__
    const auto window = static_cast<HWND>(GetHandle());
    if (active) {
        auto placement = std::make_unique<FullScreenPlacement>();
        if (!::GetWindowPlacement(window, &placement->position)) return;
        placement->style = ::GetWindowLongW(window, GWL_STYLE);
        fullScreenPlacement_ = std::move(placement);
        if (::IsZoomed(window)) ::ShowWindow(window, SW_RESTORE);
        RefreshFullScreen();
    } else {
        ::SetWindowLongW(window, GWL_STYLE, fullScreenPlacement_->style);
        const auto& position = fullScreenPlacement_->position;
        if (position.showCmd == SW_SHOWMAXIMIZED) ::ShowWindow(window, SW_RESTORE);
        ::SetWindowPlacement(window, &position);
        ::SetWindowPos(window, nullptr, 0, 0, 0, 0,
                       SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        fullScreenPlacement_.reset();
        if (!IsMaximized() && !IsIconized()) normalWindowSize_ = GetSize();
    }
#else
#ifdef __WXGTK__
    if (active) {
        auto* screen = gtk_widget_get_screen(GTK_WIDGET(GetHandle()));
        if (GDK_IS_X11_DISPLAY(gdk_screen_get_display(screen)) &&
            !gdk_x11_screen_supports_net_wm_hint(screen, gdk_atom_intern_static_string("_NET_WM_STATE_FULLSCREEN"))) {
            const auto bounds = wxDisplay(this).GetGeometry();
            ShowFullScreen(true, 0);
            SetSize(bounds);
            return;
        }
    }
#endif
    ShowFullScreen(active, 0);
#endif
}

#ifdef __WXMSW__
void EditorFrame::RefreshFullScreen() {
    if (!fullScreenPlacement_) return;
    const auto window = static_cast<HWND>(GetHandle());
    MONITORINFO monitor{sizeof(MONITORINFO)};
    const auto currentMonitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (!::GetMonitorInfoW(currentMonitor, &monitor)) return;
    fullScreenPlacement_->monitor = currentMonitor;
    const auto dpi = ::GetDpiForWindow(window);
    const int padding = ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    const int frameWidth = ::GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + padding;
    const int frameHeight = ::GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + padding;
    const auto fullScreenMode = (settings_ ? settings_->values : defaults_).fullScreenMode;
    const bool hideTitle = (fullScreenMode & FullScreenMode_HideTitle) != 0;
    const int topInset = frameHeight + (hideTitle ? ::GetSystemMetricsForDpi(SM_CYCAPTION, dpi) : 0);
    const auto& bounds = monitor.rcMonitor;
    ::SetWindowLongW(window, GWL_STYLE, fullScreenPlacement_->style & ~(WS_THICKFRAME | WS_MAXIMIZE));
    // The off-screen caption inset exceeds the default native tracking height.
    ::SetWindowPos(window, nullptr, bounds.left - frameWidth, bounds.top - topInset,
                   bounds.right - bounds.left + 2 * frameWidth, bounds.bottom - bounds.top + topInset + frameHeight,
                   SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}
#endif

bool EditorFrame::HandleEscapeKey(int modifiers) {
    using enum Scintilla::Message;
    if (wxWindow::FindFocus() != editor_ || editor_->InputMethodComposing()) return false;
    if (modifiers == wxMOD_SHIFT) {
        // Original CMD_SHIFTESC always uses the ordinary application close flow.
        Close();
        return true;
    }
    if (modifiers != wxMOD_NONE) return false;
    // Original CMD_ESCAPE: one press performs only the first eligible action.
    if (editor_->Send(AutoCActive))
        editor_->Send(AutoCCancel);
    else if (editor_->Send(CallTipActive))
        editor_->Send(CallTipCancel);
    else if (FullScreenActive())
        SetFullScreen(false);
    else if (const auto action = (settings_ ? settings_->values : defaults_).escapeFunction;
             action == EscapeFunction::Minimize)
        Iconize();
    else if (action == EscapeFunction::Exit)
        Close();
    return true;
}

void EditorFrame::ApplyBarVisibility() {
    const auto& preferences = settings_ ? settings_->values : defaults_;
#ifdef __WXMSW__
    // Adapt Notepad4 89b8b36 IDM_VIEW_MENU: hide the native HMENU while
    // retaining wx's attached menu and its accelerator table.
    const auto window = static_cast<HWND>(GetHandle());
    ::SetMenu(window, preferences.showMenu ? static_cast<HMENU>(GetMenuBar()->GetHMenu()) : nullptr);
    ::DrawMenuBar(window);
#else
    // Keep the GTK menu's accelerator group attached when its widget is hidden.
    GetMenuBar()->Show(preferences.showMenu);
#endif
    GetToolBar()->Show(preferences.showToolbar);
    GetStatusBar()->Show(preferences.showStatusbar);
    SendSizeEvent();
    Layout();
    UpdateDocumentState();
}

bool EditorFrame::SaveSettings(bool explicitSave) {
    if (!settings_) return true;
#ifdef __WXMSW__
    settings_->values.renderingTechnology = static_cast<int>(editor_->Send(Scintilla::Message::GetTechnology));
    settings_->values.fontQuality = static_cast<int>(editor_->Send(Scintilla::Message::GetFontQuality));
#endif
    settings_->values.markLongLines = editor_->Send(Scintilla::Message::GetEdgeMode) != 0;
    settings_->values.showIndentGuides = editor_->Send(Scintilla::Message::GetIndentationGuides) != 0;
    wordWrap_.Capture(settings_->values, document_.FileVariables().wordWrap);
    settings_->values.endAtLastLine = static_cast<int>(editor_->Send(Scintilla::Message::GetEndAtLastLine));
    settings_->values.autoShrinkScrollWidth = editor_->AutoShrinkScrollWidth();
    settings_->values.showLineNumbers = editor_->LineNumbersVisible();
    settings_->values.showBookmarkMargin = editor_->BookmarkMarginVisible();
    settings_->values.changeHistoryMarker = editor_->ChangeHistoryOptions();
    settings_->values.matchBraces = editor_->VisualBraceMatching();
    settings_->values.showCodeFolding = editor_->CodeFoldingVisible();
    settings_->values.highlightCurrentBlock = editor_->CurrentBlockHighlighted();
    settings_->values.viewWhiteSpace = editor_->Send(Scintilla::Message::GetViewWS) != 0;
    settings_->values.viewEOLs = editor_->Send(Scintilla::Message::GetViewEOL) != 0;
    settings_->values.warnLineEndings = warnLineEndings_;
    settings_->values.defaultEOLMode = defaultEolMode_;
    settings_->values.fixLineEndings = document_.SaveCleanup().fixLineEndings;
    settings_->values.fixTrailingBlanks = document_.SaveCleanup().fixTrailingBlanks;
    settings_->values.fileWatching = fileWatchingOptions_;
    settings_->values.zoomLevel = static_cast<int>(editor_->Send(Scintilla::Message::GetZoom));
    settings_->values.caret = editor_->GetCaretAppearance();
    settings_->values.highlightCurrentLine =
        std::to_underlying(editor_->GetCurrentLineHighlightMode()) +
        10 * static_cast<int>(editor_->Send(Scintilla::Message::GetCaretLineHighlightSubLine));
    CaptureSearchSettings(search_, settings_->values);
    settings_->values.lineSelection = std::to_underlying(editor_->GetLineSelectionMode());
    settings_->values.markOccurrences = editor_->Occurrences().Options();
    return settings_->Save(explicitSave);
}

void EditorFrame::ShowLineEndingOptions() {
    using enum Scintilla::EndOfLine;
    LineEndingOptionsDialog dialog(this, {defaultEolMode_, warnLineEndings_, document_.SaveCleanup()});
    const auto choice = dialog.Choose();
    if (!choice) return;
    defaultEolMode_ = choice->defaultMode;
    auto defaultMode = Cr;
    if (defaultEolMode_ == 0)
        defaultMode = CrLf;
    else if (defaultEolMode_ == 1)
        defaultMode = Lf;
    document_.SetDefaultEolMode(defaultMode);
    warnLineEndings_ = choice->warn;
    document_.SetSaveCleanup(choice->cleanup);
}

void EditorFrame::ShowFind(bool replace) {
    if (!findDialog_)
        findDialog_ = new FindReplaceDialog(
            this, *editor_, search_, [this](const FindResult& result) { ReportFind(result); },
            [this](wxKeyEvent& event) { OnSearchKey(event); },
            settings_ ? settings_->values.selectOption : SelectOption_Default);
    findDialog_->Present(replace, settings_ ? settings_->values.selectOption : SelectOption_Default);
}

void EditorFrame::FindNext() {
    Search(true);
}

void EditorFrame::FindPrevious() {
    Search(false);
}

void EditorFrame::FindSelection(SearchDirection direction) {
    if (const auto result = search_.FindSelection(direction)) ReportFind(*result);
}

void EditorFrame::ReplaceNext() {
    if (editor_->Send(Scintilla::Message::GetLength) == 0) return;
    if (!search_.ReplacementInitialized()) {
        ShowFind(true);
        return;
    }
    if (const auto result = search_.ReplaceNext(); result.next) ReportFind(*result.next);
}

void EditorFrame::Search(bool forward, bool extendSelection) {
    if (editor_->Send(Scintilla::Message::GetLength) == 0) return;
    if (!search_.HasFindText()) {
        const auto options = settings_ ? settings_->values.selectOption : SelectOption_Default;
        if (!(options & SelectOption_CopySelectionAsFindText) || !search_.SaveSelectionAsFindText(false)) {
            ShowFind();
            return;
        }
    }
    ReportFind(search_.Find(forward ? SearchDirection::Forward : SearchDirection::Backward, extendSelection));
}

void EditorFrame::ReportFind(const FindResult& result) {
    switch (result.status) {
        using enum FindStatus;
    case Found:
        SetStatusText(result.wrapped ? _("Search wrapped") : _("Match found"), StatusField::Feedback);
        break;
    case NotFound:
        SetStatusText(_("No match found"), StatusField::Feedback);
        ShowSearchMessage("MsgNotFound", _("The specified text was not found."), false);
        break;
    case InvalidPattern:
        SetStatusText(_("Invalid regular expression"), StatusField::Feedback);
        break;
    case EmptyQuery:
        ShowSearchMessage("MsgNotFound", _("The specified text was not found."), false);
        break;
    case Cancelled:
        break;
    }
    UpdateStatusWidths();
}

bool EditorFrame::SearchMessage(SearchDirection direction) {
    return direction == SearchDirection::Forward
               ? ShowSearchMessage("MsgFindWrap1",
                                   _("Reached the end of the document, restarting search at the beginning."), true)
               : ShowSearchMessage("MsgFindWrap2",
                                   _("Reached the beginning of the document, restarting search at the end."), true);
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
    SearchFeedbackDialog dialog(owner, key, message, confirm, mode != 2);
    const auto choice = dialog.Choose();
    if (choice.suppress) {
        mode = 1;
        if (settings_) settings_->SuppressMessage(key);
    }
    if (focused && focused->IsShownOnScreen()) focused->SetFocus();
    return choice.accepted;
}

void EditorFrame::UpdateDocumentState() {
    ApplyUnicodeControlCharacter();
    const wxFileName path(document_.Path());
    const auto format = (settings_ ? settings_->values : defaults_).pathNameFormat;
    wxString name = _("Untitled");
    if (!titleExcerpt_.empty()) {
        // TRANSLATORS: Original IDS_TITLEEXCERPT quotation marks around a captured selection.
        name = wxString::Format(_("\"%s\""), titleExcerpt_);
    } else if (!document_.Path().empty()) {
        name = format == TitlePathNameFormat::FullPath || path.GetFullName().empty() ? document_.Path()
                                                                                     : path.GetFullName();
        if (format == TitlePathNameFormat::NameFirst && !path.GetFullName().empty())
            name += " [" + path.GetPath() + "]";
    }
    const wxString protection = document_.IsReadOnlyFile()                       ? _(" (Read Only File)")
                                : editor_->Send(Scintilla::Message::GetReadOnly) ? _(" (Read Only Mode)")
                                                                                 : wxString{};
    // TRANSLATORS: Window title arguments: modified marker ("* " or empty), document name/path or untitled name,
    // and optional read-only file/mode annotation.
    SetTitle(wxString::Format(_("%s%s%s - wxnotepad"), document_.IsModified() ? "* " : "", name, protection));
    const wxString encoding = DocumentEncodingLabel(document_);
    SetStatusText(encoding + (document_.HasBom() ? " (BOM)" : ""), StatusField::Encoding);
    UpdatePositionStatus();
}

void EditorFrame::ShowSyntaxScheme(bool favorite) {
    auto& preferences = settings_ ? settings_->values : defaults_;
    const auto language = ChooseSyntaxScheme(this, document_.Language(), preferences, favorite);
    document_.SetSyntaxDetectionOptions(
        {preferences.autoSelect, preferences.noCgiGuess, preferences.noHtmlGuess,
         SyntaxFromOriginalSchemeId(preferences.defaultScheme).value_or(SyntaxLanguage::PlainText),
         preferences.schemeExtensions, preferences.favoriteSchemes});
    if (language == SyntaxLanguage::Csv) {
        if (document_.Language() != SyntaxLanguage::Csv) ChooseCsvLanguage();
    } else if (language)
        ChooseLanguage(*language);
}

void EditorFrame::CustomizeStyles() {
    auto& preferences = settings_ ? settings_->values : defaults_;
    CustomizeSyntaxStyles(this, document_.Language(), preferences, settings_, [this, &preferences] {
        editor_->SetFontSettings(preferences.fonts);
        ApplySyntaxStyles(*editor_, document_.Language());
        UpdatePositionStatus();
    });
    editor_->SetFontSettings(preferences.fonts);
    document_.SetSyntaxDetectionOptions(
        {preferences.autoSelect, preferences.noCgiGuess, preferences.noHtmlGuess,
         SyntaxFromOriginalSchemeId(preferences.defaultScheme).value_or(SyntaxLanguage::PlainText),
         preferences.schemeExtensions, preferences.favoriteSchemes});
    editor_->SetFocus();
}

void EditorFrame::ChooseCsvLanguage() {
    auto options = document_.CsvOptions();
    const bool changed = ShowCsvOptionsDialog(this, options);
    if (changed) document_.SetCsvOptions(options);
    // Original cancel/no-change still selects CSV when changing schemes, while
    // the active CSV scheme keeps its current selection/override state.
    if (document_.Language() != SyntaxLanguage::Csv)
        ChooseLanguage(SyntaxLanguage::Csv);
    else if (changed) {
        UpdateDocumentState();
        UpdateWindowUI(wxUPDATE_UI_RECURSE);
    }
    editor_->SetFocus();
}

void EditorFrame::ChooseLanguage(std::optional<SyntaxLanguage> language) {
    document_.SelectLanguage(language);
    indentation_.Apply(*editor_, document_.Language());
    UpdateDocumentState();
    UpdateWindowUI(wxUPDATE_UI_RECURSE);
}

SaveResult EditorFrame::SaveFirst(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss,
                                  SaveFileType fileType, SaveTimeMode timeMode) {
    const auto result = document_.SaveFirst(
        destination, decideLoss ? decideLoss : [this] { return ConfirmEncodingLoss(); }, fileType, timeMode);
    if (result.status == SaveStatus::Saved) {
        titleExcerpt_.clear();
        RecordRecentFile();
        indentation_.Apply(*editor_, document_.Language());
        RefreshFileWatching();
    }
    UpdateWindowUI(wxUPDATE_UI_RECURSE);
    UpdateDocumentState();
    return result;
}

SaveResult EditorFrame::Save(const EncodingLossDecision& decideLoss, SaveTimeMode timeMode) {
    if (!document_.Path().empty()) {
        if (document_.RefreshReadOnlyFile()) {
            UpdateDocumentState();
            if (wxMessageBox(wxString::Format(_("\"%s\" is read only. Save to a different file?"), document_.Path()),
                             _("Read only file"), wxYES_NO | wxICON_WARNING, this) != wxYES)
                return {SaveStatus::Cancelled, {}};
            const auto chosen = ChooseSaveFile(this, _("Save As"), document_.Language(), document_.Path(), {},
                                               (settings_ ? settings_->values : defaults_).favoriteSchemes,
                                               (settings_ ? settings_->values : defaults_).schemeExtensions);
            if (!chosen) return SaveAs(std::nullopt, decideLoss, SaveType::CurrentScheme, timeMode);
            return SaveAs(chosen->path, decideLoss, chosen->type, timeMode);
        }
        const auto result =
            document_.Save(decideLoss ? decideLoss : [this] { return ConfirmEncodingLoss(); }, timeMode);
        if (result.status == SaveStatus::Saved) {
            RecordRecentFile();
            RefreshFileWatching();
        }
        UpdateDocumentState();
        return result;
    }
    const auto chosen = ChooseSaveFile(this, _("Save file"), document_.Language(), document_.Path(), {},
                                       (settings_ ? settings_->values : defaults_).favoriteSchemes,
                                       (settings_ ? settings_->values : defaults_).schemeExtensions);
    if (!chosen) return SaveFirst(std::nullopt, decideLoss, SaveType::CurrentScheme, timeMode);
    return SaveFirst(chosen->path, decideLoss, chosen->type, timeMode);
}

SaveResult EditorFrame::SaveAs(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss,
                               SaveFileType fileType, SaveTimeMode timeMode) {
    const auto result = document_.SaveAs(
        destination, decideLoss ? decideLoss : [this] { return ConfirmEncodingLoss(); }, fileType, timeMode);
    if (result.status == SaveStatus::Saved) {
        titleExcerpt_.clear();
        RecordRecentFile();
        indentation_.Apply(*editor_, document_.Language());
        if (fileWatchingOptions_.resetOnNewFile) fileWatchingOptions_.mode = FileWatchingMode::None;
        RefreshFileWatching();
    }
    UpdateWindowUI(wxUPDATE_UI_RECURSE);
    UpdateDocumentState();
    return result;
}

bool EditorFrame::UpdateFileTime() {
    const bool updated = document_.UpdateFileTime();
    if (updated) RefreshFileWatching();
    return updated;
}

SaveResult EditorFrame::SaveAs() {
    const auto chosen = ChooseSaveFile(this, _("Save As"), document_.Language(), document_.Path(), {},
                                       (settings_ ? settings_->values : defaults_).favoriteSchemes,
                                       (settings_ ? settings_->values : defaults_).schemeExtensions);
    if (!chosen) return SaveAs(std::nullopt);
    return SaveAs(chosen->path, {}, chosen->type);
}

SaveResult EditorFrame::SaveCopy(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss) {
    const auto result =
        document_.SaveCopy(destination, decideLoss ? decideLoss : [this] { return ConfirmEncodingLoss(); });
    if (result.status == SaveStatus::Saved && destination) lastSaveCopyDirectory_ = wxFileName(*destination).GetPath();
    UpdateWindowUI(wxUPDATE_UI_RECURSE);
    UpdateDocumentState();
    return result;
}

SaveResult EditorFrame::SaveCopy() {
    wxString directory = lastSaveCopyDirectory_;
    wxString filename;
    if (document_.Path().empty() && directory.empty())
        directory = wxGetCwd();
    else if (document_.Path().empty()) {
        // Original FileSave combines an empty Untitled basename with the last
        // copy directory; FileDialog::Show then treats its last component as
        // the filename and adds the current scheme's default extension.
        wxFileName initial(directory);
        if (!initial.GetFullName().empty() && !initial.HasExt()) {
            const auto extensions =
                SyntaxExtensionList(document_.Language(), (settings_ ? settings_->values : defaults_).schemeExtensions);
            if (!extensions.empty()) initial.SetExt(extensions.front());
        }
        directory = initial.GetPath();
        filename = initial.GetFullName();
    }
    const auto chosen =
        ChooseSaveFile(this, _("Save Copy"), document_.Language(), document_.Path(), {directory, filename},
                       (settings_ ? settings_->values : defaults_).favoriteSchemes,
                       (settings_ ? settings_->values : defaults_).schemeExtensions);
    return SaveCopy(chosen ? std::optional<wxString>(chosen->path) : std::nullopt);
}

OpenResult EditorFrame::Open(const std::optional<wxString>& source, std::optional<FileEncoding> encoding) {
    const DocumentLoad::SourceDecoding decoding =
        encoding ? DocumentLoad::SourceDecoding(*encoding) : DocumentLoad::SourceDecoding{};
    return LoadFile(DocumentLoad::Open{source, decoding, [this] {
                                           return ConfirmDiscardChanges(
                                               _("Save changes to %s before opening another file?"));
                                       }});
}

OpenResult EditorFrame::LoadFile(const DocumentLoad::Intent& intent) {
    const wxFileName previous(document_.Path());
    wordWrap_.RememberBeforeLoad(document_.FileVariables().wordWrap);
    const auto result = document_.Load(intent, [this] {
        indentation_.Load(*editor_, document_.Language(), document_.FileVariables());
        wordWrap_.ApplyFileOverride(document_.FileVariables().wordWrap);
        const auto& preferences = settings_ ? settings_->values : defaults_;
        editor_->Send(Scintilla::Message::SetEdgeColumn,
                      document_.FileVariables().longLinesLimit.value_or(preferences.longLinesLimit));
    });
    if (result.status == OpenStatus::Opened) {
        if (!previous.SameAs(wxFileName(document_.Path()))) titleExcerpt_.clear();
        if (std::holds_alternative<DocumentLoad::Open>(intent) && fileWatchingOptions_.resetOnNewFile)
            fileWatchingOptions_.mode = FileWatchingMode::None;
        RefreshFileWatching();
        RecordRecentFile();
        wrapColumn_ = 0;
        if (result.lineEndings.Mixed() && warnLineEndings_)
            ConfirmLineEndingConversion(result.lineEndings,
                                        document_.Language() == SyntaxLanguage::Diff ||
                                            (result.unknownLanguage && document_.IsDefaultEncoding()));
        UpdateWindowUI(wxUPDATE_UI_RECURSE);
    }
    UpdateDocumentState();
    return result;
}

void EditorFrame::RecordRecentFile() {
    if (settings_)
        settings_->AddRecentFile(document_.Path());
    else
        RecentFiles::Add(defaults_.recentFiles, document_.Path(), RecentFiles::MinimumCapacity);
}

void EditorFrame::ShowRecentFiles() {
    auto& preferences = settings_ ? settings_->values : defaults_;
    if (preferences.recentFiles.empty() || !ConfirmDiscardChanges(_("Save changes to %s before opening recent files?")))
        return;
    RecentFilesDialog dialog(
        this, preferences,
        [this] {
            if (settings_)
                settings_->ClearRecentFiles(document_.Path());
            else {
                defaults_.recentFiles.clear();
                RecordRecentFile();
            }
        },
        [this](size_t index) {
            if (settings_)
                settings_->RemoveRecentFile(index);
            else
                defaults_.recentFiles.erase(defaults_.recentFiles.begin() + static_cast<std::ptrdiff_t>(index));
        });
    const auto selected = dialog.Choose();
    if (selected) {
        const auto result = LoadFile(DocumentLoad::Open{selected});
        if (result.status == OpenStatus::Failed)
            wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, this);
    }
    UpdateWindowUI(wxUPDATE_UI_RECURSE);
}

OpenResult EditorFrame::ReloadCurrentFile(bool followEnd) {
    if (document_.Path().empty()) return {OpenStatus::Cancelled, {}};
    const auto result = LoadFile(DocumentLoad::Reload{});
    if (result.status == OpenStatus::Opened && followEnd) editor_->Send(Scintilla::Message::DocumentEnd);
    return result;
}

OpenResult EditorFrame::Revert() {
    if (document_.Path().empty()) return {OpenStatus::Cancelled, {}};
    if (document_.IsModified() && wxMessageBox(_("Revert file to last saved state? Your changes will be lost!"),
                                               _("Revert"), wxOK | wxCANCEL | wxICON_WARNING, this) != wxOK)
        return {OpenStatus::Cancelled, {}};
    return ReloadCurrentFile();
}

OpenResult EditorFrame::ReloadWithEncoding() {
    using enum OpenStatus;
    if (document_.Path().empty()) return {Cancelled, {}};
    // F8's original DontSave path confirms loss before showing the picker.
    if (document_.IsModified() &&
        wxMessageBox(_("Recoding requires reloading file from disk, unsaved changes will be lost!"),
                     _("Reload Encoding"), wxOK | wxCANCEL | wxICON_WARNING, this) != wxOK)
        return {Cancelled, {}};
    // Original IDM_RECODE_SELECT maps the BOM variants to their source codec.
    // Native ANSI maps to CPI_NONE: the user must select an explicit source.
    const auto encoding = ChooseReloadEncoding(this, document_.IsDefaultEncoding() || document_.IsOemEncoding()
                                                         ? std::nullopt
                                                         : std::optional<FileEncoding>(document_.Encoding()));
    if (!encoding) return {Cancelled, {}};
    return LoadFile(DocumentLoad::ReloadWithEncoding{*encoding});
}

OpenResult EditorFrame::ReloadAsUtf8() {
    if (document_.Path().empty()) return {OpenStatus::Cancelled, {}};
    // Original CMD_RELOADUTF8 uses FileLoad(Reload), including FileSave(Ask)
    // before file I/O, with the view captured before Save cleanup can move it.
    return LoadFile(DocumentLoad::ReloadAsUtf8{
        [this] { return ConfirmDiscardChanges(_("Save changes to %s before reloading?")); }});
}

OpenResult EditorFrame::ReloadQuickEncoding(QuickReloadEncoding encoding) {
    return LoadFile(DocumentLoad::ReloadQuickEncoding{
        encoding, [this] { return ConfirmDiscardChanges(_("Save changes to %s before reloading?")); }});
}

OpenResult EditorFrame::Open() {
    OpenFileDialog dialog(this, document_.Language(), document_.Path(),
                          (settings_ ? settings_->values : defaults_).favoriteSchemes,
                          (settings_ ? settings_->values : defaults_).schemeExtensions);
    const auto result = Open(dialog.Choose());
    // Original FileLoad determines excerpt retention before the chooser supplies
    // its path. A successful chooser clears it even for the current file.
    if (result.status == OpenStatus::Opened) {
        titleExcerpt_.clear();
        UpdateDocumentState();
    }
    return result;
}

void EditorFrame::OnDropFiles(wxDropFilesEvent& event) {
    if (event.GetNumberOfFiles() == 0) return;
    auto path = event.GetFiles()[0];
#ifdef __WXMSW__
    // Original MsgDropFiles: Visual Studio supplies {UUID}|project|path.
    const auto separator = path.find_last_of('|');
    if (separator != wxString::npos) path = path.Mid(separator + 1);
#endif
    if (IsIconized()) Iconize(false);
    std::optional<wxString> source = path;
    if (wxDirExists(path)) {
        OpenFileDialog dialog(this, document_.Language(), document_.Path(),
                              (settings_ ? settings_->values : defaults_).favoriteSchemes,
                              (settings_ ? settings_->values : defaults_).schemeExtensions);
        dialog.SetDirectory(path);
        source = dialog.Choose();
    }
    const auto result = Open(source);
    if (result.status == OpenStatus::Failed)
        wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, this);
}

OpenResult EditorFrame::OpenAs(FileEncoding encoding) {
    OpenFileDialog dialog(this, document_.Language(), document_.Path(),
                          (settings_ ? settings_->values : defaults_).favoriteSchemes,
                          (settings_ ? settings_->values : defaults_).schemeExtensions);
    return Open(dialog.Choose(), encoding);
}

bool EditorFrame::NewDocument() {
    if (!ConfirmDiscardChanges(_("Save changes to %s before creating a new document?"))) return false;
    wordWrap_.RememberBeforeLoad(document_.FileVariables().wordWrap);
    document_.New();
    titleExcerpt_.clear();
    wordWrap_.ApplyFileOverride(std::nullopt);
    if (fileWatchingOptions_.resetOnNewFile) fileWatchingOptions_.mode = FileWatchingMode::None;
    RefreshFileWatching();
    editor_->Send(Scintilla::Message::SetEdgeColumn,
                  settings_ ? settings_->values.longLinesLimit : defaults_.longLinesLimit);
    wrapColumn_ = 0;
    indentation_.Reset(*editor_, document_.Language());
    GetMenuBar()->Check(automaticLanguageId_, true);
    UpdateDocumentState();
    return true;
}

bool EditorFrame::ConfirmDiscardChanges(const wxString& prompt) {
    if (!document_.IsModified()) return true;
    if (CanDiscardUntitledWhitespace()) return true;
    const wxFileName path(document_.Path());
    const wxString name = document_.Path().empty() ? _("Untitled") : path.GetFullName() + " [" + path.GetPath() + "]";
    wxMessageDialog dialog(this, wxString::Format(prompt, name), _("Save changes?"),
                           wxYES_NO | wxCANCEL | wxICON_QUESTION);
    dialog.SetYesNoCancelLabels(_("&Save"), _("&Discard"), _("&Cancel"));
    WXNOTE_TRACE("close.confirm", "\ttarget=%p\tstage=show-request\tdirty=1\tlength=%lld", static_cast<void*>(this),
                 static_cast<long long>(editor_->Send(Scintilla::Message::GetLength)));
    const int choice = dialog.ShowModal();
    WXNOTE_TRACE("close.choice", "\ttarget=%p\tchoice=%s", static_cast<void*>(this),
                 choice == wxID_YES  ? "Save"
                 : choice == wxID_NO ? "Discard"
                                     : "Cancel");
    if (choice == wxID_NO) {
        document_.StopAutoSave(false);
        return true;
    }
    if (choice != wxID_YES) return false;
    const auto result = Save();
    WXNOTE_TRACE("close.save-result", "\ttarget=%p\tsaved=%d", static_cast<void*>(this),
                 result.status == SaveStatus::Saved);
    if (result.status == SaveStatus::Failed)
        wxMessageBox(result.error, _("Could not save file"), wxOK | wxICON_ERROR, this);
    return result.status == SaveStatus::Saved;
}

bool EditorFrame::SelectEncoding(EncodingChoice choice) {
    bool accepted = false;
    const bool changed = document_.SelectEncoding(choice, [this, &accepted](EncodingChangeWarning warning) {
        const bool empty = warning == EncodingChangeWarning::EmptyAnsiHistory;
        const wxString message =
            empty
                ? _("You are about to change the encoding of an empty file from ANSI to non-ANSI. Note that this will "
                    "clear the undo history, as it can't be synchronized with the new encoding. Continue?")
                : _("Switching the file encoding from ANSI to non-ANSI (and vice versa) may replace unsupported text "
                    "with default characters, and the undo history will be cleared. Continue?");
        accepted = ShowEncodingWarning(empty ? EncodingWarning::EmptyAnsi : EncodingWarning::Ansi, message, true);
        return accepted;
    });
    if (changed || accepted) {
        UpdateDocumentState();
        UpdateWindowUI(wxUPDATE_UI_RECURSE);
    }
    return changed;
}

bool EditorFrame::CanDiscardUntitledWhitespace() const {
    // Pinned Notepad4 FileSave/IsASpace: only short ASCII-whitespace Untitled
    // documents bypass the lifecycle prompt; explicit Save still writes them.
    constexpr intptr_t UntitledWhitespacePromptThresholdBytes = 2048;
    if (!document_.Path().empty()) return false;
    const auto length = editor_->Send(Scintilla::Message::GetLength);
    if (length >= UntitledWhitespacePromptThresholdBytes) return false;
    const auto* bytes = reinterpret_cast<const char*>(editor_->Send(Scintilla::Message::GetRangePointer, 0, length));
    for (intptr_t index = 0; bytes && index < length; ++index) {
        const char ch = bytes[index];
        if (!(ch == ' ' || (ch >= '\t' && ch <= '\r'))) return false;
    }
    return true;
}

EncodingLossChoice EditorFrame::ConfirmEncodingLoss() {
    return ShowEncodingWarning(
               EncodingWarning::DestinationLoss,
               _("Certain characters in the current text are not supported by the selected encoding, and may be "
                 "replaced "
                 "by default placeholders when saving. It's recommended to choose another file encoding. Continue?"),
               false)
               ? EncodingLossChoice::Replace
               : EncodingLossChoice::Cancel;
}

bool EditorFrame::ShowEncodingWarning(EncodingWarning warning, const wxString& message, bool yesNo) {
    // Original InfoBox keys and modes: suppress=1 returns Yes/OK immediately;
    // never-suppress=2 leaves the checkbox disabled. Cancel may also persist it.
    const auto index = static_cast<size_t>(std::to_underlying(warning));
    const wxString key = wxString::Format("MsgConv%zu", index + 1);
    int& mode = suppressedEncodingMessages_[index];
    if (settings_) mode = settings_->MessageSuppression(key);
    if (mode == 1) return true;
    SearchFeedbackDialog dialog(this, key, message, true, mode != 2,
                                yesNo ? _("Encoding Conversion") : _("Encoding character loss"), yesNo);
    const auto choice = dialog.Choose();
    if (choice.suppress) {
        mode = 1;
        if (settings_) settings_->SuppressMessage(key);
    }
    return choice.accepted;
}

void EditorFrame::ConfirmLineEndingConversion(const LineEndingCounts& counts, bool defaultNo) {
    const auto current = static_cast<Scintilla::EndOfLine>(editor_->Send(Scintilla::Message::GetEOLMode));
    LineEndingConversionDialog dialog(this, counts, current, warnLineEndings_, defaultNo);
    const auto choice = dialog.Choose();
    warnLineEndings_ = choice.warn;
    if (choice.convert) {
        editor_->Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(std::to_underlying(choice.mode)));
        editor_->Send(Scintilla::Message::ConvertEOLs, static_cast<uintptr_t>(std::to_underlying(choice.mode)));
    }
}

void EditorFrame::ChooseDefaultFont(bool code) {
    auto& fonts = (settings_ ? settings_->values : defaults_).fonts;
    auto& style = code ? fonts.codeStyle : fonts.textStyle;
    const auto initial = DefaultStyleFont(fonts, code);
    wxFontData data;
    data.SetInitialFont(initial);
    data.SetColour(DefaultStyleForeground(fonts, code));
#ifdef __WXGTK__
    // A synthetic weight can render correctly but have no face in GTK's
    // chooser. Start at Pango's loaded face, as a native font chooser does.
    auto* context = gtk_widget_get_pango_context(GTK_WIDGET(editor_->GetHandle()));
    if (auto* loaded = pango_context_load_font(context, initial.GetNativeFontInfo()->description)) {
        wxNativeFontInfo native;
        native.description = pango_font_describe(loaded);
        wxFont effective(native);
        effective.SetFractionalPointSize(initial.GetFractionalPointSize());
        data.SetInitialFont(effective);
        g_object_unref(loaded);
    }
#endif
    data.EnableEffects(true);
    wxFontDialog dialog(this, data);
    dialog.SetTitle(code ? _("Default Code Font") : _("Default Text Font"));
    if (dialog.ShowModal() != wxID_OK) return;
    auto chosen = dialog.GetFontData().GetChosenFont();
    if (!chosen.IsOk()) return;
#ifdef __WXGTK__
    // GTK's native font chooser has no underline or strike controls.
    chosen.SetUnderlined(initial.GetUnderlined());
    chosen.SetStrikethrough(initial.GetStrikethrough());
#endif
    style = MergeChosenFont(style, chosen);
    editor_->SetFontSettings(fonts);
    ApplySyntaxLanguage(*editor_, document_.Language(), document_.EffectiveLanguage());
    UpdatePositionStatus();
}
