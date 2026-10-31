#include "editor_frame.h"
#include "editor_control.h"
#include <wx/menu.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/statusbr.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

EditorFrame::EditorFrame()
    : wxFrame(nullptr, wxID_ANY, "Untitled - wxnotepad", wxDefaultPosition, wxSize(900, 600)),
      editor_(new EditorControl(this)), document_(*editor_) {
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
    edit->Append(wxID_FORWARD, "Find &Next\tF3");
    auto* menus = new wxMenuBar;
    menus->Append(file, "&File");
    menus->Append(edit, "&Edit");
    SetMenuBar(menus);
    CreateStatusBar();
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFind(); }, wxID_FIND);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNext(); }, wxID_FORWARD);
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

void EditorFrame::ShowFind() {
    if (!findDialog_) {
        findDialog_ = new wxDialog(this, wxID_ANY, "Find");
        auto* layout = new wxBoxSizer(wxVERTICAL);
        layout->Add(new wxStaticText(findDialog_, wxID_ANY, "Find &what:"), 0, wxALL, 8);
        findQuery_ = new wxTextCtrl(findDialog_, wxID_ANY, {}, wxDefaultPosition, wxSize(320, -1), wxTE_PROCESS_ENTER);
        findQuery_->SetName("Find query");
        layout->Add(findQuery_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
        auto* matchCase = new wxCheckBox(findDialog_, wxID_ANY, "Match &case");
        matchCase->SetName("Match case");
        matchCase->SetValue(searchOptions_.matchCase);
        matchCase->Bind(wxEVT_CHECKBOX,
                        [this](wxCommandEvent& event) { searchOptions_.matchCase = event.IsChecked(); });
        layout->Add(matchCase, 0, wxLEFT | wxRIGHT | wxTOP, 8);
        auto* wholeWord = new wxCheckBox(findDialog_, wxID_ANY, "Whole &word");
        wholeWord->SetName("Whole word");
        wholeWord->SetValue(searchOptions_.wholeWord);
        wholeWord->Bind(wxEVT_CHECKBOX,
                        [this](wxCommandEvent& event) { searchOptions_.wholeWord = event.IsChecked(); });
        layout->Add(wholeWord, 0, wxLEFT | wxRIGHT | wxTOP, 8);
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        auto* next = new wxButton(findDialog_, wxID_FORWARD, "Find &Next");
        next->SetDefault();
        buttons->Add(next, 0, wxRIGHT, 8);
        buttons->Add(new wxButton(findDialog_, wxID_CANCEL, "Close"));
        layout->Add(buttons, 0, wxALL | wxALIGN_RIGHT, 8);
        findDialog_->SetSizerAndFit(layout);
        const wxAcceleratorEntry nextKey(wxACCEL_NORMAL, WXK_F3, wxID_FORWARD);
        findDialog_->SetAcceleratorTable(wxAcceleratorTable(1, &nextKey));
        findDialog_->Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNext(); }, wxID_FORWARD);
        findDialog_->Bind(wxEVT_CHAR_HOOK, &EditorFrame::OnSearchKey, this);
        findDialog_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { FindNext(); }, wxID_FORWARD);
        findQuery_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { FindNext(); });
        const auto close = [this] {
            findDialog_->Hide();
            editor_->SetFocus();
        };
        findDialog_->Bind(wxEVT_BUTTON, [close](wxCommandEvent&) { close(); }, wxID_CANCEL);
        findDialog_->Bind(wxEVT_CLOSE_WINDOW, [close](wxCloseEvent&) { close(); });
    }
    findDialog_->Show();
    findQuery_->SetFocus();
    findQuery_->SelectAll();
}

void EditorFrame::OnSearchKey(wxKeyEvent& event) {
    if (event.GetKeyCode() == 'F' && event.ControlDown() && !event.AltDown() && !event.ShiftDown())
        ShowFind();
    else if (event.GetKeyCode() == WXK_F3 && !event.HasAnyModifiers())
        FindNext();
    else
        event.Skip();
}

void EditorFrame::FindNext() {
    if (!findQuery_) {
        ShowFind();
        return;
    }
    const wxString queryText = findQuery_->GetValue();
    const auto query = queryText.utf8_str();
    if (!query.length()) return;
    using Scintilla::Message;
    editor_->Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    editor_->Send(Message::SetTargetStart, editor_->Send(Message::GetSelectionEnd));
    editor_->Send(Message::SetTargetEnd, editor_->Send(Message::GetLength));
    if (editor_->Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data())) < 0) {
        SetStatusText("No match found");
        return;
    }
    editor_->Send(Message::SetSel, editor_->Send(Message::GetTargetStart), editor_->Send(Message::GetTargetEnd));
    editor_->Send(Message::ScrollCaret);
}

Scintilla::FindOption EditorFrame::SearchFlags() const {
    auto flags = Scintilla::FindOption::None;
    if (searchOptions_.matchCase) flags = flags | Scintilla::FindOption::MatchCase;
    if (searchOptions_.wholeWord) flags = flags | Scintilla::FindOption::WholeWord;
    return flags;
}

void EditorFrame::UpdateDocumentState() {
    const wxString name = document_.Path().empty() ? wxString("Untitled") : document_.Path();
    SetTitle((editor_->Send(Scintilla::Message::GetModify) ? "*" : "") + name + " - wxnotepad");
    const wxString encoding = document_.Encoding() == FileEncoding::Utf16Le ? "UTF-16LE"
                              : document_.Encoding() == FileEncoding::Gbk   ? "GBK (CP936)"
                                                                            : "UTF-8";
    SetStatusText(encoding + (document_.HasBom() ? " (BOM)" : ""));
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
