#include "app/dialogs/find_replace_dialog.h"
#include "search/search_limits.h"
#include "editor/editor_control.h"
#include "search/find_text.h"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/hyperlink.h>
#include <string>
#include <utility>

FindReplaceDialog::FindReplaceDialog(wxWindow* parent, EditorControl& editor, SearchSession& search,
                                     std::function<void(const FindResult&)> reportFind,
                                     std::function<void(wxKeyEvent&)> onKey, int selectionOptions)
    : wxDialog(parent, wxID_ANY, "Find"), editor_(editor), search_(search), reportFind_(std::move(reportFind)),
      selectionOptions_(selectionOptions) {
    const auto& request = search_.Request();
    long textStyle = wxTE_PROCESS_ENTER;
#ifdef __WXGTK__
    // GtkEntry truncates at 65534 bytes, below the original 1 MiB selection
    // limit. A native GtkTextView retains the full query in the same row.
    textStyle |= wxTE_MULTILINE | wxTE_DONTWRAP | wxTE_NO_VSCROLL;
#endif
    auto* layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(new wxStaticText(this, wxID_ANY, "Find &what:"), 0, wxALL, 8);
    query_ = new wxTextCtrl(this, wxID_ANY, request.query, wxDefaultPosition, wxSize(320, -1), textStyle);
    query_->SetMaxLength(0);
    query_->SetMinSize(wxSize(320, query_->GetCharHeight() + FromDIP(12)));
    query_->SetName("Find query");
    layout->Add(query_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    matchCase_ = new wxCheckBox(this, wxID_ANY, "Match &case");
    matchCase_->SetName("Match case");
    matchCase_->SetValue(request.options.matchCase);
    layout->Add(matchCase_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    wholeWord_ = new wxCheckBox(this, wxID_ANY, "Whole &word");
    wholeWord_->SetName("Whole word");
    wholeWord_->SetValue(request.options.wholeWord);
    wholeWord_->SetToolTip("Applies to literal searches. Use word boundaries in a regular expression.");
    wholeWord_->Enable(!request.options.regularExpression);
    layout->Add(wholeWord_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    regularExpression_ = new wxCheckBox(this, wxID_ANY, "Regular &expression");
    regularExpression_->SetName("Regular expression");
    regularExpression_->SetValue(request.options.regularExpression);
    layout->Add(regularExpression_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    transformBackslashes_ = new wxCheckBox(this, wxID_ANY, "Transform &backslashes");
    transformBackslashes_->SetName("Transform backslashes");
    transformBackslashes_->SetValue(request.options.transformBackslashes);
    layout->Add(transformBackslashes_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    wildcardSearch_ = new wxCheckBox(this, wxID_ANY, "&Wildcard search");
    wildcardSearch_->SetName("Wildcard search");
    wildcardSearch_->SetValue(request.options.wildcardSearch);
    wildcardSearch_->SetToolTip("* matches zero or more characters; ? matches exactly one character.");
    layout->Add(wildcardSearch_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    replacementLabel_ = new wxStaticText(this, wxID_ANY, "Replace &with:");
    layout->Add(replacementLabel_, 0, wxALL, 8);
    replacement_ = new wxTextCtrl(this, wxID_ANY, request.replacement, wxDefaultPosition, wxSize(320, -1), textStyle);
    replacement_->SetMaxLength(0);
    replacement_->SetMinSize(wxSize(320, replacement_->GetCharHeight() + FromDIP(12)));
    replacement_->SetName("Replacement text");
    layout->Add(replacement_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    wrapAround_ = new wxCheckBox(this, wxID_ANY, "&Wrap around");
    wrapAround_->SetName("Wrap around");
    wrapAround_->SetValue(request.wrapAround);
    layout->Add(wrapAround_, 0, wxALL, 8);
    bookmarkMatchedLines_ = new wxCheckBox(this, wxID_ANY, "Boo&kmark matched line");
    bookmarkMatchedLines_->SetName("Bookmark matched line");
    bookmarkMatchedLines_->SetValue(request.bookmarkMatchedLines);
    layout->Add(bookmarkMatchedLines_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* next = new wxButton(this, wxID_FORWARD, "Find &Next");
    next->SetDefault();
    buttons->Add(next, 0, wxRIGHT, 8);
    findAllButton_ = new wxButton(this, wxID_ANY, "&Find All");
    findAllButton_->SetName("Find All");
    findAllButton_->Enable(!query_->IsEmpty());
    buttons->Add(findAllButton_, 0, wxRIGHT, 8);
    replaceButton_ = new wxButton(this, wxID_REPLACE, "&Replace");
    buttons->Add(replaceButton_, 0, wxRIGHT, 8);
    replaceAllButton_ = new wxButton(this, wxID_REPLACE_ALL, "Replace &All");
    buttons->Add(replaceAllButton_, 0, wxRIGHT, 8);
    replaceSelectionButton_ = new wxButton(this, wxID_ANY, "In &Selection");
    replaceSelectionButton_->SetName("Replace in selection");
    replaceSelectionButton_->Enable(!query_->IsEmpty());
    buttons->Add(replaceSelectionButton_, 0, wxRIGHT, 8);
    buttons->Add(new wxButton(this, wxID_BACKWARD, "Find &Previous"), 0, wxRIGHT, 8);
    buttons->Add(new wxButton(this, wxID_CANCEL, "Close"));
    layout->Add(buttons, 0, wxALL | wxALIGN_RIGHT, 8);
    findAllResult_ = new wxStaticText(this, wxID_ANY, "");
    findAllResult_->SetName("Find All result");
    layout->Add(findAllResult_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    replacementResult_ = new wxStaticText(this, wxID_ANY, "");
    replacementResult_->SetName("Replacement result");
    layout->Add(replacementResult_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    modeLink_ = new wxHyperlinkCtrl(this, wxID_ANY, "Switch to Replace", "");
    modeLink_->SetName("Switch Find/Replace");
    modeLink_->Bind(wxEVT_HYPERLINK, [this](wxHyperlinkEvent&) { Present(!replace_, selectionOptions_, true); });
    layout->Add(modeLink_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    SetSizerAndFit(layout);

    query_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        UpdateButtons();
        event.Skip();
    });
    regularExpression_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (regularExpression_->GetValue()) {
            transformBackslashes_->SetValue(false);
            wildcardSearch_->SetValue(false);
        }
        wholeWord_->Enable(!regularExpression_->GetValue());
    });
    transformBackslashes_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (transformBackslashes_->GetValue()) regularExpression_->SetValue(false);
        wholeWord_->Enable(!regularExpression_->GetValue());
    });
    wildcardSearch_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        regularExpression_->SetValue(false);
        wholeWord_->Enable();
    });
    bookmarkMatchedLines_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        auto request = search_.Request();
        request.bookmarkMatchedLines = bookmarkMatchedLines_->GetValue();
        search_.Configure(std::move(request));
    });
    parent->Bind(wxEVT_EDITOR_STATE_CHANGED, &FindReplaceDialog::OnEditorState, this);
    const int selectNext = wxWindow::NewControlId();
    const int selectPrevious = wxWindow::NewControlId();
    const wxAcceleratorEntry searchKeys[] = {{wxACCEL_NORMAL, WXK_F3, wxID_FORWARD},
                                             {wxACCEL_SHIFT, WXK_F3, wxID_BACKWARD},
                                             {wxACCEL_CTRL | wxACCEL_ALT, WXK_F2, selectNext},
                                             {wxACCEL_CTRL | wxACCEL_ALT | wxACCEL_SHIFT, WXK_F2, selectPrevious}};
    SetAcceleratorTable(wxAcceleratorTable(4, searchKeys));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Find(SearchDirection::Forward); }, wxID_FORWARD);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Find(SearchDirection::Backward); }, wxID_BACKWARD);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Find(SearchDirection::Forward, true); }, selectNext);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Find(SearchDirection::Backward, true); }, selectPrevious);
    Bind(wxEVT_CHAR_HOOK, [this, onKey = std::move(onKey)](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_F3 && !event.ControlDown() && !event.AltDown() && !event.MetaDown())
            Find(event.ShiftDown() ? SearchDirection::Backward : SearchDirection::Forward);
        else if (event.GetKeyCode() == WXK_F2 && event.ControlDown() && event.AltDown() && !event.MetaDown())
            Find(event.ShiftDown() ? SearchDirection::Backward : SearchDirection::Forward, true);
        else
            onKey(event);
    });
    Bind(
        wxEVT_BUTTON, [this](wxCommandEvent&) { Find(SearchDirection::Forward, wxGetKeyState(WXK_SHIFT)); },
        wxID_FORWARD);
    Bind(
        wxEVT_BUTTON, [this](wxCommandEvent&) { Find(SearchDirection::Backward, wxGetKeyState(WXK_SHIFT)); },
        wxID_BACKWARD);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { FindAll(); }, findAllButton_->GetId());
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReplaceOne(); }, wxID_REPLACE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReplaceAll(); }, wxID_REPLACE_ALL);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReplaceAll(true); }, replaceSelectionButton_->GetId());
    query_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { Find(SearchDirection::Forward); });
    replacement_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { Find(SearchDirection::Forward); });
    const auto close = [this, &editor] {
        Hide();
        editor.SetFocus();
    };
    Bind(wxEVT_BUTTON, [close](wxCommandEvent&) { close(); }, wxID_CANCEL);
    Bind(wxEVT_CLOSE_WINDOW, [close](wxCloseEvent&) { close(); });
    UpdateButtons();
}

FindReplaceDialog::~FindReplaceDialog() {
    GetParent()->Unbind(wxEVT_EDITOR_STATE_CHANGED, &FindReplaceDialog::OnEditorState, this);
}

void FindReplaceDialog::OnEditorState(wxCommandEvent& event) {
    if (!search_.MatchCount().has_value() &&
        (!findAllErrorRevision_.has_value() || *findAllErrorRevision_ != editor_.DocumentRevision())) {
        findAllResult_->SetLabel("");
        findAllErrorRevision_.reset();
    }
    event.Skip();
}

void FindReplaceDialog::FindAll() {
    if (query_->IsEmpty()) return;
    ConfigureSearch();
    const auto result = search_.FindAll();
    findAllErrorRevision_ =
        result.status == FindStatus::InvalidPattern ? std::optional(editor_.DocumentRevision()) : std::nullopt;
    findAllResult_->SetLabel(result.status == FindStatus::InvalidPattern
                                 ? wxString("Invalid regular expression")
                                 : wxString::Format("%zu matches", result.count));
    GetSizer()->Fit(this);
}

void FindReplaceDialog::Present(bool replace, int selectionOptions, bool toggle) {
    selectionOptions_ = selectionOptions;
    const bool firstTime = !IsShown() || replace_ != replace;
    if (IsShown() && replace_ != replace) {
        // Ctrl+F/Ctrl+H retain edited text across the dialog switch. Options
        // are committed only by a search/replace command in the original.
        auto request = search_.Request();
        request.query = query_->GetValue();
        if (replace_) request.replacement = replacement_->GetValue();
        search_.Configure(std::move(request));
    }
    if (firstTime) {
        const auto& request = search_.Request();
        query_->ChangeValue(request.query);
        replacement_->ChangeValue(request.replacement);
        if (query_->IsEmpty() && !search_.RecentFind().empty()) query_->ChangeValue(search_.RecentFind().front());
        if (replacement_->IsEmpty() && !search_.LastReplaceEmpty() && !search_.RecentReplace().empty())
            replacement_->ChangeValue(search_.RecentReplace().front());
        matchCase_->SetValue(request.options.matchCase);
        wholeWord_->SetValue(request.options.wholeWord);
        regularExpression_->SetValue(request.options.regularExpression);
        transformBackslashes_->SetValue(request.options.transformBackslashes);
        wildcardSearch_->SetValue(request.options.wildcardSearch);
        wrapAround_->SetValue(request.wrapAround);
        bookmarkMatchedLines_->SetValue(request.bookmarkMatchedLines);
        wholeWord_->Enable(!request.options.regularExpression);
    }
    // The explicit dialog link sets original bSwitchedFindReplace == 3,
    // suppressing both seed sources. Ctrl+F/Ctrl+H switch with value 2.
    const bool selected = !toggle && SeedQuery(firstTime);
    UpdateButtons();
    replace_ = replace;
    SetTitle(replace ? "Replace" : "Find");
    modeLink_->SetLabel(replace ? "Switch to Find" : "Switch to Replace");
    replacementLabel_->Show(replace);
    replacement_->Show(replace);
    replaceButton_->Show(replace);
    replaceAllButton_->Show(replace);
    replaceSelectionButton_->Show(replace);
    replacementResult_->Show(replace);
    GetSizer()->Fit(this);
    Show();
    auto* focus = selected && replace ? replacement_ : query_;
    focus->SetFocus();
    focus->SelectAll();
}

bool FindReplaceDialog::SeedQuery(bool firstTime) {
    // Pinned Edit.cpp::CopySelectionAsFindText and Helpers.cpp::AddBackslashW.
    const auto length = editor_.Send(Scintilla::Message::GetSelText);
    wxString text;
    if (length > 0 && static_cast<uintptr_t>(length) <= SearchLimits::SelectionBytes &&
        (selectionOptions_ & SelectOption_CopySelectionAsFindText)) {
        std::string bytes(static_cast<size_t>(length) + 1, '\0');
        editor_.Send(Scintilla::Message::GetSelText, 0, reinterpret_cast<intptr_t>(bytes.data()));
        text = wxString::FromUTF8(bytes.c_str());
    }
    const bool selected = !text.empty();
    if (!selected && firstTime && (selectionOptions_ & SelectOption_CopyPasteBufferAsFindText) &&
        wxTheClipboard->Open()) {
        wxTextDataObject data;
        if (wxTheClipboard->IsSupported(wxDF_UNICODETEXT) && wxTheClipboard->GetData(data)) {
            // Original EditGetClipboardTextW normalizes clipboard line endings
            // to the document's insertion mode before AddBackslashW.
            text = data.GetText();
            text.Replace("\r\n", "\n");
            text.Replace("\r", "\n");
            const auto mode = static_cast<Scintilla::EndOfLine>(editor_.Send(Scintilla::Message::GetEOLMode));
            if (mode == Scintilla::EndOfLine::CrLf)
                text.Replace("\n", "\r\n");
            else if (mode == Scintilla::EndOfLine::Cr)
                text.Replace("\n", "\r");
        }
        wxTheClipboard->Close();
    }
    if (!text.empty()) {
        const auto escaped = EscapeFindText(text);
        transformBackslashes_->SetValue(escaped.controls && !search_.Request().options.regularExpression);
        auto request = search_.Request();
        request.options.transformBackslashes = transformBackslashes_->GetValue();
        search_.Configure(std::move(request));
        // APPM_COPYDATA in the original leaves the existing query control alone.
        if (firstTime) query_->ChangeValue(escaped.text);
    }
    return selected;
}

void FindReplaceDialog::ConfigureSearch() {
    UpdateButtons();
    if (query_->IsEmpty()) return;
    auto request = search_.Request();
    request.query = query_->GetValue();
    if (replace_) request.replacement = replacement_->GetValue();
    request.options = {matchCase_->GetValue(), wholeWord_->GetValue(), regularExpression_->GetValue(),
                       transformBackslashes_->GetValue(), wildcardSearch_->GetValue()};
    request.wrapAround = wrapAround_->GetValue();
    request.bookmarkMatchedLines = bookmarkMatchedLines_->GetValue();
    search_.Configure(std::move(request));
    search_.Remember(replace_);
    if (!search_.MatchCount().has_value()) {
        findAllResult_->SetLabel("");
        findAllErrorRevision_.reset();
    }
}

void FindReplaceDialog::UpdateButtons() {
    for (const int id : {wxID_FORWARD, wxID_BACKWARD, wxID_REPLACE, wxID_REPLACE_ALL})
        FindWindow(id)->Enable(!query_->IsEmpty());
    replaceSelectionButton_->Enable(!query_->IsEmpty());
    findAllButton_->Enable(!query_->IsEmpty());
}

void FindReplaceDialog::Find(SearchDirection direction, bool extendSelection) {
    if (query_->IsEmpty()) return;
    ConfigureSearch();
    reportFind_(search_.Find(direction, extendSelection));
}

void FindReplaceDialog::ReplaceOne() {
    if (query_->IsEmpty()) return;
    ConfigureSearch();
    const auto result = search_.ReplaceOne();
    if (result.next) reportFind_(*result.next);
}

void FindReplaceDialog::ReplaceAll(bool inSelection) {
    if (query_->IsEmpty()) return;
    ConfigureSearch();
    const auto result = inSelection ? search_.ReplaceInSelection() : search_.ReplaceAll();
    if (result.status == ReplaceAllStatus::RectangularSelection) {
        wxMessageBox("This operation can't be perfomed within a rectangular selection.", "wxnotepad",
                     wxOK | wxICON_WARNING, this);
        return;
    }
    if (result.status == ReplaceAllStatus::ExpansionFailed) return;
    replacementResult_->SetLabel(result.status == ReplaceAllStatus::InvalidPattern
                                     ? wxString("Invalid regular expression")
                                     : wxString::Format("%zu replacements", result.count));
    GetSizer()->Fit(this);
}
