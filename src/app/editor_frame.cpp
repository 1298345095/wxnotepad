#include "app/editor_frame.h"
#include "syntax/syntax_scheme.h"
#include "editor/editor_control.h"
#include "app/dialogs/document_dialogs.h"
#include "app/dialogs/find_replace_dialog.h"
#include "app/dialogs/search_feedback_dialog.h"
#include "settings/settings_store.h"
#include "settings/search_settings.h"
#include "diagnostics/trace.h"
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/statusbr.h>
#include <wx/filename.h>

EditorFrame::EditorFrame(SettingsStore* settings)
    : wxFrame(nullptr, wxID_ANY, "Untitled - wxnotepad", wxDefaultPosition, wxSize(900, 600)),
      editor_(new EditorControl(this, settings ? settings->values.selectOption : OriginalSettings{}.selectOption)),
      settings_(settings), launchWorkingDirectory_(wxGetCwd()), normalWindowSize_(GetSize()),
      indentation_(settings ? settings->values : defaults_),
      document_(*editor_, settings ? settings->values.encoding : EncodingOptions{},
                settings ? SyntaxDetectionOptions{settings->values.autoSelect, settings->values.noCgiGuess}
                         : SyntaxDetectionOptions{}),
      search_(*editor_) {
    const auto& preferences = settings ? settings->values : defaults_;
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        if (!IsMaximized() && !IsIconized()) normalWindowSize_ = GetSize();
        event.Skip();
    });
    editor_->SetAutoShrinkScrollWidth(preferences.autoShrinkScrollWidth);
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
        editor_->Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(mode));
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
        if (settings->values.wordWrap) editor_->ToggleWordWrap();
        editor_->Send(Scintilla::Message::SetViewWS,
                      static_cast<uintptr_t>(settings->values.viewWhiteSpace ? Scintilla::WhiteSpace::VisibleAlways
                                                                             : Scintilla::WhiteSpace::Invisible));
    }
    CreateMenus();
    CreateStatusBar(3);
    search_.SetWrapConfirmation([this](SearchDirection direction) { return SearchMessage(direction); });
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
        UpdatePositionStatus();
        event.Skip();
    });
    Bind(wxEVT_EDITOR_STATE_CHANGED, [this](wxCommandEvent&) { UpdateDocumentState(); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        WXNOTE_TRACE("close.request", "\ttarget=%p\tcan_veto=%d\tdirty=%d", static_cast<void*>(this), event.CanVeto(),
                     editor_->Send(Scintilla::Message::GetModify) != 0);
        if (event.CanVeto() && !ConfirmDiscardChanges("closing")) {
            event.Veto();
            WXNOTE_TRACE("close.veto", "\ttarget=%p", static_cast<void*>(this));
            return;
        }
        SaveSettings(false);
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
    settings_->values.markLongLines = editor_->Send(Scintilla::Message::GetEdgeMode) != 0;
    settings_->values.showIndentGuides = editor_->Send(Scintilla::Message::GetIndentationGuides) != 0;
    settings_->values.wordWrap = editor_->Send(Scintilla::Message::GetWrapMode) != 0;
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
    settings_->values.zoomLevel = static_cast<int>(editor_->Send(Scintilla::Message::GetZoom));
    settings_->values.caret = editor_->GetCaretAppearance();
    settings_->values.highlightCurrentLine =
        static_cast<int>(editor_->GetCurrentLineHighlightMode()) +
        10 * static_cast<int>(editor_->Send(Scintilla::Message::GetCaretLineHighlightSubLine));
    CaptureSearchSettings(search_, settings_->values);
    settings_->values.lineSelection = static_cast<int>(editor_->GetLineSelectionMode());
    settings_->values.markOccurrences = editor_->Occurrences().Options();
    return settings_->Save(explicitSave);
}

void EditorFrame::ShowLineEndingOptions() {
    LineEndingOptionsDialog dialog(this, {defaultEolMode_, warnLineEndings_, document_.SaveCleanup()});
    const auto choice = dialog.Choose();
    if (!choice) return;
    defaultEolMode_ = choice->defaultMode;
    const auto defaultMode = defaultEolMode_ == 0   ? Scintilla::EndOfLine::CrLf
                             : defaultEolMode_ == 1 ? Scintilla::EndOfLine::Lf
                                                    : Scintilla::EndOfLine::Cr;
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
    const wxString name = document_.Path().empty() ? wxString("Untitled") : document_.Path();
    const wxString protection = document_.IsReadOnlyFile()                       ? " (Read Only File)"
                                : editor_->Send(Scintilla::Message::GetReadOnly) ? " (Read Only Mode)"
                                                                                 : "";
    SetTitle((editor_->Send(Scintilla::Message::GetModify) ? "*" : "") + name + protection + " - wxnotepad");
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
        if (document_.RefreshReadOnlyFile()) {
            UpdateDocumentState();
            if (wxMessageBox("\"" + document_.Path() + "\" is read only. Save to a different file?", "Read only file",
                             wxYES_NO | wxICON_WARNING, this) != wxYES)
                return {SaveStatus::Cancelled, {}};
            const auto chosen = ChooseSaveFile(this, "Save As", document_.Language(), document_.Path());
            if (!chosen) return SaveAs(std::nullopt, decideLoss);
            return SaveAs(chosen->path, decideLoss, chosen->type);
        }
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

SaveResult EditorFrame::SaveCopy(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss) {
    const auto result = document_.SaveCopy(destination, decideLoss ? decideLoss : [this] { return ConfirmGbkLoss(); });
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
        ChooseSaveFile(this, "Save Copy", document_.Language(), document_.Path(), {directory, filename});
    return SaveCopy(chosen ? std::optional<wxString>(chosen->path) : std::nullopt);
}

OpenResult EditorFrame::Open(const std::optional<wxString>& source, std::optional<FileEncoding> encoding) {
    const auto result = document_.Open(
        source, [this] { return ConfirmDiscardChanges("opening another file"); }, encoding, std::nullopt,
        [this] {
            indentation_.Load(*editor_, document_.Language(), document_.FileVariables());
            const auto& preferences = settings_ ? settings_->values : defaults_;
            editor_->Send(Scintilla::Message::SetEdgeColumn,
                          document_.FileVariables().longLinesLimit.value_or(preferences.longLinesLimit));
        });
    if (result.status == OpenStatus::Opened) {
        wrapColumn_ = 0;
        if (result.lineEndings.Mixed() && warnLineEndings_)
            ConfirmLineEndingConversion(result.lineEndings, result.unknownLanguage && document_.IsDefaultEncoding());
        UpdateWindowUI(wxUPDATE_UI_RECURSE);
    }
    UpdateDocumentState();
    return result;
}

OpenResult EditorFrame::Open() {
    OpenFileDialog dialog(this);
    return Open(dialog.Choose());
}

OpenResult EditorFrame::OpenAs(FileEncoding encoding) {
    OpenFileDialog dialog(this);
    return Open(dialog.Choose(), encoding);
}

bool EditorFrame::NewDocument() {
    if (!ConfirmDiscardChanges("creating a new document")) return false;
    document_.New();
    editor_->Send(Scintilla::Message::SetEdgeColumn,
                  settings_ ? settings_->values.longLinesLimit : defaults_.longLinesLimit);
    wrapColumn_ = 0;
    indentation_.Reset(*editor_, document_.Language());
    GetMenuBar()->Check(automaticLanguageId_, true);
    UpdateDocumentState();
    return true;
}

bool EditorFrame::ConfirmDiscardChanges(const wxString& action) {
    if (!editor_->Send(Scintilla::Message::GetModify)) return true;
    if (CanDiscardUntitledWhitespace()) return true;
    const wxString name = document_.Path().empty() ? wxString("Untitled") : document_.Path();
    wxMessageDialog dialog(this, "Save changes to " + name + " before " + action + "?", "Save changes?",
                           wxYES_NO | wxCANCEL | wxICON_QUESTION);
    dialog.SetYesNoCancelLabels("&Save", "&Discard", "&Cancel");
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
        wxMessageBox(result.error, "Could not save file", wxOK | wxICON_ERROR, this);
    return result.status == SaveStatus::Saved;
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

GbkLossChoice EditorFrame::ConfirmGbkLoss() {
    wxMessageDialog dialog(
        this,
        "Certain characters in the current text are not supported by the selected encoding, and may be replaced "
        "by default placeholders when saving. It's recommended to choose another file encoding. Continue?",
        "GBK character loss", wxOK | wxCANCEL | wxICON_WARNING);
    return dialog.ShowModal() == wxID_OK ? GbkLossChoice::Replace : GbkLossChoice::Cancel;
}

void EditorFrame::ConfirmLineEndingConversion(const LineEndingCounts& counts, bool defaultNo) {
    const auto current = static_cast<Scintilla::EndOfLine>(editor_->Send(Scintilla::Message::GetEOLMode));
    LineEndingConversionDialog dialog(this, counts, current, warnLineEndings_, defaultNo);
    const auto choice = dialog.Choose();
    warnLineEndings_ = choice.warn;
    if (choice.convert) {
        editor_->Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(choice.mode));
        editor_->Send(Scintilla::Message::ConvertEOLs, static_cast<uintptr_t>(choice.mode));
    }
}
