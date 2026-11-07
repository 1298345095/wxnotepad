#pragma once

#include <wx/frame.h>
#include <array>
#include "document/document_session.h"
#include "search/search_session.h"
#include "editor/indentation_settings.h"
#include "editor/align_lines.h"

class EditorControl;
class FindReplaceDialog;
class SettingsStore;
class wxMenu;

class EditorFrame final : public wxFrame {
public:
    explicit EditorFrame(SettingsStore* settings = nullptr);
    EditorControl* Editor() const { return editor_; }
    const wxString& DocumentPath() const { return document_.Path(); }
    SaveResult Save(const GbkLossDecision& decideLoss = {});
    SaveResult SaveAs(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss = {},
                      SaveFileType fileType = SaveType::CurrentScheme);
    SaveResult SaveAs();
    SaveResult SaveCopy(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss = {});
    SaveResult SaveCopy();
    SaveResult SaveFirst(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss = {},
                         SaveFileType fileType = SaveType::CurrentScheme);
    OpenResult Open(const std::optional<wxString>& source, std::optional<FileEncoding> encoding = std::nullopt);
    OpenResult Open();
    OpenResult OpenAs(FileEncoding encoding);
    bool DocumentHasUtf8Bom() const { return document_.HasUtf8Bom(); }
    FileEncoding DocumentEncoding() const { return document_.Encoding(); }
    bool DocumentHasBom() const { return document_.HasBom(); }
    bool New();

private:
    EditorControl* editor_;
    SettingsStore* settings_;
    OriginalSettings defaults_;
    IndentationSettings indentation_;
    DocumentSession document_;
    int automaticLanguageId_;
    int saveCopyId_;
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
    void CreateMenus();
    wxMenu* CreateFileMenu();
    wxMenu* CreateEditMenu();
    wxMenu* CreateLanguageMenu();
    wxMenu* CreateViewMenu();
    void AppendEditingCommands(wxMenu* edit);
    void AppendDateTimeCommands(wxMenu* edit);
    void InsertDateTime(bool shortDate);
    void AppendSearchCommands(wxMenu* edit);
    void AppendNavigationCommands(wxMenu* edit);
    void AppendLineEndingCommands(wxMenu* edit);
    void AppendLineSelectionCommands(wxMenu* edit);
    void AppendIndentationOptions(wxMenu* view);
    void AppendZoomCommands(wxMenu* view);
    void AppendDisplayCommands(wxMenu* view);
    void AppendFoldCommands(wxMenu* view);
    void AppendSettingsCommands(wxMenu* view);
    bool HandleCommandKey(const wxKeyEvent& event);
    bool HandleTabConversionKey(const wxKeyEvent& event);
    void ConvertTabs(bool toTabs, bool onlyIndent);
    bool HandleSearchKey(const wxKeyEvent& event);
    bool HandleNavigationKey(const wxKeyEvent& event);
    bool HandleViewKey(const wxKeyEvent& event);
    bool HandleWhitespaceKey(const wxKeyEvent& event, int commandId);
    bool HandleDocumentKey(const wxKeyEvent& event);
    void ShowLineEndingOptions();
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
    GbkLossChoice ConfirmGbkLoss();
    void ConfirmLineEndingConversion(const LineEndingCounts& counts, bool defaultNo);
    bool SaveSettings(bool explicitSave);
};
