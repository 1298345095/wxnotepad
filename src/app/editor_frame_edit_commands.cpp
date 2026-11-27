#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "editor/editor_ids.h"
#include "settings/settings_store.h"
#include <utility>
#include <wx/menu.h>
#include <wx/translation.h>
#include "app/command_ids.h"
#include "app/search_shortcuts.h"
#include "app/dialogs/align_lines_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include "app/dialogs/column_wrap_dialog.h"
#include "app/dialogs/enclose_selection_dialog.h"
#include "app/dialogs/go_to_line_dialog.h"
#include "app/dialogs/insert_tag_dialog.h"
#include "app/dialogs/modify_lines_dialog.h"
#include "app/dialogs/sort_lines_dialog.h"
#include "editor/character_information.h"
#include "editor/platform/clipboard.h"
#include "editor/platform/custom_case.h"
#include "editor/platform/native_encoding.h"
#include "syntax/syntax_scheme.h"
#include <array>
#include <wx/accel.h>
#include <wx/msgdlg.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

wxMenu* EditorFrame::CreateEditMenu() {
    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO, _("&Undo\tCtrl+Z"));
    edit->Append(wxID_REDO, _("&Redo\tCtrl+Y"));
    edit->AppendSeparator();
    edit->Append(wxID_CUT, _("Cu&t\tCtrl+X"));
    edit->Append(wxID_COPY, _("&Copy\tCtrl+C"));
    edit->Append(wxID_PASTE, _("&Paste\tCtrl+V"));
    edit->Append(wxID_DELETE, _("&Delete"));
    edit->Append(wxID_SELECTALL, _("Select &All\tCtrl+A"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::SelectAll); }, wxID_SELECTALL);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); },
        wxID_SELECTALL);
    const int swap = wxWindow::NewControlId();
    edit->Append(swap, _("S&wap\tCtrl+K"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SwapClipboard(document_.IsDefaultEncoding()); }, swap);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty) ||
                         editor_->Send(Scintilla::Message::CanPaste));
        },
        swap);
    edit->AppendSeparator();
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Undo); }, wxID_UNDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Redo); }, wxID_REDO);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanUndo)); },
        wxID_UNDO);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanRedo)); },
        wxID_REDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Copy(); }, wxID_COPY);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Cut(); }, wxID_CUT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Paste); }, wxID_PASTE);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); }, wxID_COPY);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); }, wxID_CUT);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanPaste)); },
        wxID_PASTE);
    const int clearClipboard = wxWindow::NewControlId();
    edit->Append(clearClipboard, _("Clear Clip&board"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { EditorClipboard::Clear(*this); }, clearClipboard);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(EditorClipboard::HasData(*this)); },
        clearClipboard);
    auto* clipboard = new wxMenu;
    AppendFileNameCopyCommands(clipboard);
    clipboard->AppendSeparator();
    const int copyAll = wxWindow::NewControlId();
    clipboard->Append(copyAll, _("Copy &All\tAlt+A"));
    const int copyAdd = wxWindow::NewControlId();
    clipboard->Append(copyAdd, _("Copy A&dd\tCtrl+E"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            editor_->Send(Scintilla::Message::CopyRange, 0, editor_->Send(Scintilla::Message::GetLength));
        },
        copyAll);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); }, copyAll);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->CopyAdd())
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        copyAdd);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
        copyAdd);
    const int copyRtf = wxWindow::NewControlId();
    // TRANSLATORS: Copies styled text to the clipboard in Rich Text Format (IDM_EDIT_COPYRTF).
    clipboard->Append(copyRtf, _("Copy as &RTF"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->CopyAsRtf(document_.IsDefaultEncoding()); }, copyRtf);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
        copyRtf);
    AppendCopyWindowPosition(clipboard);
    edit->AppendSubMenu(clipboard, _("Cop&y to Clipboard"));
    edit->AppendSeparator();
    AppendEditingCommands(edit);
    return edit;
}

void EditorFrame::AppendEditingCommands(wxMenu* edit) {
    auto* selection = new wxMenu;
    AppendSelectionCommands(selection);
    edit->AppendSubMenu(selection, _("&Selection"));
    AppendEnclosureCommands(edit);
    auto* lines = new wxMenu;
    AppendLineCommands(lines);
    edit->AppendSubMenu(lines, _("&Lines"));
    AppendConversionCommands(edit);
    AppendDateTimeCommands(edit);
    auto* special = new wxMenu;
    AppendSpecialCommands(special);
    edit->AppendSubMenu(special, _("Spec&ial"));
}

void EditorFrame::AppendConversionCommands(wxMenu* edit) {
    auto* conversion = new wxMenu;
    for (const auto& [label, mapping] :
         {std::pair{wxTRANSLATE("&Upper Case\tCtrl+Shift+U"), CustomCaseCommand::Upper},
          std::pair{wxTRANSLATE("&Lower Case\tCtrl+U"), CustomCaseCommand::Lower},
          std::pair{wxTRANSLATE("In&vert Case\tCtrl+Alt+U"), CustomCaseCommand::Invert},
          std::pair{wxTRANSLATE("Title &Case\tCtrl+Alt+I"), CustomCaseCommand::Title},
          std::pair{wxTRANSLATE("&Sentence Case\tCtrl+Alt+O"), CustomCaseCommand::Sentence}}) {
        const int id = wxWindow::NewControlId();
        conversion->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, mapping](wxCommandEvent&) { editor_->Send(Scintilla::Message::CustomCaseMapping, mapping); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    conversion->AppendSeparator();
    struct Conversion {
        const char* label;
        bool toTabs;
        bool onlyIndent;
    };
    for (const auto& command : {Conversion{wxTRANSLATE("Tabify Selection\tCtrl+Shift+T"), true, false},
                                Conversion{wxTRANSLATE("Untabify Selection\tCtrl+Shift+S"), false, false},
                                Conversion{wxTRANSLATE("Tabify Indent\tCtrl+Alt+T"), true, true},
                                Conversion{wxTRANSLATE("Untabify Indent\tCtrl+Alt+S"), false, true}}) {
        const int id = wxWindow::NewControlId();
        conversion->Append(id, wxGetTranslation(command.label));
        Bind(wxEVT_MENU, [this, command](wxCommandEvent&) { ConvertTabs(command.toTabs, command.onlyIndent); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    conversion->AppendSeparator();
    for (const auto& [label, radix] :
         {std::pair{wxTRANSLATE("To &Hexadecimal\tCtrl+Alt+H"), 16u},
          std::pair{wxTRANSLATE("To &Decimal\tCtrl+Alt+D"), 10u}, std::pair{wxTRANSLATE("To &Binary\tCtrl+Alt+B"), 2u},
          std::pair{wxTRANSLATE("To &Octet"), 8u}}) {
        const int id = wxWindow::NewControlId();
        conversion->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, radix](wxCommandEvent&) {
                if (!editor_->ConvertNumberRadix(radix))
                    wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    conversion->AppendSeparator();
    // Delivered context conversions remain available in Convert.
    for (const auto& [label, mapping] :
         {std::pair{wxTRANSLATE("Escape HT&ML/XML Chars\tAlt+Shift+X"), CustomCaseCommand::EscapeHtml},
          std::pair{wxTRANSLATE("Unescape HTML/&XML Chars\tAlt+Shift+H"), CustomCaseCommand::UnescapeHtml}}) {
        const int id = wxWindow::NewControlId();
        conversion->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, mapping](wxCommandEvent&) { editor_->Send(Scintilla::Message::CustomCaseMapping, mapping); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    conversion->AppendSeparator();
    for (const auto& [label, command] : {std::pair{wxTRANSLATE("URL &Encode\tCtrl+Shift+E"), UrlConversion::Encode},
                                         std::pair{wxTRANSLATE("URL Component Encode"), UrlConversion::ComponentEncode},
                                         std::pair{wxTRANSLATE("URL &Decode\tCtrl+Shift+R"), UrlConversion::Decode}}) {
        const int id = wxWindow::NewControlId();
        conversion->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, command](wxCommandEvent&) {
                if (!editor_->ConvertUrl(command))
                    wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
    edit->AppendSubMenu(conversion, _("Con&vert"));
}

void EditorFrame::AppendEnclosureCommands(wxMenu* edit) {
    auto* enclosure = new wxMenu;
    const int customEnclosure = wxWindow::NewControlId();
    enclosure->Append(customEnclosure, _("&With...\tAlt+Q"));
    const int encloseTag = wxWindow::NewControlId();
    enclosure->Append(encloseTag, _("&HTML/XML Tag...\tAlt+X"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowInsertTagDialog(this, *editor_); }, encloseTag);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, encloseTag);
    enclosure->AppendSeparator();
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) { ShowEncloseSelectionDialog(this, *editor_, enclosureBefore_, enclosureAfter_); },
        customEnclosure);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, customEnclosure);
    const auto addEnclosure = [this, enclosure](const wxString& label, const wxString& delimiter) {
        const int id = wxWindow::NewControlId();
        enclosure->Append(id, label);
        Bind(
            wxEVT_MENU,
            [this, delimiter](wxCommandEvent&) {
                if (!editor_->EncloseSelection(delimiter, delimiter))
                    wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
            },
            id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    };
    addEnclosure(_("Triple &Single Quotes\tCtrl+3"), "'''");
    addEnclosure(_("Triple &Double Quotes\tCtrl+6"), R"(""")");
    addEnclosure(_("Triple &Backticks\tCtrl+9"), "```");
    edit->AppendSubMenu(enclosure, _("&Enclose Selection"));
}

void EditorFrame::AppendCommentCommands(wxMenu* edit) {
    const auto comments = [this] {
        return SyntaxCommentsFor(document_.Language(),
                                 static_cast<int>(editor_->Send(Scintilla::Message::GetStyleIndexAt,
                                                                editor_->Send(Scintilla::Message::GetCurrentPos))));
    };
    const int lineComment = wxWindow::NewControlId();
    auto* lineCommentItem = new wxMenuItem(edit, lineComment, _("T&oggle Line Comment\tCtrl+/"));
    lineCommentItem->AddExtraAccel(wxAcceleratorEntry(wxACCEL_CTRL, WXK_NUMPAD_DIVIDE, lineComment));
    lineCommentItem->AddExtraAccel(wxAcceleratorEntry(wxACCEL_CTRL, WXK_DIVIDE, lineComment));
    // wxMSW collects the item's extra accelerators when it is appended.
    edit->Append(lineCommentItem);
    Bind(
        wxEVT_MENU,
        [this, comments](wxCommandEvent&) {
            const auto* prefix = comments().line;
            if (!prefix) return;
            const auto& preferences = settings_ ? settings_->values : defaults_;
            if (!editor_->ToggleLineComments(prefix,
                                             (preferences.autoInsertMask & AutoInsertMask_SpaceAfterComment) != 0))
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        lineComment);
    Bind(
        wxEVT_UPDATE_UI, [comments](wxUpdateUIEvent& event) { event.Enable(comments().line != nullptr); }, lineComment);
    const int blockComment = wxWindow::NewControlId();
    edit->Append(blockComment, _("Toggle Block &Comment\tCtrl+Q"));
    Bind(
        wxEVT_MENU,
        [this, comments](wxCommandEvent&) {
            const auto& syntax = SyntaxFor(document_.Language());
            const auto rules = comments();
            if (rules.open && !editor_->ToggleBlockComments(rules.open, rules.close, syntax.commentStyleLast))
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        blockComment);
    Bind(
        wxEVT_UPDATE_UI, [comments](wxUpdateUIEvent& event) { event.Enable(comments().open != nullptr); },
        blockComment);
}

void EditorFrame::AppendSelectionCommands(wxMenu* edit) {
    const int duplicateSelection = wxWindow::NewControlId();
    edit->Append(duplicateSelection, _("&Duplicate\tAlt+D"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::SelectionDuplicate); },
        duplicateSelection);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, duplicateSelection);
    edit->AppendSeparator();
    AppendCommentCommands(edit);
    const int indent = wxWindow::NewControlId();
    edit->Append(indent, _("&Indent"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->IndentLines(true); }, indent);
    const int unindent = wxWindow::NewControlId();
    edit->Append(unindent, _("&Unindent"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->IndentLines(false); }, unindent);
    for (const int id : {indent, unindent})
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    edit->AppendSeparator();
    const int stripLeading = wxWindow::NewControlId();
    const int stripTrailing = wxWindow::NewControlId();
    edit->Append(stripTrailing, _("Strip &Trailing Blanks\tAlt+T"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->StripLeadingBlanks(); }, stripLeading);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->StripTrailingBlanks(); }, stripTrailing);
    for (const int id : {stripLeading, stripTrailing})
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    const int stripFirst = wxWindow::NewControlId();
    edit->Append(stripFirst, _("Strip &First Character\tAlt+Z"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->StripFirstCharacter())
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        stripFirst);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, stripFirst);
    const int stripLast = wxWindow::NewControlId();
    edit->Append(stripLast, _("Strip &Last Character\tAlt+L"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->StripLastCharacter())
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        stripLast);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, stripLast);
    Bind(wxEVT_CHAR_HOOK, [this, stripFirst, stripLast](wxKeyEvent& event) {
        const int key = event.GetKeyCode();
        if (!event.AltDown() || (key != 'Z' && key != 'L')) {
            event.Skip();
            return;
        }
        // Retain the exact modifiers for the original Alt+Z / Alt+L shortcuts.
        if (!event.ControlDown() && !event.ShiftDown() && !event.MetaDown()) {
            wxCommandEvent command(wxEVT_MENU, key == 'Z' ? stripFirst : stripLast);
            GetEventHandler()->ProcessEvent(command);
        }
    });
    edit->Append(stripLeading, _("Strip Leading Blan&ks"));
    edit->AppendSeparator();
    const int mergeBlankLines = wxWindow::NewControlId();
    const int removeBlankLines = wxWindow::NewControlId();
    edit->Append(mergeBlankLines, _("Merge Blank Lines\tAlt+B"));
    edit->Append(removeBlankLines, _("Remove Blank Lines\tAlt+R"));
    for (const int id : {mergeBlankLines, removeBlankLines}) {
        Bind(
            wxEVT_MENU,
            [this, id, mergeBlankLines](wxCommandEvent&) {
                if (!editor_->RemoveBlankLines(id == mergeBlankLines))
                    wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
            },
            id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    }
    const int mergeDuplicates = wxWindow::NewControlId();
    const int removeDuplicates = wxWindow::NewControlId();
    edit->Append(mergeDuplicates, _("Merge Duplicate Lines"));
    edit->Append(removeDuplicates, _("Remove Duplicate Lines"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MergeDuplicateLines(); }, mergeDuplicates);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->RemoveDuplicateLines(); }, removeDuplicates);
    for (const int id : {mergeDuplicates, removeDuplicates})
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) {
                using enum Scintilla::Message;
                event.Enable(editor_->Send(LineFromPosition, editor_->Send(GetSelectionEnd)) >
                             editor_->Send(LineFromPosition, editor_->Send(GetSelectionStart)));
            },
            id);
    const int padSpaces = wxWindow::NewControlId();
    edit->Append(padSpaces, _("&Pad With Spaces\tAlt+P"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->PadWithSpaces(); }, padSpaces);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, padSpaces);
    const int compressWhitespace = wxWindow::NewControlId();
    edit->Append(compressWhitespace, _("Compress &Whitespace\tAlt+W"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->CompressWhitespace())
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        compressWhitespace);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, compressWhitespace);
}

void EditorFrame::AppendLineCommands(wxMenu* edit) {
    const int moveUp = wxWindow::NewControlId();
    const int moveDown = wxWindow::NewControlId();
    edit->Append(moveUp, _("Move &Up\tAlt+Up"));
    edit->Append(moveDown, _("Move &Down\tAlt+Down"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MoveLines(true); }, moveUp);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MoveLines(false); }, moveDown);
    for (const int id : {moveUp, moveDown})
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    const int duplicateLine = wxWindow::NewControlId();
    const int transposeLines = wxWindow::NewControlId();
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::LineDuplicate); }, duplicateLine);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::LineTranspose); }, transposeLines);
    for (const int id : {duplicateLine, transposeLines})
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    edit->Append(transposeLines, _("T&ranspose\tAlt+S"));
    edit->AppendSeparator();
    const int sortLines = wxWindow::NewControlId();
    edit->Append(sortLines, _("S&ort Lines...\tAlt+O"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (ShowSortLinesDialog(this, sortOptions_, editor_->Send(Scintilla::Message::SelectionIsRectangle)))
                editor_->SortLines(sortOptions_);
            editor_->SetFocus();
        },
        sortLines);
    for (const int id : {sortLines})
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) {
                using enum Scintilla::Message;
                event.Enable(editor_->Send(LineFromPosition, editor_->Send(GetSelectionEnd)) >
                             editor_->Send(LineFromPosition, editor_->Send(GetSelectionStart)));
            },
            id);
    const int modifyLines = wxWindow::NewControlId();
    edit->Append(modifyLines, _("&Modify Lines...\tAlt+M"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) { ShowModifyLinesDialog(this, *editor_, linePrefix_, lineSuffix_, skipEmptyLines_); },
        modifyLines);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, modifyLines);
    const int alignLines = wxWindow::NewControlId();
    edit->Append(alignLines, _("Alig&n Lines...\tAlt+J"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (ShowAlignLinesDialog(this, alignmentMode_)) {
                const auto result = editor_->AlignLines(alignmentMode_);
                if (result == AlignmentResult::RectangularSelection)
                    wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                                 wxOK | wxICON_WARNING, this);
                else if (result == AlignmentResult::LinesTooLong)
                    wxMessageBox(_("This operation can't be performed (lines too long)."), "wxnotepad",
                                 wxOK | wxICON_INFORMATION, this);
            }
            editor_->SetFocus();
        },
        alignLines);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, alignLines);
    edit->AppendSeparator();
    edit->Append(duplicateLine, _("&Duplicate Line\tCtrl+D"));
    const int cutLine = wxWindow::NewControlId();
    const int copyLine = wxWindow::NewControlId();
    const int deleteLine = wxWindow::NewControlId();
    edit->Append(cutLine, _("Cu&t Line\tCtrl+Shift+X"));
    edit->Append(copyLine, _("&Copy Line\tCtrl+Shift+C"));
    edit->Append(deleteLine, _("D&elete Line\tCtrl+Shift+D"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->CutLine(); }, cutLine);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->CopyLine(); }, copyLine);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->DeleteLine(); }, deleteLine);
    for (const int id : {cutLine, copyLine, deleteLine})
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    edit->AppendSeparator();
    AppendLineWrappingCommands(edit);
    Bind(wxEVT_CHAR_HOOK, [this, modifyLines, transposeLines](wxKeyEvent& event) {
        const int key = event.GetKeyCode();
        if (!event.AltDown() || (key != 'M' && key != 'S')) {
            event.Skip();
            return;
        }
        // Original command accelerators take priority over Scheme/Search mnemonics.
        if (!event.ControlDown() && !event.ShiftDown() && !event.MetaDown()) {
            wxCommandEvent command(wxEVT_MENU, key == 'M' ? modifyLines : transposeLines);
            GetEventHandler()->ProcessEvent(command);
        }
    });
}

void EditorFrame::AppendLineWrappingCommands(wxMenu* edit) {
    const int joinLines = wxWindow::NewControlId();
    const int splitLines = wxWindow::NewControlId();
    const int columnWrap = wxWindow::NewControlId();
    edit->Append(joinLines, _("&Join Lines\tCtrl+J"));
    edit->Append(splitLines, _("&Split Lines\tCtrl+I"));
    edit->Append(columnWrap, _("Column &Wrap..."));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const int boundary = static_cast<int>(editor_->Send(Scintilla::Message::GetEdgeColumn));
            if (ShowColumnWrapDialog(this, wrapColumn_, boundary) && !editor_->WrapToColumn(wrapColumn_))
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
            editor_->SetFocus();
        },
        columnWrap);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SplitLines(); }, splitLines);
    const int joinParagraphs = wxWindow::NewControlId();
    edit->Append(joinParagraphs, _("Join &Paragraphs\tCtrl+Shift+J"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->JoinLines(); }, joinLines);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!editor_->JoinParagraphs())
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        joinParagraphs);
    for (const int id : {splitLines, columnWrap, joinLines, joinParagraphs})
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
}

void EditorFrame::AppendSearchCommands(wxMenu* edit) {
    edit->Append(wxID_FIND, _("&Find...\tCtrl+F"));
    const int saveFind = wxWindow::NewControlId();
    const int replaceNext = wxWindow::NewControlId();
    // TRANSLATORS: Store the selection as the next search term (IDM_EDIT_SAVEFIND).
    edit->Append(saveFind, _("Sa&ve Find Text\tAlt+F3"));
    edit->Append(wxID_FORWARD, _("Find &Next\tF3"));
    edit->Append(wxID_BACKWARD, _("Find &Previous\tShift+F3"));
    edit->Append(wxID_REPLACE, _("R&eplace...\tCtrl+H"));
    // TRANSLATORS: Replace the next match with the initialized replacement (IDM_EDIT_REPLACENEXT).
    edit->Append(replaceNext, _("Repl&ace Next\tF4"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFind(); }, wxID_FIND);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { search_.SaveSelectionAsFindText(true); }, saveFind);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowFind(true); }, wxID_REPLACE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNext(); }, wxID_FORWARD);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindPrevious(); }, wxID_BACKWARD);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ReplaceNext(); }, replaceNext);

    const int nextSelection = wxWindow::NewControlId();
    const int previousSelection = wxWindow::NewControlId();
    const std::array selectionKeys = {
        SearchShortcuts::F3Accelerator(F3SearchAction::SelectionNext, nextSelection),
        SearchShortcuts::F3Accelerator(F3SearchAction::SelectionPrevious, previousSelection)};
    SetAcceleratorTable(wxAcceleratorTable(static_cast<int>(selectionKeys.size()), selectionKeys.data()));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindSelection(SearchDirection::Forward); }, nextSelection);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindSelection(SearchDirection::Backward); }, previousSelection);
}

void EditorFrame::AppendNavigationCommands(wxMenu* edit) {
    edit->AppendSeparator();
    const int matchingBrace = wxWindow::NewControlId();
    edit->Append(matchingBrace, _("Find Matching &Brace\tCtrl+B"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MatchBrace(); }, matchingBrace);
    const int selectMatchingBrace = wxWindow::NewControlId();
    edit->Append(selectMatchingBrace, _("Select to Matching B&race\tCtrl+Shift+B"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->MatchBrace(true); }, selectMatchingBrace);
    const int selectWord = wxWindow::NewControlId();
    edit->Append(selectWord, _("Select &Word\tCtrl+Alt+Space"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SelectWord(); }, selectWord);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); },
        selectWord);
    const int selectLines = wxWindow::NewControlId();
    edit->Append(selectLines, _("Select &Lines (Expand Selection)\tCtrl+Shift+Space"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SelectLines(); }, selectLines);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); },
        selectLines);
    const int selectBlock = wxWindow::NewControlId();
    edit->Append(selectBlock, _("Select Lines in &Current Block\tAlt+Shift+]"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SelectCurrentBlockLines(); }, selectBlock);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); },
        selectBlock);
    edit->AppendSeparator();
    const int selectDocumentStart = wxWindow::NewControlId();
    const int selectDocumentEnd = wxWindow::NewControlId();
    edit->Append(selectDocumentStart, _("Select to Document Star&t"));
    edit->Append(selectDocumentEnd, _("Select to Document En&d"));
    // Preserve the original boundary setters, including reverse and rectangular selections.
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::SetSelectionStart, 0); },
        selectDocumentStart);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            editor_->Send(Scintilla::Message::SetSelectionEnd, editor_->Send(Scintilla::Message::GetLength));
        },
        selectDocumentEnd);
    for (const int id : {selectDocumentStart, selectDocumentEnd})
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); }, id);
    const int selectNext = wxWindow::NewControlId();
    const int selectPrevious = wxWindow::NewControlId();
    edit->Append(selectNext, _("Select to Ne&xt\tCtrl+Alt+F2"));
    edit->Append(selectPrevious, _("Select to Previou&s\tCtrl+Alt+Shift+F2"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Search(true, true); }, selectNext);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Search(false, true); }, selectPrevious);
    edit->AppendSeparator();
    auto* bookmarks = new wxMenu;
    const int bookmarkToggle = wxWindow::NewControlId();
    const int bookmarkNext = wxWindow::NewControlId();
    const int bookmarkPrevious = wxWindow::NewControlId();
    const int bookmarkSelect = wxWindow::NewControlId();
    const int bookmarkClear = wxWindow::NewControlId();
    bookmarks->Append(bookmarkToggle, _("&Toggle\tCtrl+F2"));
    bookmarks->AppendSeparator();
    bookmarks->Append(bookmarkNext, _("Goto &Next\tF2"));
    bookmarks->Append(bookmarkPrevious, _("Goto &Previous\tShift+F2"));
    bookmarks->AppendSeparator();
    bookmarks->Append(bookmarkSelect, _("&Select All\tAlt+F6"));
    bookmarks->Append(bookmarkClear, _("&Clear All\tAlt+F2"));
    edit->AppendSubMenu(bookmarks, _("Boo&kmarks"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->ToggleBookmark(); }, bookmarkToggle);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->GoToBookmark(true); }, bookmarkNext);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->GoToBookmark(false); }, bookmarkPrevious);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SelectAllBookmarks(); }, bookmarkSelect);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::MarkerDeleteAll, EditorMarker::Bookmark); },
        bookmarkClear);
    auto* goTo = new wxMenu;
    const int goToLine = std::to_underlying(AppCommand::GoToLine);
    goTo->Append(goToLine, _("&Goto Line...\tCtrl+G"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowGoToLineDialog(this, *editor_); }, goToLine);
    goTo->AppendSeparator();
    const auto addBlock = [this, goTo](const wxString& label, BlockNavigation command) {
        const int id = wxWindow::NewControlId();
        goTo->Append(id, label);
        Bind(wxEVT_MENU, [this, command](wxCommandEvent&) { editor_->GoToBlock(command); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); }, id);
    };
    addBlock(_("Goto Blo&ck Start\tAlt+["), BlockNavigation::Start);
    addBlock(_("Goto Bloc&k End\tAlt+]"), BlockNavigation::End);
    addBlock(_("Goto &Previous Block\tAlt+,"), BlockNavigation::Previous);
    addBlock(_("Goto &Next Block\tAlt+."), BlockNavigation::Next);
    addBlock(_("Goto P&revious Sibling Block\tCtrl+Alt+,"), BlockNavigation::PreviousSibling);
    addBlock(_("Goto N&ext Sibling Block\tCtrl+Alt+."), BlockNavigation::NextSibling);
    goTo->AppendSeparator();
    for (const auto& [label, start] : {std::pair{wxTRANSLATE("Goto &Selection Start\tCtrl+Shift+,"), true},
                                       std::pair{wxTRANSLATE("Goto Selec&tion End\tCtrl+Shift+."), false}}) {
        const int id = wxWindow::NewControlId();
        goTo->Append(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, start](wxCommandEvent&) { editor_->FocusSelectionBoundary(start); }, id);
    }
    edit->AppendSubMenu(goTo, _("&Goto"));
}

void EditorFrame::AppendLineSelectionCommands(wxMenu* edit) {
    using enum LineSelectionMode;
    auto* lineSelection = new wxMenu;
    for (const auto& [label, mode] :
         {std::pair{wxTRANSLATE("&None"), None}, std::pair{wxTRANSLATE("&Visual Studio"), VisualStudio},
          std::pair{wxTRANSLATE("&Old Visual Studio"), OldVisualStudio}, std::pair{wxTRANSLATE("&Normal"), Normal}}) {
        const int id = wxWindow::NewControlId();
        lineSelection->AppendRadioItem(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, mode](wxCommandEvent&) { editor_->SetLineSelectionMode(mode); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, mode](wxUpdateUIEvent& event) { event.Check(editor_->GetLineSelectionMode() == mode); }, id);
    }
    edit->AppendSubMenu(lineSelection, _("Line Selection &Mode"));
}

void EditorFrame::InsertDateTime(bool shortDate) {
    const auto& preferences = settings_ ? settings_->values : defaults_;
    editor_->InsertDateTime(shortDate ? DateTimeInsertion::Short : DateTimeInsertion::Long,
                            shortDate ? preferences.dateTimeShort : preferences.dateTimeLong);
}

void EditorFrame::InsertFileName(FileNamePart part) {
    auto text = document_.FileNameText(part);
    if (document_.IsDefaultEncoding()) text = NativeDefaultEncodingText(text);
    const auto bytes = text.ToStdString(wxConvUTF8);
    editor_->Send(Scintilla::Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(bytes.c_str()));
}

void EditorFrame::CopyFileName(FileNamePart part) {
    const auto text = document_.FileNameText(part);
    const auto bytes = text.ToStdString(wxConvUTF8);
    editor_->Send(Scintilla::Message::CopyText, bytes.size(), reinterpret_cast<intptr_t>(bytes.c_str()));
}

void EditorFrame::AppendFileNameCopyCommands(wxMenu* copy) {
    using enum FileNamePart;
    for (const auto& [label, part] : {std::pair{wxTRANSLATE("&File Name"), BaseName},
                                      std::pair{wxTRANSLATE("File Name and &Ext.\tShift+F9"), FileName},
                                      std::pair{wxTRANSLATE("Full &Path Name\tAlt+Shift+F9"), FullPath}}) {
        const int id = wxWindow::NewControlId();
        copy->Append(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, part](wxCommandEvent&) { CopyFileName(part); }, id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    }
}

void EditorFrame::AppendDateTimeCommands(wxMenu* edit) {
    auto* insert = new wxMenu;
    const int completeWord = wxWindow::NewControlId();
    auto* completionItem = new wxMenuItem(insert, completeWord, _("Complete &Word\tAlt+/"));
    completionItem->AddExtraAccel(wxAcceleratorEntry(wxACCEL_ALT, WXK_NUMPAD_DIVIDE, completeWord));
    completionItem->AddExtraAccel(wxAcceleratorEntry(wxACCEL_ALT, WXK_DIVIDE, completeWord));
    insert->Append(completionItem);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto& preferences = settings_ ? settings_->values : defaults_;
            CompleteWord(*editor_, SyntaxFor(document_.EffectiveLanguage()), preferences.completion);
        },
        completeWord);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::GetLength) != 0); },
        completeWord);
    insert->AppendSeparator();
    const int insertTag = wxWindow::NewControlId();
    insert->Append(insertTag, _("&HTML/XML Tag...\tAlt+X"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowInsertTagDialog(this, *editor_); }, insertTag);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, insertTag);
    const int guid = wxWindow::NewControlId();
    insert->Append(guid, _("New &GUID"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->InsertGuid(); }, guid);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, guid);
    auto* controls = new wxMenu;
    for (size_t index = 0; index < UnicodeControlInsertions.size(); ++index) {
        const int id = wxWindow::NewControlId();
        controls->Append(id, wxGetTranslation(wxString::FromUTF8(UnicodeControlInsertions[index].label)));
        Bind(
            wxEVT_MENU,
            [this, index](wxCommandEvent&) {
                editor_->InsertUnicodeControlCharacter(static_cast<UnicodeControlCharacter>(index),
                                                       document_.IsDefaultEncoding());
            },
            id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    }
    insert->AppendSubMenu(controls, _("Unicode &Control Character"));
    insert->AppendSeparator();
    for (const auto& [label, part] :
         {std::pair{wxTRANSLATE("&File Name"), FileNamePart::BaseName},
          std::pair{wxTRANSLATE("File Name and E&xtension\tCtrl+F9"), FileNamePart::FileName},
          std::pair{wxTRANSLATE("Full &Path Name\tCtrl+Shift+F9"), FileNamePart::FullPath}}) {
        const int id = wxWindow::NewControlId();
        insert->Append(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, part](wxCommandEvent&) { InsertFileName(part); }, id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    }
    insert->AppendSeparator();
    auto* timestamps = new wxMenu;
    const auto add = [this](wxMenu* menu, const char* label, DateTimeInsertion command) {
        const int id = wxWindow::NewControlId();
        menu->Append(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, command](wxCommandEvent&) { editor_->InsertDateTime(command); }, id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    };
    add(insert, wxTRANSLATE("Current &Date"), DateTimeInsertion::LocalDate);
    add(insert, wxTRANSLATE("Current Date &Time"), DateTimeInsertion::LocalDateTime);
    add(insert, wxTRANSLATE("&UTC Date Time"), DateTimeInsertion::UtcDateTime);
    add(insert, wxTRANSLATE("Unix Timesta&mp"), DateTimeInsertion::UnixSeconds);
    add(timestamps, wxTRANSLATE("&Millisecond (ms)"), DateTimeInsertion::UnixMilliseconds);
    add(timestamps, wxTRANSLATE("Micro&second (us)"), DateTimeInsertion::UnixMicroseconds);
    add(timestamps, wxTRANSLATE("&Nanosecond (ns)"), DateTimeInsertion::UnixNanoseconds);
    insert->AppendSubMenu(timestamps, _("&Other Timestamps"));
    for (const auto& [label, shortDate] : {std::pair{wxTRANSLATE("Time/Date (&Short Form)\tCtrl+F5"), true},
                                           std::pair{wxTRANSLATE("Time/Date (&Long Form)\tCtrl+Shift+F5"), false}}) {
        const int id = wxWindow::NewControlId();
        insert->Append(id, wxGetTranslation(label));
        Bind(wxEVT_MENU, [this, shortDate](wxCommandEvent&) { InsertDateTime(shortDate); }, id);
        Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, id);
    }
    insert->AppendSeparator();
    const int encoding = wxWindow::NewControlId();
    insert->Append(encoding, _("&Encoding Identifier\tCtrl+F8"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { InsertEncodingIdentifier(); }, encoding);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, encoding);
    const int shebang = wxWindow::NewControlId();
    insert->Append(shebang, _("Script She&bang Line"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->InsertScriptShebangLine(SyntaxFor(document_.Language())); },
        shebang);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(true); }, shebang);
    edit->AppendSubMenu(insert, _("I&nsert"));
}

void EditorFrame::InsertEncodingIdentifier() {
    editor_->InsertEncodingIdentifier(OriginalEncodingIdentifier(document_.CurrentEncoding()),
                                      SyntaxFor(document_.Language()));
}

void EditorFrame::ConvertTabs(bool toTabs, bool onlyIndent) {
    if (!(toTabs ? editor_->SpacesToTabs(onlyIndent) : editor_->TabsToSpaces(onlyIndent)))
        wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                     wxOK | wxICON_WARNING, this);
}

void EditorFrame::AppendSpecialCommands(wxMenu* edit) {
    const int showHex = wxWindow::NewControlId();
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (!ShowHexCode(*editor_, document_.IsDefaultEncoding()))
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, this);
        },
        showHex);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
        showHex);
    const int showCharacter = wxWindow::NewControlId();
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            const auto technical = CharacterInformation(*editor_, document_.IsDefaultEncoding());
            const auto payload = wxString::FromUTF8(technical);
            editor_->Send(Scintilla::Message::CopyText, technical.size(), reinterpret_cast<intptr_t>(technical.data()));
            wxDialog dialog(this, wxID_ANY, _("Character Info"));
            auto* contents = new wxBoxSizer(wxVERTICAL);
            auto* information = new wxStaticText(&dialog, wxID_ANY, wxEmptyString);
            information->SetLabelText(payload);
            information->SetName("Character information");
            contents->Add(information, 0, wxALL, dialog.FromDIP(12));
            contents->Add(dialog.CreateSeparatedButtonSizer(wxOK), 0, wxEXPAND | wxALL, dialog.FromDIP(8));
            DialogLayout::Fit(dialog, contents);
            dialog.CentreOnParent();
            dialog.ShowModal();
        },
        showCharacter);
    for (const auto& [label, mapping] :
         {std::pair{wxTRANSLATE("C&haracter to Hex\tCtrl+Alt+X"), CustomCaseCommand::CharacterToHex},
          std::pair{wxTRANSLATE("Hex to Cha&racter\tCtrl+Alt+C"), CustomCaseCommand::HexToCharacter},
          std::pair{wxTRANSLATE("Esca&pe C Chars\tCtrl+Alt+E"), CustomCaseCommand::EscapeC},
          std::pair{wxTRANSLATE("&Unescape C Chars\tCtrl+Alt+R"), CustomCaseCommand::UnescapeC}}) {
        if (mapping == CustomCaseCommand::EscapeC) {
            edit->Append(showHex, _("&Show Hex Code"));
            edit->Append(showCharacter, _("Show Character &Info"));
            edit->AppendSeparator();
        }
        const int id = wxWindow::NewControlId();
        edit->Append(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, mapping](wxCommandEvent&) { editor_->Send(Scintilla::Message::CustomCaseMapping, mapping); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this](wxUpdateUIEvent& event) { event.Enable(!editor_->Send(Scintilla::Message::GetSelectionEmpty)); },
            id);
    }
}

wxMenu* EditorFrame::CreateSearchMenu() {
    auto* search = new wxMenu;
    AppendSearchCommands(search);
    AppendNavigationCommands(search);
    return search;
}
