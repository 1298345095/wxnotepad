#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "app/find_replace_dialog.h"
#include "app/go_to_line_dialog.h"
#include <wx/menu.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/statusbr.h>

EditorFrame::EditorFrame()
    : wxFrame(nullptr, wxID_ANY, "Untitled - wxnotepad", wxDefaultPosition, wxSize(900, 600)),
      editor_(new EditorControl(this)), document_(*editor_), search_(*editor_) {
    auto* file = new wxMenu;
    file->Append(wxID_NEW, "&New\tCtrl+N");
    file->Append(wxID_OPEN, "&Open...\tCtrl+O");
    const int openGbk = wxWindow::NewControlId();
    file->Append(openGbk, "Open As &GBK...");
    const int openUtf16 = wxWindow::NewControlId();
    file->Append(openUtf16, "Open As UTF-16LE...");
    file->Append(wxID_SAVE, "&Save\tCtrl+S");
    file->Append(wxID_SAVEAS, "Save &As...\tCtrl+Shift+S");
    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO, "&Undo\tCtrl+Z");
    edit->Append(wxID_REDO, "&Redo\tCtrl+Y");
    edit->AppendSeparator();
    edit->Append(wxID_CUT, "Cu&t\tCtrl+X");
    edit->Append(wxID_COPY, "&Copy\tCtrl+C");
    edit->Append(wxID_PASTE, "&Paste\tCtrl+V");
    edit->AppendSeparator();
    edit->Append(wxID_FIND, "&Find...\tCtrl+F");
    edit->Append(wxID_REPLACE, "&Replace...\tCtrl+H");
    edit->Append(wxID_FORWARD, "Find &Next\tF3");
    edit->Append(wxID_BACKWARD, "Find &Previous\tShift+F3");
    const int goToLine = wxWindow::NewControlId();
    edit->Append(goToLine, "&Go To Line...\tCtrl+G");
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowGoToLineDialog(this, *editor_); }, goToLine);
    auto* indentation = new wxMenu;
    for (const bool tabs : {true, false}) {
        const int id = wxWindow::NewControlId();
        indentation->AppendRadioItem(id, tabs ? "&Tabs" : "&Spaces");
        Bind(wxEVT_MENU, [this, tabs](wxCommandEvent&) { editor_->Send(Scintilla::Message::SetUseTabs, tabs); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, tabs](wxUpdateUIEvent& event) {
                event.Check((editor_->Send(Scintilla::Message::GetUseTabs) != 0) == tabs);
            },
            id);
    }
    indentation->AppendSeparator();
    for (const int width : {2, 4, 8}) {
        const int id = wxWindow::NewControlId();
        indentation->AppendRadioItem(id, wxString::Format("&%d Columns", width));
        Bind(wxEVT_MENU, [this, width](wxCommandEvent&) { editor_->SetIndentationWidth(width); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, width](wxUpdateUIEvent& event) {
                event.Check(editor_->Send(Scintilla::Message::GetTabWidth) == width);
            },
            id);
    }
    edit->AppendSubMenu(indentation, "&Indentation");
    auto* menus = new wxMenuBar;
    menus->Append(file, "&File");
    menus->Append(edit, "&Edit");
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
    menus->Append(languages, "&Language");
    auto* view = new wxMenu;
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
    Bind(wxEVT_CHAR_HOOK, [this, whitespace](wxKeyEvent& event) {
        if ((event.GetKeyCode() == '8' || event.GetKeyCode() == '*') && event.ControlDown() && event.ShiftDown() &&
            !event.AltDown() && !event.MetaDown()) {
            wxCommandEvent command(wxEVT_MENU, whitespace);
            GetEventHandler()->ProcessEvent(command);
        } else {
            event.Skip();
        }
    });
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetViewWS) != 0); }, whitespace);
    view->AppendSeparator();
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
    menus->Append(view, "&View");
    SetMenuBar(menus);
    CreateStatusBar(3);
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
        UpdatePositionStatus();
        event.Skip();
    });
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFind(); }, wxID_FIND);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFind(true); }, wxID_REPLACE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNext(); }, wxID_FORWARD);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindPrevious(); }, wxID_BACKWARD);
    Bind(wxEVT_CHAR_HOOK, &EditorFrame::OnSearchKey, this);
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
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Undo); }, wxID_UNDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Redo); }, wxID_REDO);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanUndo)); },
        wxID_UNDO);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanRedo)); },
        wxID_REDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Copy); }, wxID_COPY);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Cut); }, wxID_CUT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Paste); }, wxID_PASTE);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Enable(editor_->Send(Scintilla::Message::GetSelectionStart) !=
                         editor_->Send(Scintilla::Message::GetSelectionEnd));
        },
        wxID_COPY);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Enable(!editor_->Send(Scintilla::Message::GetReadOnly) &&
                         editor_->Send(Scintilla::Message::GetSelectionStart) !=
                             editor_->Send(Scintilla::Message::GetSelectionEnd));
        },
        wxID_CUT);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanPaste)); },
        wxID_PASTE);
    Bind(wxEVT_EDITOR_STATE_CHANGED, [this](wxCommandEvent&) { UpdateDocumentState(); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (event.CanVeto() && !ConfirmDiscardChanges("closing")) {
            event.Veto();
            return;
        }
        Destroy();
    });
    UpdateDocumentState();
    editor_->SetFocus();
}

void EditorFrame::ShowFind(bool replace) {
    if (!findDialog_)
        findDialog_ = new FindReplaceDialog(
            this, *editor_, search_, [this](const FindResult& result) { ReportFind(result); },
            [this](wxKeyEvent& event) { OnSearchKey(event); });
    findDialog_->Present(replace);
}

void EditorFrame::OnSearchKey(wxKeyEvent& event) {
    if (event.GetKeyCode() == 'W' && event.ControlDown() && event.ShiftDown() && !event.AltDown() && !event.MetaDown())
        editor_->ToggleWordWrap();
    else if (event.GetKeyCode() == 'N' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
             !event.MetaDown())
        editor_->SetLineNumbersVisible(!editor_->LineNumbersVisible());
    else if (event.GetKeyCode() == 'F' && event.ControlDown() && !event.AltDown() && !event.ShiftDown())
        ShowFind();
    else if (event.GetKeyCode() == 'H' && event.ControlDown() && !event.AltDown() && !event.ShiftDown())
        ShowFind(true);
    else if (event.GetKeyCode() == WXK_F3 && !event.HasAnyModifiers())
        FindNext();
    else if (event.GetKeyCode() == WXK_F3 && event.ShiftDown() && !event.ControlDown() && !event.AltDown() &&
             !event.MetaDown())
        FindPrevious();
    else
        event.Skip();
}

void EditorFrame::FindNext() {
    Search(true);
}

void EditorFrame::FindPrevious() {
    Search(false);
}

void EditorFrame::Search(bool forward) {
    if (!findDialog_) {
        ShowFind();
        return;
    }
    findDialog_->Find(forward ? SearchDirection::Forward : SearchDirection::Backward);
}

void EditorFrame::ReportFind(const FindResult& result) {
    switch (result.status) {
    case FindStatus::Found:
        SetStatusText(result.wrapped ? "Search wrapped" : "Match found", 2);
        break;
    case FindStatus::NotFound:
        SetStatusText("No match found", 2);
        break;
    case FindStatus::InvalidPattern:
        SetStatusText("Invalid regular expression", 2);
        break;
    case FindStatus::EmptyQuery:
        break;
    }
}

void EditorFrame::UpdateDocumentState() {
    const wxString name = document_.Path().empty() ? wxString("Untitled") : document_.Path();
    SetTitle((editor_->Send(Scintilla::Message::GetModify) ? "*" : "") + name + " - wxnotepad");
    const wxString encoding = document_.Encoding() == FileEncoding::Utf16Le ? "UTF-16LE"
                              : document_.Encoding() == FileEncoding::Gbk   ? "GBK (CP936)"
                                                                            : "UTF-8";
    SetStatusText(encoding + (document_.HasBom() ? " (BOM)" : "") + " | " + LanguageName(document_.Language()) +
                  (document_.LanguageOverride() ? " (override)" : ""));
    UpdatePositionStatus();
}

void EditorFrame::UpdatePositionStatus() {
    const auto caret = editor_->Send(Scintilla::Message::GetCurrentPos);
    const auto line = editor_->Send(Scintilla::Message::LineFromPosition, caret);
    const auto column = editor_->Send(Scintilla::Message::GetColumn, caret, line);
    wxString position =
        wxString::Format("Ln %lld, Col %lld", static_cast<long long>(line + 1), static_cast<long long>(column + 1));
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
    UpdateDocumentState();
}

SaveResult EditorFrame::SaveFirst(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss) {
    const auto result = document_.SaveFirst(destination, decideLoss ? decideLoss : [this] { return ConfirmGbkLoss(); });
    UpdateDocumentState();
    return result;
}

SaveResult EditorFrame::Save(const GbkLossDecision& decideLoss) {
    if (!document_.Path().empty()) {
        const auto result = document_.Save(decideLoss ? decideLoss : [this] { return ConfirmGbkLoss(); });
        UpdateDocumentState();
        return result;
    }
    wxFileDialog dialog(this, "Save file", {}, {}, "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return SaveFirst(std::nullopt, decideLoss);
    return SaveFirst(dialog.GetPath(), decideLoss);
}

SaveResult EditorFrame::SaveAs(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss) {
    const auto result = document_.SaveAs(destination, decideLoss ? decideLoss : [this] { return ConfirmGbkLoss(); });
    UpdateDocumentState();
    return result;
}

SaveResult EditorFrame::SaveAs() {
    const wxFileName current(document_.Path());
    wxFileDialog dialog(this, "Save As", current.GetPath(), current.GetFullName(),
                        "Text files (*.txt)|*.txt|All files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return SaveAs(std::nullopt);
    return SaveAs(dialog.GetPath());
}

OpenResult EditorFrame::Open(const std::optional<wxString>& source, std::optional<FileEncoding> encoding) {
    const auto result =
        document_.Open(source, [this] { return ConfirmDiscardChanges("opening another file"); }, encoding);
    if (result.status == OpenStatus::Opened) {
        GetMenuBar()->Check(automaticLanguageId_, true);
        search_.ResetMatch();
    }
    UpdateDocumentState();
    return result;
}

OpenResult EditorFrame::Open() {
    wxFileDialog dialog(this, "Open file", {}, {}, "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return Open(std::nullopt);
    return Open(dialog.GetPath());
}

OpenResult EditorFrame::OpenAs(FileEncoding encoding) {
    wxFileDialog dialog(this, "Open file", {}, {}, "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return Open(std::nullopt, encoding);
    return Open(dialog.GetPath(), encoding);
}

bool EditorFrame::New() {
    if (!ConfirmDiscardChanges("creating a new document")) return false;
    document_.New();
    GetMenuBar()->Check(automaticLanguageId_, true);
    search_.ResetMatch();
    UpdateDocumentState();
    return true;
}

bool EditorFrame::ConfirmDiscardChanges(const wxString& action) {
    if (!editor_->Send(Scintilla::Message::GetModify)) return true;
    const wxString name = document_.Path().empty() ? wxString("Untitled") : document_.Path();
    wxMessageDialog dialog(this, "Save changes to " + name + " before " + action + "?", "Save changes?",
                           wxYES_NO | wxCANCEL | wxICON_QUESTION);
    dialog.SetYesNoCancelLabels("&Save", "&Discard", "&Cancel");
    const int choice = dialog.ShowModal();
    if (choice == wxID_NO) return true;
    if (choice != wxID_YES) return false;
    const auto result = Save();
    if (result.status == SaveStatus::Failed)
        wxMessageBox(result.error, "Could not save file", wxOK | wxICON_ERROR, this);
    return result.status == SaveStatus::Saved;
}

GbkLossChoice EditorFrame::ConfirmGbkLoss() {
    wxMessageDialog dialog(
        this,
        "GBK cannot preserve every character in this document. Save as UTF-8 without a BOM to keep all text, "
        "or confirm GBK replacement. Replace in GBK changes unsupported characters to ? in both the file "
        "and document after saving; Undo restores the original text.",
        "GBK character loss", wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT | wxICON_WARNING);
    dialog.SetYesNoCancelLabels("Save as &UTF-8", "&Replace in GBK", "&Cancel");
    const auto choice = dialog.ShowModal();
    if (choice == wxID_YES) return GbkLossChoice::Utf8;
    if (choice == wxID_NO) return GbkLossChoice::Replace;
    return GbkLossChoice::Cancel;
}
