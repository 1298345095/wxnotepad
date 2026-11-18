#include "app/editor_frame.h"
#include <utility>
#include "syntax/syntax_scheme.h"
#include "editor/editor_control.h"
#include "app/dialogs/document_dialogs.h"
#include "app/dialogs/find_replace_dialog.h"
#include "app/dialogs/search_feedback_dialog.h"
#include "app/dialogs/recent_files_dialog.h"
#include "settings/settings_store.h"
#include "settings/search_settings.h"
#include "settings/recent_files.h"
#include "diagnostics/trace.h"
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/statusbr.h>
#include <wx/filename.h>
#include <wx/translation.h>
#include <array>

namespace {
wxString EncodingName(FileEncoding encoding) {
    switch (encoding) {
        using enum FileEncoding;
    case Utf16Le:
        return "UTF-16LE";
    case Utf16Be:
        return "UTF-16BE";
    case Gbk:
        return "GBK (CP936)";
    case Gb18030:
        return "GB18030 (CP54936)";
    case Big5:
        return "Big5 (CP950)";
    case Windows1252:
        return "Windows-1252";
    case Dos437:
        return "DOS-437";
    default:
        return "UTF-8";
    }
}

wxString DocumentEncodingLabel(const DocumentSession& document) {
    const auto encoding = document.Encoding();
    const wxString name = EncodingName(encoding);
    if (document.IsDefaultEncoding()) {
        switch (encoding) {
            using enum FileEncoding;
        case Gbk:
            return "ANSI (CP936)";
        case Gb18030:
            return "ANSI (CP54936)";
        case Big5:
            return "ANSI (CP950)";
        default:
            return "ANSI (" + name + ")";
        }
    }
    if (document.IsOemEncoding()) return "OEM (" + name + ")";
    return name;
}
} // namespace

EditorFrame::EditorFrame(SettingsStore* settings)
    : wxFrame(nullptr, wxID_ANY, wxString::Format(_("%s - wxnotepad"), _("Untitled")), wxDefaultPosition,
              wxSize(900, 600)),
      editor_(new EditorControl(this, settings ? settings->values.selectOption : OriginalSettings{}.selectOption)),
      settings_(settings), launchWorkingDirectory_(wxGetCwd()), normalWindowSize_(GetSize()),
      indentation_(settings ? settings->values : defaults_),
      document_(*editor_, settings ? settings->values.encoding : EncodingOptions{},
                settings ? SyntaxDetectionOptions{settings->values.autoSelect, settings->values.noCgiGuess,
                                                  settings->values.noHtmlGuess}
                         : SyntaxDetectionOptions{}),
      fileWatchingOptions_(settings ? settings->values.fileWatching : FileWatchingOptions{}),
      fileChangeMonitor_(*this, [this] { return HandleFileChange(); }), search_(*editor_) {
    // Establish the single editor child before preferences calculate wrapped geometry.
    Layout();
    const auto& preferences = settings ? settings->values : defaults_;
#ifdef __WXMSW__
    editor_->Send(Scintilla::Message::SetTechnology, preferences.renderingTechnology);
#endif
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        if (!IsMaximized() && !IsIconized()) normalWindowSize_ = GetSize();
        event.Skip();
    });
    editor_->SetAutoInsertMask(preferences.autoInsertMask);
    editor_->SetCompletionSettings(preferences.completion);
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
        showWordWrapSymbols_ = settings->values.showWordWrapSymbols;
        wordWrapSymbols_ = settings->values.wordWrapSymbols;
        ApplyWrapSymbols();
        wordWrap_ = settings->values.wordWrap;
        editor_->SetWordWrapMode(static_cast<Scintilla::Wrap>(settings->values.wordWrapMode));
        if (!wordWrap_) editor_->SetWordWrapMode(Scintilla::Wrap::None);
        editor_->SetWordWrapIndent(settings->values.wordWrapIndent);
        editor_->Send(Scintilla::Message::SetMarginOptions, settings->values.wordWrapSelectSubLine);
        editor_->Send(Scintilla::Message::SetViewWS,
                      static_cast<uintptr_t>(std::to_underlying(settings->values.viewWhiteSpace
                                                                    ? Scintilla::WhiteSpace::VisibleAlways
                                                                    : Scintilla::WhiteSpace::Invisible)));
    }
    document_.New();
    CreateMenus();
    CreateStatusBar(3);
    search_.SetWrapConfirmation([this](SearchDirection direction) { return SearchMessage(direction); });
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
        UpdatePositionStatus();
        event.Skip();
    });
    Bind(wxEVT_EDITOR_STATE_CHANGED, [this](wxCommandEvent&) { UpdateDocumentState(); });
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

bool EditorFrame::SaveSettings(bool explicitSave) {
    if (!settings_) return true;
#ifdef __WXMSW__
    settings_->values.renderingTechnology = static_cast<int>(editor_->Send(Scintilla::Message::GetTechnology));
#endif
    settings_->values.markLongLines = editor_->Send(Scintilla::Message::GetEdgeMode) != 0;
    settings_->values.showIndentGuides = editor_->Send(Scintilla::Message::GetIndentationGuides) != 0;
    settings_->values.wordWrap = document_.FileVariables().wordWrap.has_value()
                                     ? wordWrap_
                                     : editor_->Send(Scintilla::Message::GetWrapMode) != 0;
    settings_->values.wordWrapMode = std::to_underlying(editor_->PreferredWordWrapMode());
    settings_->values.wordWrapIndent = editor_->WordWrapIndent();
    settings_->values.wordWrapSelectSubLine = editor_->Send(Scintilla::Message::GetMarginOptions) != 0;
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
    settings_->values.showWordWrapSymbols = showWordWrapSymbols_;
    settings_->values.wordWrapSymbols = wordWrapSymbols_;
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

void EditorFrame::Search(bool forward, bool extendSelection) {
    if (!search_.HasFindText()) {
        ShowFind();
        return;
    }
    ReportFind(search_.Find(forward ? SearchDirection::Forward : SearchDirection::Backward, extendSelection));
}

void EditorFrame::ReportFind(const FindResult& result) {
    switch (result.status) {
        using enum FindStatus;
    case Found:
        SetStatusText(result.wrapped ? _("Search wrapped") : _("Match found"), 2);
        break;
    case NotFound:
        SetStatusText(_("No match found"), 2);
        ShowSearchMessage("MsgNotFound", _("The specified text was not found."), false);
        break;
    case InvalidPattern:
        SetStatusText(_("Invalid regular expression"), 2);
        break;
    case EmptyQuery:
        ShowSearchMessage("MsgNotFound", _("The specified text was not found."), false);
        break;
    case Cancelled:
        break;
    }
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
    const wxString name = document_.Path().empty() ? _("Untitled") : document_.Path();
    const wxString protection = document_.IsReadOnlyFile()                       ? _(" (Read Only File)")
                                : editor_->Send(Scintilla::Message::GetReadOnly) ? _(" (Read Only Mode)")
                                                                                 : wxString{};
    // TRANSLATORS: Window title arguments: modified marker ("*" or empty), document path or untitled name,
    // and optional read-only file/mode annotation.
    SetTitle(wxString::Format(_("%s%s%s - wxnotepad"), document_.IsModified() ? "*" : "", name, protection));
    const wxString encoding = DocumentEncodingLabel(document_);
    const auto eolMode = static_cast<Scintilla::EndOfLine>(editor_->Send(Scintilla::Message::GetEOLMode));
    const wxString eol = eolMode == Scintilla::EndOfLine::CrLf ? "CRLF"
                         : eolMode == Scintilla::EndOfLine::Cr ? "CR"
                                                               : "LF";
    // TRANSLATORS: Status arguments: encoding name, optional BOM annotation, syntax name,
    // optional manual syntax override annotation, and line-ending name (CRLF, CR or LF).
    SetStatusText(wxString::Format(_("%s%s | %s%s | EOL: %s"), encoding, document_.HasBom() ? " (BOM)" : "",
                                   LanguageName(document_.EffectiveLanguage()),
                                   document_.LanguageOverride() ? _(" (override)") : wxString{}, eol));
    UpdatePositionStatus();
}

void EditorFrame::UpdatePositionStatus() {
    const auto caret = editor_->Send(Scintilla::Message::GetCurrentPos);
    const auto line = editor_->Send(Scintilla::Message::LineFromPosition, caret);
    const auto column = editor_->Send(Scintilla::Message::GetColumn, caret, line);
    wxString position = wxString::Format(_("Ln %llu, Col %llu"), static_cast<unsigned long long>(line) + 1,
                                         static_cast<unsigned long long>(column) + 1);
    const auto selected = editor_->SelectionCharacterCount();
    if (selected) position += wxString::Format(_(" | Sel %lld"), static_cast<long long>(selected));
    SetStatusText(position, 1);
    auto* status = GetStatusBar();
    const int metadataWidth = status->GetTextExtent(status->GetStatusText()).x + FromDIP(20);
    const int positionWidth = status->GetTextExtent(position).x + FromDIP(20);
    const std::array widths{metadataWidth, positionWidth, -1};
    if (status->GetStatusWidth(0) != metadataWidth || status->GetStatusWidth(1) != positionWidth)
        status->SetStatusWidths(static_cast<int>(widths.size()), widths.data());
}

void EditorFrame::ChooseLanguage(std::optional<SyntaxLanguage> language) {
    document_.SelectLanguage(language);
    indentation_.Apply(*editor_, document_.Language());
    UpdateDocumentState();
}

SaveResult EditorFrame::SaveFirst(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss,
                                  SaveFileType fileType, SaveTimeMode timeMode) {
    const auto result = document_.SaveFirst(
        destination, decideLoss ? decideLoss : [this] { return ConfirmEncodingLoss(); }, fileType, timeMode);
    if (result.status == SaveStatus::Saved) {
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
            const auto chosen = ChooseSaveFile(this, _("Save As"), document_.Language(), document_.Path());
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
    const auto chosen = ChooseSaveFile(this, _("Save file"), document_.Language(), document_.Path());
    if (!chosen) return SaveFirst(std::nullopt, decideLoss, SaveType::CurrentScheme, timeMode);
    return SaveFirst(chosen->path, decideLoss, chosen->type, timeMode);
}

SaveResult EditorFrame::SaveAs(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss,
                               SaveFileType fileType, SaveTimeMode timeMode) {
    const auto result = document_.SaveAs(
        destination, decideLoss ? decideLoss : [this] { return ConfirmEncodingLoss(); }, fileType, timeMode);
    if (result.status == SaveStatus::Saved) {
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
    const auto chosen = ChooseSaveFile(this, _("Save As"), document_.Language(), document_.Path());
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
            const auto extensions = SyntaxFor(document_.Language()).extensions;
            if (!extensions.empty()) initial.SetExt(extensions.front());
        }
        directory = initial.GetPath();
        filename = initial.GetFullName();
    }
    const auto chosen =
        ChooseSaveFile(this, _("Save Copy"), document_.Language(), document_.Path(), {directory, filename});
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
    if (!document_.FileVariables().wordWrap.has_value())
        wordWrap_ = editor_->Send(Scintilla::Message::GetWrapMode) != 0;
    const auto result = document_.Load(intent, [this] {
        indentation_.Load(*editor_, document_.Language(), document_.FileVariables());
        editor_->SetWordWrapMode(document_.FileVariables().wordWrap.value_or(wordWrap_)
                                     ? editor_->PreferredWordWrapMode()
                                     : Scintilla::Wrap::None);
        const auto& preferences = settings_ ? settings_->values : defaults_;
        editor_->Send(Scintilla::Message::SetEdgeColumn,
                      document_.FileVariables().longLinesLimit.value_or(preferences.longLinesLimit));
    });
    if (result.status == OpenStatus::Opened) {
        if (std::holds_alternative<DocumentLoad::Open>(intent) && fileWatchingOptions_.resetOnNewFile)
            fileWatchingOptions_.mode = FileWatchingMode::None;
        RefreshFileWatching();
        RecordRecentFile();
        wrapColumn_ = 0;
        if (result.lineEndings.Mixed() && warnLineEndings_)
            ConfirmLineEndingConversion(result.lineEndings, result.unknownLanguage && document_.IsDefaultEncoding());
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

OpenResult EditorFrame::Open() {
    OpenFileDialog dialog(this);
    return Open(dialog.Choose());
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
        OpenFileDialog dialog(this);
        dialog.SetDirectory(path);
        source = dialog.Choose();
    }
    const auto result = Open(source);
    if (result.status == OpenStatus::Failed)
        wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, this);
}

OpenResult EditorFrame::OpenAs(FileEncoding encoding) {
    OpenFileDialog dialog(this);
    return Open(dialog.Choose(), encoding);
}

bool EditorFrame::NewDocument() {
    if (!ConfirmDiscardChanges(_("Save changes to %s before creating a new document?"))) return false;
    if (!document_.FileVariables().wordWrap.has_value())
        wordWrap_ = editor_->Send(Scintilla::Message::GetWrapMode) != 0;
    document_.New();
    editor_->SetWordWrapMode(wordWrap_ ? editor_->PreferredWordWrapMode() : Scintilla::Wrap::None);
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
    const wxString name = document_.Path().empty() ? _("Untitled") : document_.Path();
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
    if (choice == wxID_NO) return true;
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
    if (!document_.Path().empty()) return false;
    const auto length = editor_->Send(Scintilla::Message::GetLength);
    if (length >= 2048) return false;
    const auto* bytes = reinterpret_cast<const char*>(editor_->Send(Scintilla::Message::GetRangePointer, 0, length));
    for (intptr_t index = 0; bytes && index < length; ++index) {
        const char ch = bytes[index];
        if (!(ch == ' ' || (ch >= 0x09 && ch <= 0x0d))) return false;
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
