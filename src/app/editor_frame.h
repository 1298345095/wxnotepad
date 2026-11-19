#pragma once
#include <wx/cmndata.h>

#include <wx/frame.h>
#include <array>
#include <memory>
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
    ~EditorFrame() override;
#ifdef __WXMSW__
    bool MSWTranslateMessage(WXMSG* message) override;
#endif
    EditorControl* Editor() const { return editor_; }
    const wxString& DocumentPath() const { return document_.Path(); }
    SaveResult Save(const EncodingLossDecision& decideLoss = {}, SaveTimeMode timeMode = SaveTimeMode::Current);
    SaveResult SaveAs(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss = {},
                      SaveFileType fileType = SaveType::CurrentScheme, SaveTimeMode timeMode = SaveTimeMode::Current);
    SaveResult SaveAs();
    SaveResult SaveCopy(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss = {});
    SaveResult SaveCopy();
    SaveResult SaveFirst(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss = {},
                         SaveFileType fileType = SaveType::CurrentScheme,
                         SaveTimeMode timeMode = SaveTimeMode::Current);
    bool UpdateFileTime();
    OpenResult Open(const std::optional<wxString>& source, std::optional<FileEncoding> encoding = std::nullopt);
    OpenResult Open();
    OpenResult OpenAs(FileEncoding encoding);
    OpenResult Revert();
    OpenResult ReloadWithEncoding();
    OpenResult ReloadAsUtf8();
    OpenResult ReloadCurrentFile(bool followEnd = false);
    bool DocumentHasUtf8Bom() const { return document_.HasUtf8Bom(); }
    FileEncoding DocumentEncoding() const { return document_.Encoding(); }
    bool DocumentHasBom() const { return document_.HasBom(); }
    bool DocumentIsModified() const { return document_.IsModified(); }
    bool SelectEncoding(EncodingChoice choice);
    bool NewDocument();
    void ShowRecentFiles();
    void SetFileWatchingOptions(const FileWatchingOptions& options);
    const FileWatchingOptions& FileWatching() const { return fileWatchingOptions_; }
    bool NewWindow();
    bool Restart();
    bool ExecuteDocument();
    bool OpenDocumentWith();
    OpenResult OpenRestartDocument(const wxString& path, std::optional<EncodingChoice> encoding,
                                   std::optional<SyntaxLanguage> language, bool readOnly, Scintilla::Position line,
                                   Scintilla::Position column);

private:
    EditorControl* editor_;
    SettingsStore* settings_;
    const wxString launchWorkingDirectory_;
    wxSize normalWindowSize_;
    bool FullScreenActive() const;
    void SetFullScreen(bool active);
    void HandleFullScreenKey(int modifiers);
    bool HandleFullScreenEscape();
#ifdef __WXMSW__
    struct FullScreenPlacement;
    std::unique_ptr<FullScreenPlacement> fullScreenPlacement_;
    void RefreshFullScreen();
#endif
    OriginalSettings defaults_;
    wxPrintData printerData_;
    IndentationSettings indentation_;
    DocumentSession document_;
    FileWatchingOptions fileWatchingOptions_;
    FileChangeMonitor fileChangeMonitor_;
    int automaticLanguageId_;
    int saveCopyId_;
    OpenResult ReloadQuickEncoding(QuickReloadEncoding encoding);
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
    bool unicodeControlCharactersVisible_ = false;
    void ApplyUnicodeControlCharacter();
    bool showWordWrapSymbols_ = false;
    int wordWrapSymbols_ = 2;
    // A loaded file's truncate-lines variable can override this global preference.
    bool wordWrap_ = false;
    void ToggleWordWrap();
    void ShowWordWrapSettings();
    void ApplyWrapSymbols();
    void CreateMenus();
    void ApplyBarVisibility();
    wxMenu* CreateFileMenu();
    void AppendReloadEncodingCommands(wxMenu* reload);
    void AppendEncodingCommands(wxMenu* file);
    wxMenu* CreateEditMenu();
    wxMenu* CreateLanguageMenu();
    wxMenu* CreateViewMenu();
    wxMenu* CreateSearchMenu();
    wxMenu* CreateSettingsMenu();
    wxMenu* CreateToolsMenu();
    void AppendDocumentTools(wxMenu* tools);
    void AppendEditingCommands(wxMenu* edit);
    void AppendConversionCommands(wxMenu* edit);
    void AppendEnclosureCommands(wxMenu* edit);
    void AppendCommentCommands(wxMenu* edit);
    void AppendSelectionCommands(wxMenu* edit);
    void AppendLineCommands(wxMenu* edit);
    void AppendLineWrappingCommands(wxMenu* edit);
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
    void AppendCompletionOptions(wxMenu* view);
    void AppendSelectionOptions(wxMenu* view);
    void AppendZoomCommands(wxMenu* view);
    void AppendDisplayCommands(wxMenu* view);
    void AppendCallTipCommands(wxMenu* view);
    void AppendMarginCommands(wxMenu* view);
    void ToggleIndentationGuides();
    void ToggleLongLineMarker();
    void AppendCurrentLineCommands(wxMenu* view);
    void AppendCaretCommands(wxMenu* view);
    void AppendFoldCommands(wxMenu* view);
    void AppendSettingsCommands(wxMenu* view);
    void AppendUILanguageCommands(wxMenu* settings);
    void ChooseUILanguage(uint16_t language);
    bool LaunchWindow(bool currentFile);
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
    bool HandleFileChange();
    void ReportFind(const FindResult& result);
    bool SearchMessage(SearchDirection direction);
    bool ShowSearchMessage(const wxString& key, const wxString& message, bool confirm);
    std::array<int, 3> suppressedSearchMessages_{};
    std::array<int, 3> suppressedEncodingMessages_{};
    enum class EncodingWarning { Ansi, EmptyAnsi, DestinationLoss };
    bool ShowEncodingWarning(EncodingWarning warning, const wxString& message, bool yesNo);
    void ShowFind(bool replace = false);
    void FindNext();
    void FindPrevious();
    void FindSelection(SearchDirection direction);
    void ReplaceNext();
    void Search(bool forward, bool extendSelection = false);
    void OnSearchKey(wxKeyEvent& event);
    void UpdateDocumentState();
    void UpdatePositionStatus();
    void ChooseDefaultFont(bool code);
    void ChooseLanguage(std::optional<SyntaxLanguage> language);
    bool ConfirmDiscardChanges(const wxString& action);
    void OnDropFiles(wxDropFilesEvent& event);
    OpenResult LoadFile(const DocumentLoad::Intent& intent);
    bool CanDiscardUntitledWhitespace() const;
    EncodingLossChoice ConfirmEncodingLoss();
    void ConfirmLineEndingConversion(const LineEndingCounts& counts, bool defaultNo);
    bool SaveSettings(bool explicitSave);
};
