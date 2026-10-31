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
#include <string>
#include <string_view>
#include <wx/checkbox.h>

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
    edit->Append(wxID_REPLACE, "&Replace...\tCtrl+H");
    edit->Append(wxID_FORWARD, "Find &Next\tF3");
    edit->Append(wxID_BACKWARD, "Find &Previous\tShift+F3");
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
            wxEVT_UPDATE_UI, [this, language](wxUpdateUIEvent& event) { event.Check(languageOverride_ == language); },
            id);
        return id;
    };
    automaticLanguageId_ = addLanguage("&Automatic", std::nullopt);
    addLanguage("&Plain Text", SyntaxLanguage::PlainText);
    addLanguage("&C/C++", SyntaxLanguage::Cpp);
    addLanguage("&Python", SyntaxLanguage::Python);
    addLanguage("&JSON", SyntaxLanguage::Json);
    menus->Append(languages, "&Language");
    SetMenuBar(menus);
    CreateStatusBar();
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
    if (!findDialog_) {
        findDialog_ = new wxDialog(this, wxID_ANY, "Find");
        auto* layout = new wxBoxSizer(wxVERTICAL);
        layout->Add(new wxStaticText(findDialog_, wxID_ANY, "Find &what:"), 0, wxALL, 8);
        findQuery_ = new wxTextCtrl(findDialog_, wxID_ANY, {}, wxDefaultPosition, wxSize(320, -1), wxTE_PROCESS_ENTER);
        findQuery_->SetName("Find query");
        findQuery_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
            lastEmptyMatch_.reset();
            regexMatch_.reset();
            event.Skip();
        });
        layout->Add(findQuery_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
        auto* matchCase = new wxCheckBox(findDialog_, wxID_ANY, "Match &case");
        matchCase->SetName("Match case");
        matchCase->SetValue(searchOptions_.matchCase);
        matchCase->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
            searchOptions_.matchCase = event.IsChecked();
            lastEmptyMatch_.reset();
            regexMatch_.reset();
        });
        layout->Add(matchCase, 0, wxLEFT | wxRIGHT | wxTOP, 8);
        auto* wholeWord = new wxCheckBox(findDialog_, wxID_ANY, "Whole &word");
        wholeWord->SetName("Whole word");
        wholeWord->SetValue(searchOptions_.wholeWord);
        wholeWord->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
            searchOptions_.wholeWord = event.IsChecked();
            lastEmptyMatch_.reset();
            regexMatch_.reset();
        });
        layout->Add(wholeWord, 0, wxLEFT | wxRIGHT | wxTOP, 8);
        auto* regularExpression = new wxCheckBox(findDialog_, wxID_ANY, "Regular &expression");
        regularExpression->SetName("Regular expression");
        regularExpression->SetValue(searchOptions_.regularExpression);
        wholeWord->SetToolTip("Applies to literal searches. Use word boundaries in a regular expression.");
        wholeWord->Enable(!searchOptions_.regularExpression);
        regularExpression->Bind(wxEVT_CHECKBOX, [this, wholeWord](wxCommandEvent& event) {
            searchOptions_.regularExpression = event.IsChecked();
            lastEmptyMatch_.reset();
            regexMatch_.reset();
            wholeWord->Enable(!event.IsChecked());
        });
        layout->Add(regularExpression, 0, wxLEFT | wxRIGHT | wxTOP, 8);
        replacementLabel_ = new wxStaticText(findDialog_, wxID_ANY, "Replace &with:");
        layout->Add(replacementLabel_, 0, wxALL, 8);
        replacement_ = new wxTextCtrl(findDialog_, wxID_ANY, {}, wxDefaultPosition, wxSize(320, -1));
        replacement_->SetName("Replacement text");
        layout->Add(replacement_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
        wrapAround_ = new wxCheckBox(findDialog_, wxID_ANY, "&Wrap around");
        wrapAround_->SetName("Wrap around");
        wrapAround_->SetValue(true);
        wrapAround_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { regexMatch_.reset(); });
        layout->Add(wrapAround_, 0, wxALL, 8);
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        auto* next = new wxButton(findDialog_, wxID_FORWARD, "Find &Next");
        next->SetDefault();
        buttons->Add(next, 0, wxRIGHT, 8);
        replaceButton_ = new wxButton(findDialog_, wxID_REPLACE, "&Replace");
        buttons->Add(replaceButton_, 0, wxRIGHT, 8);
        replaceAllButton_ = new wxButton(findDialog_, wxID_REPLACE_ALL, "Replace &All");
        buttons->Add(replaceAllButton_, 0, wxRIGHT, 8);
        buttons->Add(new wxButton(findDialog_, wxID_BACKWARD, "Find &Previous"), 0, wxRIGHT, 8);
        buttons->Add(new wxButton(findDialog_, wxID_CANCEL, "Close"));
        layout->Add(buttons, 0, wxALL | wxALIGN_RIGHT, 8);
        replacementResult_ = new wxStaticText(findDialog_, wxID_ANY, "");
        replacementResult_->SetName("Replacement result");
        layout->Add(replacementResult_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        findDialog_->SetSizerAndFit(layout);
        const wxAcceleratorEntry searchKeys[] = {{wxACCEL_NORMAL, WXK_F3, wxID_FORWARD},
                                                 {wxACCEL_SHIFT, WXK_F3, wxID_BACKWARD}};
        findDialog_->SetAcceleratorTable(wxAcceleratorTable(2, searchKeys));
        findDialog_->Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNext(); }, wxID_FORWARD);
        findDialog_->Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindPrevious(); }, wxID_BACKWARD);
        findDialog_->Bind(wxEVT_CHAR_HOOK, &EditorFrame::OnSearchKey, this);
        findDialog_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { FindNext(); }, wxID_FORWARD);
        findDialog_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReplaceOne(); }, wxID_REPLACE);
        findDialog_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReplaceAll(); }, wxID_REPLACE_ALL);
        findDialog_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { FindPrevious(); }, wxID_BACKWARD);
        findQuery_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { FindNext(); });
        const auto close = [this] {
            findDialog_->Hide();
            editor_->SetFocus();
        };
        findDialog_->Bind(wxEVT_BUTTON, [close](wxCommandEvent&) { close(); }, wxID_CANCEL);
        findDialog_->Bind(wxEVT_CLOSE_WINDOW, [close](wxCloseEvent&) { close(); });
    }
    findDialog_->SetTitle(replace ? "Replace" : "Find");
    replacementLabel_->Show(replace);
    replacement_->Show(replace);
    replaceButton_->Show(replace);
    replaceAllButton_->Show(replace);
    replacementResult_->Show(replace);
    findDialog_->GetSizer()->Fit(findDialog_);
    findDialog_->Show();
    findQuery_->SetFocus();
    findQuery_->SelectAll();
}

void EditorFrame::OnSearchKey(wxKeyEvent& event) {
    if (event.GetKeyCode() == 'F' && event.ControlDown() && !event.AltDown() && !event.ShiftDown())
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
    regexMatch_.reset();
    if (!findQuery_) {
        ShowFind();
        return;
    }
    const wxString queryText = findQuery_->GetValue();
    const auto query = queryText.utf8_str();
    if (!query.length()) return;
    using Scintilla::Message;
    editor_->Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    editor_->Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
    const auto start = editor_->Send(forward ? Message::GetSelectionEnd : Message::GetSelectionStart);
    const auto length = editor_->Send(Message::GetLength);
    const bool afterEmpty = searchOptions_.regularExpression && lastEmptyMatch_ && start == lastEmptyMatch_->position &&
                            editor_->Send(Message::GetSelectionStart) == editor_->Send(Message::GetSelectionEnd) &&
                            lastEmptyMatch_->documentRevision == editor_->DocumentRevision() &&
                            lastEmptyMatch_->selectionRevision == editor_->SelectionRevision();
    const auto search = [&](intptr_t from, intptr_t to) {
        editor_->Send(Message::SetTargetStart, from);
        editor_->Send(Message::SetTargetEnd, to);
        return editor_->Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data())) >= 0;
    };
    bool wrapped = false;
    bool found = false;
    if (searchOptions_.regularExpression && !forward) {
        // The pinned Boost backward path is line-by-line. Scan forward so
        // Previous sees the same explicit cross-line matches as Next.
        intptr_t previousStart = -1, previousEnd = -1;
        intptr_t lastStart = -1, lastEnd = -1;
        for (intptr_t position = 0; position <= length;) {
            if (!search(position, length)) break;
            lastStart = editor_->Send(Message::GetTargetStart);
            lastEnd = editor_->Send(Message::GetTargetEnd);
            if (lastEnd <= start && !(afterEmpty && lastStart == start && lastEnd == start)) {
                previousStart = lastStart;
                previousEnd = lastEnd;
            }
            if (lastStart == lastEnd) {
                if (lastEnd == length) break;
                position = editor_->Send(Message::PositionAfter, lastEnd);
            } else {
                position = lastEnd;
            }
        }
        if (previousStart < 0 && wrapAround_->GetValue()) {
            wrapped = true;
            previousStart = lastStart;
            previousEnd = lastEnd;
        }
        found = previousStart >= 0;
        if (found) {
            editor_->Send(Message::SetTargetStart, previousStart);
            editor_->Send(Message::SetTargetEnd, previousEnd);
        }
    } else {
        found = !(afterEmpty && start == length) &&
                search(afterEmpty ? editor_->Send(Message::PositionAfter, start) : start, forward ? length : 0);
    }
    if (!found && wrapAround_->GetValue() && !(searchOptions_.regularExpression && !forward)) {
        wrapped = true;
        found = search(forward ? 0 : length, searchOptions_.regularExpression ? length : start);
        if (searchOptions_.regularExpression && found && editor_->Send(Message::GetTargetStart) > start) found = false;
    }
    if (editor_->Send(Message::GetStatus) == static_cast<intptr_t>(Scintilla::Status::RegEx)) {
        SetStatusText("Invalid regular expression");
        return;
    }
    if (!found) {
        SetStatusText("No match found");
        return;
    }
    const auto matchStart = editor_->Send(Message::GetTargetStart);
    const auto matchEnd = editor_->Send(Message::GetTargetEnd);
    const auto firstLine = editor_->Send(Message::LineFromPosition, matchStart);
    // The target is end-exclusive. A zero-width match still needs its own line;
    // a match ending at the next line start need not open that line's folds.
    const auto lastPosition = matchEnd > matchStart ? editor_->Send(Message::PositionBefore, matchEnd) : matchStart;
    const auto lastLine = editor_->Send(Message::LineFromPosition, lastPosition);
    for (auto line = firstLine; line <= lastLine; ++line)
        editor_->Send(Message::EnsureVisible, line);
    editor_->Send(Message::SetSel, matchStart, matchEnd);
    lastEmptyMatch_.reset();
    if (searchOptions_.regularExpression && matchStart == matchEnd)
        lastEmptyMatch_ = EmptyMatch{matchStart, editor_->DocumentRevision(), editor_->SelectionRevision()};
    if (searchOptions_.regularExpression)
        regexMatch_ = RegexMatch{matchStart, matchEnd, editor_->DocumentRevision(), editor_->SelectionRevision()};
    editor_->Send(Message::ScrollCaret);
    SetStatusText(wrapped ? "Search wrapped" : "Match found");
}

Scintilla::FindOption EditorFrame::SearchFlags() const {
    auto flags = Scintilla::FindOption::None;
    if (searchOptions_.matchCase) flags = flags | Scintilla::FindOption::MatchCase;
    if (searchOptions_.wholeWord) flags = flags | Scintilla::FindOption::WholeWord;
    if (searchOptions_.regularExpression)
        flags = flags | Scintilla::FindOption::RegExp | Scintilla::FindOption::Cxx11RegEx;
    return flags;
}

void EditorFrame::ReplaceOne() {
    const wxString queryText = findQuery_->GetValue();
    const auto query = queryText.utf8_str();
    if (!query.length()) return;
    using Scintilla::Message;
    const auto start = editor_->Send(Message::GetSelectionStart);
    const auto end = editor_->Send(Message::GetSelectionEnd);
    if (searchOptions_.regularExpression && (!regexMatch_ || regexMatch_->start != start || regexMatch_->end != end ||
                                             regexMatch_->documentRevision != editor_->DocumentRevision() ||
                                             regexMatch_->selectionRevision != editor_->SelectionRevision())) {
        FindNext();
        return;
    }
    editor_->Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    editor_->Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
    editor_->Send(Message::SetTargetStart, start);
    editor_->Send(Message::SetTargetEnd, searchOptions_.regularExpression ? editor_->Send(Message::GetLength) : end);
    // Regex assertions retain document-end context; the core patch also retains
    // preceding context. Both result endpoints must still equal the selection.
    if ((start == end && !(searchOptions_.regularExpression && lastEmptyMatch_ && lastEmptyMatch_->position == start &&
                           lastEmptyMatch_->documentRevision == editor_->DocumentRevision())) ||
        editor_->Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data())) != start ||
        editor_->Send(Message::GetTargetEnd) != end) {
        FindNext();
        return;
    }
    const wxString replacementText = replacement_->GetValue();
    const auto replacementUtf8 = replacementText.utf8_str();
    // The original formatter reads the byte after a trailing dollar/backslash.
    const std::string replacement(replacementUtf8.data(), replacementUtf8.length());
    auto after = end;
    bool changesBytes = true;
    if (searchOptions_.regularExpression) {
        const auto expanded = editor_->ExpandRegexReplacement(replacement);
        if (!expanded) return;
        std::string selected(static_cast<size_t>(end - start) + 1, '\0');
        editor_->Send(Message::GetTargetText, 0, reinterpret_cast<intptr_t>(selected.data()));
        selected.pop_back();
        changesBytes = *expanded != selected;
    }
    if (changesBytes) {
        editor_->Send(searchOptions_.regularExpression ? Message::ReplaceTargetRE : Message::ReplaceTarget,
                      replacement.length(), reinterpret_cast<intptr_t>(replacement.c_str()));
        after = editor_->Send(Message::GetTargetEnd);
    }
    editor_->Send(Message::SetSel, after, after);
    if (searchOptions_.regularExpression && start == end) {
        // Skip inserted bytes, then one original character. EOF is processed once.
        lastEmptyMatch_ = EmptyMatch{after, editor_->DocumentRevision(), editor_->SelectionRevision()};
    }
    FindNext();
}

void EditorFrame::ReplaceAll() {
    const wxString queryText = findQuery_->GetValue();
    const auto query = queryText.utf8_str();
    const wxString replacementText = replacement_->GetValue();
    const auto replacement = replacementText.utf8_str();
    using Scintilla::Message;
    size_t count = 0;
    bool editing = false;
    intptr_t start = 0;
    auto limit = editor_->Send(Message::GetLength);
    editor_->Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    // Regex may match the empty document or the boundary after a consuming match.
    while (query.length() && (start < limit || (searchOptions_.regularExpression && start == limit))) {
        editor_->Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
        editor_->Send(Message::SetTargetStart, start);
        editor_->Send(Message::SetTargetEnd, limit);
        if (editor_->Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data())) < 0) {
            if (editor_->Send(Message::GetStatus) == static_cast<intptr_t>(Scintilla::Status::RegEx)) {
                replacementResult_->SetLabel("Invalid regular expression");
                findDialog_->GetSizer()->Fit(findDialog_);
                return;
            }
            break;
        }
        const auto matchStart = editor_->Send(Message::GetTargetStart);
        const auto matchEnd = editor_->Send(Message::GetTargetEnd);
        const auto length = static_cast<size_t>(matchEnd - matchStart);
        std::string matched(length + 1, '\0');
        editor_->Send(Message::GetTargetText, 0, reinterpret_cast<intptr_t>(matched.data()));
        matched.resize(length);
        const std::string replacementBytes(replacement.data(), replacement.length());
        const auto expanded = searchOptions_.regularExpression ? editor_->ExpandRegexReplacement(replacementBytes)
                                                               : std::optional<std::string>(replacementBytes);
        if (!expanded) return;
        if (matched != *expanded) {
            if (!editing) {
                editor_->Send(Message::BeginUndoAction);
                editing = true;
            }
            const auto inserted =
                editor_->Send(searchOptions_.regularExpression ? Message::ReplaceTargetRE : Message::ReplaceTarget,
                              replacementBytes.size(), reinterpret_cast<intptr_t>(replacementBytes.c_str()));
            limit += inserted - (matchEnd - matchStart);
            start = matchStart + inserted;
        } else {
            start = matchEnd;
        }
        ++count;
        if (matchStart == matchEnd) {
            if (start == limit) break; // This EOF boundary has now been visited.
            // Skip inserted bytes, then one original character. Never split UTF-8;
            // the core normalizes a candidate between CR and LF to the next line.
            start = editor_->Send(Message::PositionAfter, start);
        }
    }
    if (editing) editor_->Send(Message::EndUndoAction);
    replacementResult_->SetLabel(wxString::Format("%zu replacements", count));
    findDialog_->GetSizer()->Fit(findDialog_);
}

void EditorFrame::UpdateDocumentState() {
    const wxString name = document_.Path().empty() ? wxString("Untitled") : document_.Path();
    SetTitle((editor_->Send(Scintilla::Message::GetModify) ? "*" : "") + name + " - wxnotepad");
    const wxString encoding = document_.Encoding() == FileEncoding::Utf16Le ? "UTF-16LE"
                              : document_.Encoding() == FileEncoding::Gbk   ? "GBK (CP936)"
                                                                            : "UTF-8";
    SetStatusText(encoding + (document_.HasBom() ? " (BOM)" : "") + " | " + LanguageName(language_) +
                  (languageOverride_ ? " (override)" : ""));
}

void EditorFrame::ChooseLanguage(std::optional<SyntaxLanguage> language) {
    languageOverride_ = language;
    const auto effective = languageOverride_.value_or(LanguageForPath(document_.Path()));
    if (effective != language_) {
        language_ = effective;
        ApplySyntaxLanguage(*editor_, language_);
    }
    UpdateDocumentState();
}

SaveResult EditorFrame::SaveFirst(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss) {
    const wxString previousPath = document_.Path();
    const auto result = document_.SaveFirst(destination, decideLoss ? decideLoss : [this] { return ConfirmGbkLoss(); });
    RefreshLanguageAfterSave(previousPath, result);
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
    const wxString previousPath = document_.Path();
    const auto result = document_.SaveAs(destination, decideLoss ? decideLoss : [this] { return ConfirmGbkLoss(); });
    RefreshLanguageAfterSave(previousPath, result);
    UpdateDocumentState();
    return result;
}

void EditorFrame::RefreshLanguageAfterSave(const wxString& previousPath, const SaveResult& result) {
    if (languageOverride_ || result.status != SaveStatus::Saved || previousPath == document_.Path()) return;
    const auto language = LanguageForPath(document_.Path());
    if (language == language_) return;
    language_ = language;
    ApplySyntaxLanguage(*editor_, language_);
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
        languageOverride_.reset();
        GetMenuBar()->Check(automaticLanguageId_, true);
        language_ = LanguageForPath(document_.Path());
        ApplySyntaxLanguage(*editor_, language_);
        regexMatch_.reset();
        lastEmptyMatch_.reset();
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
    languageOverride_.reset();
    GetMenuBar()->Check(automaticLanguageId_, true);
    language_ = SyntaxLanguage::PlainText;
    ApplySyntaxLanguage(*editor_, language_);
    regexMatch_.reset();
    lastEmptyMatch_.reset();
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
