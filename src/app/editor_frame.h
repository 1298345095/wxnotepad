#pragma once

#include <wx/frame.h>
#include <array>
#include "document/document_session.h"
#include "search/search_session.h"
#include "editor/indentation_settings.h"
#include "editor/align_lines.h"
#include "app/file_change_monitor.h"

class EditorControl;
class FindReplaceDialog;
class SettingsStore;
class wxMenu;

class EditorFrame final : public wxFrame {
public:
    explicit EditorFrame(SettingsStore* settings = nullptr);
    EditorControl* Editor() const { return editor_; }
    const wxString& DocumentPath() const { return document_.Path(); }
    SaveResult Save(const GbkLossDecision& decideLoss = {}, SaveTimeMode timeMode = SaveTimeMode::Current);
    SaveResult SaveAs(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss = {},
                      SaveFileType fileType = SaveType::CurrentScheme, SaveTimeMode timeMode = SaveTimeMode::Current);
    SaveResult SaveAs();
    SaveResult SaveCopy(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss = {});
    SaveResult SaveCopy();
    SaveResult SaveFirst(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss = {},
                         SaveFileType fileType = SaveType::CurrentScheme,
                         SaveTimeMode timeMode = SaveTimeMode::Current);
    bool UpdateFileTime();
    OpenResult Open(const std::optional<wxString>& source, std::optional<FileEncoding> encoding = std::nullopt);
    OpenResult Open();
    OpenResult OpenAs(FileEncoding encoding);
    OpenResult Revert();
    OpenResult ReloadCurrentFile(bool followEnd = false);
    bool DocumentHasUtf8Bom() const { return document_.HasUtf8Bom(); }
    FileEncoding DocumentEncoding() const { return document_.Encoding(); }
    bool DocumentHasBom() const { return document_.HasBom(); }
    bool NewDocument();
    void ShowRecentFiles();
    void SetFileWatchingOptions(const FileWatchingOptions& options);
    const FileWatchingOptions& FileWatching() const { return fileWatchingOptions_; }
    bool NewWindow();

private:
    EditorControl* editor_;
    SettingsStore* settings_;
    const wxString launchWorkingDirectory_;
    wxSize normalWindowSize_;
    OriginalSettings defaults_;
    IndentationSettings indentation_;
    DocumentSession document_;
    FileWatchingOptions fileWatchingOptions_;
    FileChangeMonitor fileChangeMonitor_;
    int automaticLanguageId_;
    int saveCopyId_;
    int recentFilesId_;
    void AppendRecentFilesCommand(wxMenu* file);
    void RecordRecentFile();
    int revertId_;
    wxString lastSaveCopyDirectory_;
    SearchSession search_;
    FindReplaceDialog* findDialog_ = nullptr;
    wxString enclosureBefore_;
    wxString enclosureAfter_;
    AlignmentMode alignmentMode_ = AlignmentMode::Left;
    unsigned sortOptions_ = 0;
    wxString linePrefix_;
    wxString lineSuffix_;
    bool skipEmptyLines_ = false;
    int wrapColumn_ = 0;
    int defaultEolMode_ = 0;
    bool warnLineEndings_ = true;
    bool showWordWrapSymbols_ = false;
    int wordWrapSymbols_ = 2;
    void ApplyWrapSymbols();
    void CreateMenus();
    wxMenu* CreateFileMenu();
    wxMenu* CreateEditMenu();
    wxMenu* CreateLanguageMenu();
    wxMenu* CreateViewMenu();
    wxMenu* CreateSearchMenu();
    wxMenu* CreateSettingsMenu();
    void AppendEditingCommands(wxMenu* edit);
    void AppendConversionCommands(wxMenu* edit);
    void AppendEnclosureCommands(wxMenu* edit);
    void AppendSelectionCommands(wxMenu* edit);
    void AppendLineCommands(wxMenu* edit);
    void AppendSpecialCommands(wxMenu* edit);
    void AppendDateTimeCommands(wxMenu* edit);
    void InsertDateTime(bool shortDate);
    void InsertEncodingIdentifier();
    void InsertFileName(FileNamePart part);
    void CopyFileName(FileNamePart part);
    void AppendFileNameCopyCommands(wxMenu* copy);
    void AppendSearchCommands(wxMenu* edit);
    void AppendNavigationCommands(wxMenu* edit);
    void AppendLineEndingCommands(wxMenu* edit);
    void AppendLineSelectionCommands(wxMenu* edit);
    void AppendIndentationOptions(wxMenu* view);
    void AppendZoomCommands(wxMenu* view);
    void AppendDisplayCommands(wxMenu* view);
    void AppendMarginCommands(wxMenu* view);
    void ToggleIndentationGuides();
    void ToggleLongLineMarker();
    void AppendCurrentLineCommands(wxMenu* view);
    void AppendCaretCommands(wxMenu* view);
    void AppendFoldCommands(wxMenu* view);
    void AppendSettingsCommands(wxMenu* view);
    bool HandleCommandKey(const wxKeyEvent& event);
    bool HandleInsertionKey(const wxKeyEvent& event);
    bool HandleCaseConversionKey(const wxKeyEvent& event);
    bool HandleTabConversionKey(const wxKeyEvent& event);
    void ConvertTabs(bool toTabs, bool onlyIndent);
    bool HandleSearchKey(const wxKeyEvent& event);
    bool HandleNavigationKey(const wxKeyEvent& event);
    bool HandleBlockNavigationKey(const wxKeyEvent& event);
    void HandleAltBlockNavigationKey(const wxKeyEvent& event, int key);
    bool HandleViewKey(const wxKeyEvent& event);
    bool HandleDisplayKey(const wxKeyEvent& event, int whitespace, int eol, int symbols);
    bool HandleDocumentKey(const wxKeyEvent& event);
    void ToggleReadOnlyMode();
    void ShowLineEndingOptions();
    void ShowFileChangeNotificationOptions();
    void RefreshFileWatching();
    void HandleFileChange();
    void ReportFind(const FindResult& result);
    bool SearchMessage(SearchDirection direction);
    bool ShowSearchMessage(const wxString& key, const wxString& message, bool confirm);
    std::array<int, 3> suppressedSearchMessages_{};
    void ShowFind(bool replace = false);
    void FindNext();
    void FindPrevious();
    void Search(bool forward, bool extendSelection = false);
    void OnSearchKey(wxKeyEvent& event);
    void UpdateDocumentState();
    void UpdatePositionStatus();
    void ChooseLanguage(std::optional<SyntaxLanguage> language);
    bool ConfirmDiscardChanges(const wxString& action);
    OpenResult LoadFile(const std::optional<wxString>& source, const std::function<bool()>& beforeReplace,
                        std::optional<FileEncoding> encoding = std::nullopt,
                        std::optional<EncodingChoice> weakEncoding = std::nullopt);
    bool CanDiscardUntitledWhitespace() const;
    GbkLossChoice ConfirmGbkLoss();
    void ConfirmLineEndingConversion(const LineEndingCounts& counts, bool defaultNo);
    bool SaveSettings(bool explicitSave);
};
