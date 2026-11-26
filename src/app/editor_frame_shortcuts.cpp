#include "app/editor_frame.h"
#include "app/command_ids.h"
#include "app/dialogs/insert_tag_dialog.h"
#include "editor/editor_control.h"
#include "editor/editor_ids.h"
#include "settings/settings_store.h"
#include <utility>
#include <wx/menu.h>
#include <wx/translation.h>
#include "app/search_shortcuts.h"
#include "editor/platform/custom_case.h"
#include "syntax/syntax_scheme.h"

void EditorFrame::OnSearchKey(wxKeyEvent& event) {
    if (!HandleCommandKey(event)) event.Skip();
}

bool EditorFrame::HandleCommandKey(const wxKeyEvent& event) {
    if (HandleSelectionActionKey(event)) return true;
    if (event.GetKeyCode() == WXK_F12 && !event.HasAnyModifiers()) {
        ShowSyntaxScheme();
        return true;
    }
#ifndef __WXMSW__
    // MSW's hook still reports physical Escape after TSF converts the retrieved
    // MSG to VK_PROCESSKEY. MSWTranslateMessage owns the actual Escape message.
    if (event.GetKeyCode() == WXK_ESCAPE && HandleEscapeKey(event.GetModifiers())) return true;
#endif
    if (HandleInsertionKey(event)) return true;
    if (event.GetKeyCode() == 'I' && event.ControlDown() && !event.AltDown()) {
        if (event.ShiftDown() && !event.MetaDown())
            editor_->SetCurrentLineHighlightMode(LineHighlightMode::BackgroundColor);
        else if (!event.ShiftDown() && !event.MetaDown() && !editor_->Send(Scintilla::Message::GetSelectionEmpty))
            editor_->SplitLines();
        return true;
    }
    if (HandleTabConversionKey(event) || HandleCaseConversionKey(event)) return true;
    if (event.GetKeyCode() == 'T' && event.AltDown()) {
        if (!event.ControlDown() && !event.ShiftDown() && !event.MetaDown()) editor_->StripTrailingBlanks();
        return true;
    }
    return HandleSearchKey(event) || HandleNavigationKey(event) || HandleViewKey(event) || HandleDocumentKey(event);
}

bool EditorFrame::HandleInsertionKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_F8 && event.ControlDown()) {
        if (!event.AltDown() && !event.ShiftDown() && !event.MetaDown()) InsertEncodingIdentifier();
        return true;
    }
    if (event.GetKeyCode() == WXK_F9 && event.ControlDown()) {
        if (!event.AltDown() && !event.MetaDown())
            InsertFileName(event.ShiftDown() ? FileNamePart::FullPath : FileNamePart::FileName);
        return true;
    }
    if (event.GetKeyCode() == WXK_F9 && event.AltDown() && event.ShiftDown() && !event.ControlDown()) {
        if (!event.MetaDown()) CopyFileName(FileNamePart::FullPath);
        return true;
    }
    if (event.GetKeyCode() == 'X' && event.AltDown() && !event.ControlDown() && !event.ShiftDown() &&
        !event.MetaDown()) {
        ShowInsertTagDialog(this, *editor_);
        return true;
    }
    if (event.GetKeyCode() == WXK_F5 && event.ControlDown()) {
        if (!event.AltDown() && !event.MetaDown()) InsertDateTime(!event.ShiftDown());
        return true;
    }
    return false;
}

bool EditorFrame::HandleCaseConversionKey(const wxKeyEvent& event) {
    const int key = event.GetKeyCode();
    if (event.AltDown() && event.ShiftDown() && (key == 'X' || key == 'H')) {
        if (!event.ControlDown() && !event.MetaDown())
            editor_->Send(Scintilla::Message::CustomCaseMapping,
                          key == 'X' ? CustomCaseCommand::EscapeHtml : CustomCaseCommand::UnescapeHtml);
        return true;
    }
    if (!event.ControlDown() || !event.AltDown()) return false;
    int mapping;
    switch (key) {
    case 'X':
        mapping = CustomCaseCommand::CharacterToHex;
        break;
    case 'C':
        mapping = CustomCaseCommand::HexToCharacter;
        break;
    case 'E':
        mapping = CustomCaseCommand::EscapeC;
        break;
    case 'R':
        mapping = CustomCaseCommand::UnescapeC;
        break;
    case 'U':
        mapping = CustomCaseCommand::Invert;
        break;
    case 'I':
        mapping = CustomCaseCommand::Title;
        break;
    case 'O':
        mapping = CustomCaseCommand::Sentence;
        break;
    default:
        return false;
    }
    if (!event.ShiftDown() && !event.MetaDown()) editor_->Send(Scintilla::Message::CustomCaseMapping, mapping);
    return true;
}

bool EditorFrame::HandleTabConversionKey(const wxKeyEvent& event) {
    if ((event.GetKeyCode() != 'T' && event.GetKeyCode() != 'S') || !event.ControlDown() ||
        (!event.ShiftDown() && !event.AltDown()))
        return false;
    if (event.ShiftDown() != event.AltDown() && !event.MetaDown())
        ConvertTabs(event.GetKeyCode() == 'T', event.AltDown());
    // Consume extra modifiers before native menu lookup can drop them.
    return true;
}

bool EditorFrame::HandleSearchKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() == 'F' && event.ControlDown() && !event.AltDown() && !event.ShiftDown())
        ShowFind();
    else if (event.GetKeyCode() == 'H' && event.ControlDown() && !event.AltDown() && !event.ShiftDown())
        ShowFind(true);
    else if (event.GetKeyCode() == WXK_F2 && event.ControlDown() && event.AltDown() && !event.MetaDown())
        Search(!event.ShiftDown(), true);
    else if (event.GetKeyCode() == WXK_F3) {
        switch (SearchShortcuts::ClassifyF3(event.GetModifiers())) {
        case F3SearchAction::Next:
            FindNext();
            break;
        case F3SearchAction::Previous:
            FindPrevious();
            break;
        case F3SearchAction::SaveFindText:
            search_.SaveSelectionAsFindText(true);
            break;
        case F3SearchAction::SelectionNext:
            FindSelection(SearchDirection::Forward);
            break;
        case F3SearchAction::SelectionPrevious:
            FindSelection(SearchDirection::Backward);
            break;
        case F3SearchAction::Ignore:
            break;
        }
    } else if (event.GetKeyCode() == WXK_F4 && !event.AltDown()) {
        if (!event.HasAnyModifiers()) ReplaceNext();
    } else
        return false;
    return true;
}

bool EditorFrame::HandleNavigationKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_SPACE && event.ControlDown() && (event.AltDown() || event.ShiftDown())) {
        if (!event.MetaDown() && event.AltDown() != event.ShiftDown()) {
            if (event.AltDown())
                editor_->SelectWord();
            else
                editor_->SelectLines();
        }
        // Consume extra modifiers before native accelerators can drop them.
        return true;
    }
    if (HandleBlockNavigationKey(event)) return true;
    if (event.GetKeyCode() == 'B' && event.ControlDown() && !event.AltDown() && !event.MetaDown())
        editor_->MatchBrace(event.ShiftDown());
    else if (event.GetKeyCode() == WXK_F2 && !event.MetaDown()) {
        if (event.ControlDown() && !event.ShiftDown() && !event.AltDown())
            editor_->ToggleBookmark();
        else if (event.AltDown() && !event.ControlDown() && !event.ShiftDown())
            editor_->Send(Scintilla::Message::MarkerDeleteAll, EditorMarker::Bookmark);
        else if (!event.ControlDown() && !event.AltDown())
            editor_->GoToBookmark(!event.ShiftDown());
        // Consume unmatched modifiers so native menu lookup cannot drop them.
    } else if (event.GetKeyCode() == WXK_F6 && event.AltDown() && !event.ControlDown() && !event.ShiftDown() &&
               !event.MetaDown())
        editor_->SelectAllBookmarks();
    else
        return false;
    return true;
}

bool EditorFrame::HandleBlockNavigationKey(const wxKeyEvent& event) {
    // The original OEM accelerators distinguish Shift and Ctrl exactly. Consume
    // other Alt punctuation variants before native lookup can drop modifiers.
    const int rawKey = event.GetKeyCode();
    const int key = rawKey == '{' ? '[' : rawKey == '}' ? ']' : rawKey == '<' ? ',' : rawKey == '>' ? '.' : rawKey;
    const bool punctuation = key == '[' || key == ']' || key == ',' || key == '.';
    if (!punctuation || event.MetaDown()) return false;
    if (event.AltDown()) {
        HandleAltBlockNavigationKey(event, key);
        return true;
    }
    if ((key == ',' || key == '.') && event.ControlDown() && event.ShiftDown()) {
        // Original CMD_JUMP2SELSTART / CMD_JUMP2SELEND: change the active
        // endpoint without changing the selected text or line-selection mode.
        editor_->FocusSelectionBoundary(key == ',');
        return true;
    }
    return false;
}

void EditorFrame::HandleAltBlockNavigationKey(const wxKeyEvent& event, int key) {
    using enum BlockNavigation;
    if (key == '[' && !event.ControlDown() && !event.ShiftDown())
        editor_->GoToBlock(Start);
    else if (key == ']' && !event.ControlDown()) {
        if (event.ShiftDown())
            editor_->SelectCurrentBlockLines();
        else
            editor_->GoToBlock(End);
    } else if ((key == ',' || key == '.') && !event.ShiftDown()) {
        if (key == ',')
            editor_->GoToBlock(event.ControlDown() ? PreviousSibling : Previous);
        else
            editor_->GoToBlock(event.ControlDown() ? NextSibling : Next);
    }
}

void EditorFrame::HandleFullScreenKey(int modifiers) {
    auto& preferences = settings_ ? settings_->values : defaults_;
    switch (modifiers) {
    case wxMOD_NONE:
        SetFullScreen(!FullScreenActive());
        break;
    case wxMOD_ALT:
        preferences.showMenu = !preferences.showMenu;
        ApplyBarVisibility();
        break;
    case wxMOD_CONTROL:
        preferences.showToolbar = !preferences.showToolbar;
        ApplyBarVisibility();
        break;
    case wxMOD_SHIFT:
        preferences.showStatusbar = !preferences.showStatusbar;
        ApplyBarVisibility();
        break;
    default:
        break;
    }
}

bool EditorFrame::HandleViewKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_F11) {
        HandleFullScreenKey(event.GetModifiers());
        return true;
    }
    if (event.GetKeyCode() == 'V' && event.ControlDown() && event.ShiftDown()) {
        if (!event.AltDown() && !event.MetaDown()) editor_->SetVisualBraceMatching(!editor_->VisualBraceMatching());
        return true;
    }
    if (event.GetKeyCode() == 'L' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
        !event.MetaDown()) {
        ToggleLongLineMarker();
        return true;
    }
    if (event.GetKeyCode() == 'G' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
        !event.MetaDown()) {
        ToggleIndentationGuides();
        return true;
    }
    if (event.GetKeyCode() == 'F' && event.ControlDown() && event.ShiftDown() && !event.AltDown() && !event.MetaDown())
        editor_->SetCurrentLineHighlightMode(LineHighlightMode::OutlineFrame);
    else if (event.GetKeyCode() == 'W' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
             !event.MetaDown())
        wordWrap_.Toggle();
    else if (event.GetKeyCode() == 'N' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
             !event.MetaDown())
        editor_->SetLineNumbersVisible(!editor_->LineNumbersVisible());
    else if (event.GetKeyCode() == 'M' && event.ControlDown() && event.ShiftDown() && !event.AltDown() &&
             !event.MetaDown())
        editor_->SetBookmarkMarginVisible(!editor_->BookmarkMarginVisible());
    else
        return false;
    return true;
}

bool EditorFrame::HandleDisplayKey(const wxKeyEvent& event, int whitespace, int eol, int symbols) {
    if (!event.ControlDown() || !event.ShiftDown() || event.AltDown() || event.MetaDown()) return false;
    const int key = event.GetKeyCode();
    const int commandId = key == '8' || key == '*'   ? whitespace
                          : key == '9' || key == '(' ? eol
                          : key == '0' || key == ')' ? symbols
                                                     : wxID_NONE;
    if (commandId == wxID_NONE) return false;
    wxCommandEvent command(wxEVT_MENU, commandId);
    GetEventHandler()->ProcessEvent(command);
    return true;
}

bool EditorFrame::HandleDocumentKey(const wxKeyEvent& event) {
    if (event.GetKeyCode() == 'H' && event.AltDown() && !event.ControlDown() && !event.ShiftDown() &&
        !event.MetaDown()) {
        ShowRecentFiles();
        return true;
    }
    if (event.GetKeyCode() == WXK_F5 && !event.HasAnyModifiers()) {
        wxCommandEvent command(wxEVT_MENU, revertId_);
        GetEventHandler()->ProcessEvent(command);
        return true;
    }
    if (event.GetKeyCode() == WXK_F10) {
        // Leave the native context-menu gesture to wx/GTK/DefWindowProc.
        if (event.GetModifiers() == wxMOD_SHIFT) return false;
        if (!event.HasAnyModifiers()) ToggleReadOnlyMode();
        return true;
    }
    if (event.GetKeyCode() != WXK_F6) return false;
    // GTK Wayland can consume Ctrl while matching F6 accelerators. Route
    // the original exact modifiers before native accelerator matching.
    if (!event.HasAnyModifiers()) {
        wxCommandEvent command(wxEVT_MENU, wxID_SAVEAS);
        GetEventHandler()->ProcessEvent(command);
    } else if (event.ControlDown() && !event.ShiftDown() && !event.AltDown() && !event.MetaDown()) {
        wxCommandEvent command(wxEVT_MENU, std::to_underlying(AppCommand::SaveCopy));
        GetEventHandler()->ProcessEvent(command);
    }
    return true;
}
