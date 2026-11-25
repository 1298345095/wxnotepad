#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include <utility>
#include <wx/menu.h>
#include <wx/translation.h>
#include "app/command_ids.h"
#include "app/dialogs/word_wrap_settings_dialog.h"
#include "app/dialogs/zoom_level_dialog.h"
#include "editor/display_units.h"
#include "syntax/syntax_scheme.h"
#include <array>

wxMenu* EditorFrame::CreateViewMenu() {
    auto* view = new wxMenu;
    const int codeFont = wxWindow::NewControlId();
    const int textFont = wxWindow::NewControlId();
    view->Append(codeFont, _("Default &Code Font...	Alt+F12"));
    view->Append(textFont, _("Default &Text Font..."));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ChooseDefaultFont(true); }, codeFont);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ChooseDefaultFont(false); }, textFont);
    AppendCaretCommands(view);
    view->AppendSeparator();
    AppendDisplayCommands(view);
    view->AppendSeparator();
    const int matchBraces = wxWindow::NewControlId();
    view->AppendCheckItem(matchBraces, _("Visual &Brace Matching\tCtrl+Shift+V"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetVisualBraceMatching(!editor_->VisualBraceMatching()); },
        matchBraces);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->VisualBraceMatching()); }, matchBraces);
    AppendCurrentLineCommands(view);
    auto* occurrences = new wxMenu;
    for (const auto& [label, bit] : {std::pair{wxTRANSLATE("&Off"), MarkOccurrenceOption_Enabled},
                                     std::pair{wxTRANSLATE("Match &Case"), MarkOccurrenceOption_MatchCase},
                                     std::pair{wxTRANSLATE("Match &Whole Word Only"), MarkOccurrenceOption_WholeWord},
                                     std::pair{wxTRANSLATE("&Bookmark Matched Line"), MarkOccurrenceOption_Bookmark}}) {
        const int id = wxWindow::NewControlId();
        occurrences->AppendCheckItem(id, wxGetTranslation(label));
        Bind(
            wxEVT_MENU,
            [this, bit](wxCommandEvent&) { editor_->Occurrences().SetOptions(editor_->Occurrences().Options() ^ bit); },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, bit](wxUpdateUIEvent& event) {
                const int options = editor_->Occurrences().Options();
                event.Check(bit == MarkOccurrenceOption_Enabled ? !(options & MarkOccurrenceOption_Enabled)
                                                                : (options & bit) != 0);
                event.Enable(bit == MarkOccurrenceOption_Enabled || (options & MarkOccurrenceOption_Enabled));
            },
            id);
    }
    view->AppendSubMenu(occurrences, _("Mark &Occurrences"));
    AppendCallTipCommands(view);
    view->AppendSeparator();
    AppendMarginCommands(view);
    view->AppendSeparator();
    AppendFoldCommands(view);
    view->AppendSeparator();
    AppendZoomCommands(view);
    const int fullScreen = wxWindow::NewControlId();
#ifdef __WXMSW__
    // TRANSLATORS: Toggle full screen; Esc exits it (IDM_VIEW_TOGGLE_FULLSCREEN).
    view->Append(fullScreen, _("Toggle Full &Screen\tF11 [Esc]"));
#else
    // TRANSLATORS: Linux menu displays Esc in the caption because only F11 is an accelerator.
    view->Append(fullScreen, _("Toggle Full &Screen [Esc]\tF11"));
#endif
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SetFullScreen(!FullScreenActive()); }, fullScreen);
    return view;
}

void EditorFrame::AppendCallTipCommands(wxMenu* view) {
    auto* modes = new wxMenu;
    // TRANSLATORS: Disable hexadecimal color previews (IDM_VIEW_SHOWCALLTIP_OFF).
    const std::array<wxString, 5> labels{_("&Off"), "RGBA", "ARGB", "BGRA", "ABGR"};
    for (size_t index = 0; index < labels.size(); ++index) {
        const auto mode = static_cast<ColorCallTipMode>(index);
        const int id = wxWindow::NewControlId();
        auto* item = modes->AppendRadioItem(id, labels[index]);
        if ((settings_ ? settings_->values : defaults_).showCallTip == mode) item->Check(true);
        Bind(
            wxEVT_MENU,
            [this, modes, id, mode](wxCommandEvent&) {
                (settings_ ? settings_->values : defaults_).showCallTip = mode;
                editor_->SetColorCallTipMode(mode);
                modes->Check(id, true);
            },
            id);
    }
    // TRANSLATORS: View submenu for hexadecimal color previews (IDM_VIEW_SHOWCALLTIP_*).
    view->AppendSubMenu(modes, _("Show CallTip"));
}

void EditorFrame::AppendZoomCommands(wxMenu* view) {
    auto* zoom = new wxMenu;
    const auto addZoom = [this, zoom](int id, const wxString& label, Scintilla::Message message, uintptr_t value = 0) {
        zoom->Append(id, label);
        Bind(wxEVT_MENU, [this, message, value](wxCommandEvent&) { editor_->Send(message, value); }, id);
    };
    addZoom(std::to_underlying(AppCommand::ZoomIn), _("Zoom &In\tCtrl++"), Scintilla::Message::ZoomIn);
    addZoom(std::to_underlying(AppCommand::ZoomOut), _("Zoom &Out\tCtrl+-"), Scintilla::Message::ZoomOut);
    const int zoomLevel = std::to_underlying(AppCommand::ZoomLevel);
    zoom->Append(zoomLevel, _("Zoom &Level..."));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowZoomLevelDialog(this, *editor_); }, zoomLevel);
    addZoom(wxWindow::NewControlId(), _("&Reset Zoom\tCtrl+\\"), Scintilla::Message::SetZoom,
            DisplayUnits::ZoomPercentScale);
    view->AppendSubMenu(zoom, _("&Zoom"));
}

void EditorFrame::AppendDisplayCommands(wxMenu* view) {
    const int wordWrap = std::to_underlying(AppCommand::WordWrap);
    view->AppendCheckItem(wordWrap, _("&Word Wrap\tCtrl+Shift+W"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { wordWrap_.Toggle(); }, wordWrap);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetWrapMode) != 0); }, wordWrap);
    const int marker = wxWindow::NewControlId();
    view->AppendCheckItem(marker, _("&Long Line Marker\tCtrl+Shift+L"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ToggleLongLineMarker(); }, marker);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetEdgeMode) != 0); }, marker);
    const int guides = wxWindow::NewControlId();
    view->AppendCheckItem(guides, _("Indentation &Guides\tCtrl+Shift+G"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ToggleIndentationGuides(); }, guides);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetIndentationGuides) != 0); },
        guides);
    view->AppendSeparator();
    const int whitespace = wxWindow::NewControlId();
    view->AppendCheckItem(whitespace, _("Show &Whitespace\tCtrl+Shift+8"));
    Bind(
        wxEVT_MENU,
        [this, whitespace](wxCommandEvent&) {
            const bool visible = editor_->Send(Scintilla::Message::GetViewWS) == 0;
            editor_->Send(Scintilla::Message::SetViewWS,
                          static_cast<uintptr_t>(std::to_underlying(visible ? Scintilla::WhiteSpace::VisibleAlways
                                                                            : Scintilla::WhiteSpace::Invisible)));
            GetMenuBar()->Check(whitespace, visible);
        },
        whitespace);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetViewWS) != 0); }, whitespace);
    const int eol = wxWindow::NewControlId();
    view->AppendCheckItem(eol, _("Show Line &Endings\tCtrl+Shift+9"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            editor_->Send(Scintilla::Message::SetViewEOL, !editor_->Send(Scintilla::Message::GetViewEOL));
        },
        eol);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->Send(Scintilla::Message::GetViewEOL) != 0); }, eol);
    const int symbols = wxWindow::NewControlId();
    view->AppendCheckItem(symbols, _("Show Wrap S&ymbols\tCtrl+Shift+0"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { wordWrap_.ToggleSymbols(); }, symbols);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(wordWrap_.SymbolsVisible()); }, symbols);
    const int controlCharacters = wxWindow::NewControlId();
    view->AppendCheckItem(controlCharacters, _("Un&icode Control Character"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (document_.IsDefaultEncoding()) return;
            auto& preferences = settings_ ? settings_->values : defaults_;
            preferences.showUnicodeControlCharacter = !preferences.showUnicodeControlCharacter;
            ApplyUnicodeControlCharacter();
        },
        controlCharacters);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Enable(!document_.IsDefaultEncoding());
            event.Check((settings_ ? settings_->values : defaults_).showUnicodeControlCharacter);
        },
        controlCharacters);
    // One frame hook owns dispatch; the dialog retains its narrower callback.
    Bind(wxEVT_CHAR_HOOK, [this, whitespace, eol, symbols](wxKeyEvent& event) {
        if (!HandleCommandKey(event) && !HandleDisplayKey(event, whitespace, eol, symbols)) event.Skip();
    });
}

void EditorFrame::ApplyUnicodeControlCharacter() {
    // EditShowUnicodeControlCharacter/EditOnCodePageChanged at Notepad4 89b8b36;
    // see licenses/Notepad4.txt. The host stores all text as UTF-8, so eligibility
    // follows the document's logical CPI_DEFAULT identity.
    const bool visible =
        (settings_ ? settings_->values : defaults_).showUnicodeControlCharacter && !document_.IsDefaultEncoding();
    if (visible == unicodeControlCharactersVisible_) return;
    unicodeControlCharactersVisible_ = visible;
    for (const auto& character : UnicodeControlInsertions) {
        if (!character.representation[0]) continue;
        editor_->Send(visible ? Scintilla::Message::SetRepresentation : Scintilla::Message::ClearRepresentation,
                      reinterpret_cast<uintptr_t>(character.utf8),
                      visible ? reinterpret_cast<intptr_t>(character.representation) : 0);
    }
    // Original InvalidateStyleRedraw(): representation messages alone retain
    // cached line widths. Preserve the current caret preference while invoking
    // the same core invalidation before wrapping, hit-testing and repainting.
    editor_->Send(Scintilla::Message::SetCaretLineVisibleAlways,
                  editor_->Send(Scintilla::Message::GetCaretLineVisibleAlways));
}

void EditorFrame::ShowWordWrapSettings() {
    auto options = wordWrap_.Options();
    if (ShowWordWrapSettingsDialog(this, options))
        wordWrap_.ApplyOptions(options, settings_ ? settings_->values : defaults_);
}

void EditorFrame::AppendFoldCommands(wxMenu* view) {
    const int show = wxWindow::NewControlId();
    view->AppendCheckItem(show, _("Show Code &Folding\tCtrl+Shift+Alt+F"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetCodeFoldingVisible(!editor_->CodeFoldingVisible()); }, show);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->CodeFoldingVisible()); }, show);
    auto* folds = new wxMenu;
    const auto addFoldCommand = [this, folds](const wxString& label, FoldCommand command, int level = 0) {
        const int id = command == FoldCommand::DefaultLevels ? std::to_underlying(AppCommand::FoldDefaultLevels)
                                                             : wxWindow::NewControlId();
        if (command == FoldCommand::CurrentBlock) currentBlockFoldId_ = id;
        folds->Append(id, label);
        Bind(
            wxEVT_MENU,
            [this, command, level](wxCommandEvent&) {
                editor_->ToggleFolds(command, SyntaxFor(document_.Language()).original, level);
            },
            id);
    };
    using enum FoldCommand;
    // TRANSLATORS: Toggle only the fold levels configured by the current language (IDM_VIEW_FOLD_DEFAULT).
    addFoldCommand(_("&Default Levels\tShift+Alt+D"), DefaultLevels);
    // TRANSLATORS: Toggle all levels of code folding (IDM_VIEW_FOLD_ALL).
    addFoldCommand(_("&All Levels\tShift+Alt+A"), AllLevels);
    // TRANSLATORS: Toggle the fold block surrounding the caret (IDM_VIEW_FOLD_CURRENT_BLOCK).
    addFoldCommand(_("&Current Block\tAlt+C"), CurrentBlock);
    // TRANSLATORS: Toggle all blocks at the caret's fold depth (IDM_VIEW_FOLD_CURRENT_LEVEL).
    addFoldCommand(_("Current &Level"), CurrentLevel);
    folds->AppendSeparator();
    for (int level = 1; level < 10; ++level) {
        // TRANSLATORS: Both numbers are the fold depth 1–9; the second is part of the Alt shortcut.
        addFoldCommand(wxString::Format(_("Level &%d\tAlt+%d"), level, level), NumericLevel, level - 1);
    }
    // TRANSLATORS: The tenth fold depth, with 0 as the menu mnemonic (IDM_VIEW_FOLD_LEVEL10).
    addFoldCommand(_("Level 1&0"), NumericLevel, 9);
    // TRANSLATORS: View submenu for expanding or collapsing code folds (IDM_VIEW_FOLD_*).
    view->AppendSubMenu(folds, _("&Toggle Folds"));
}

void EditorFrame::AppendCurrentLineCommands(wxMenu* view) {
    const int highlight = wxWindow::NewControlId();
    view->AppendCheckItem(highlight, _("Highlight Current &Block"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) { editor_->SetCurrentBlockHighlighted(!editor_->CurrentBlockHighlighted()); },
        highlight);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->CurrentBlockHighlighted()); },
        highlight);
    auto* currentLine = new wxMenu;
    const auto addMode = [this, currentLine](const wxString& label, LineHighlightMode mode) {
        const int id = wxWindow::NewControlId();
        currentLine->AppendRadioItem(id, label);
        Bind(wxEVT_MENU, [this, mode](wxCommandEvent&) { editor_->SetCurrentLineHighlightMode(mode); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, mode](wxUpdateUIEvent& event) { event.Check(editor_->GetCurrentLineHighlightMode() == mode); }, id);
    };
    addMode(_("&No Highlight"), LineHighlightMode::None);
    addMode(_("Background &Color\tCtrl+Shift+I"), LineHighlightMode::BackgroundColor);
    addMode(_("Outline &Frame\tCtrl+Shift+F"), LineHighlightMode::OutlineFrame);
    currentLine->AppendSeparator();
    const int subline = wxWindow::NewControlId();
    currentLine->AppendCheckItem(subline, _("Highlight &Subline"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            editor_->Send(Scintilla::Message::SetCaretLineHighlightSubLine,
                          !editor_->Send(Scintilla::Message::GetCaretLineHighlightSubLine));
        },
        subline);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Check(editor_->Send(Scintilla::Message::GetCaretLineHighlightSubLine) != 0);
        },
        subline);
    view->AppendSubMenu(currentLine, _("Highlight C&urrent Line"));
}

void EditorFrame::AppendCaretCommands(wxMenu* view) {
    auto* caret = new wxMenu;
    const int overstrike = wxWindow::NewControlId();
    caret->AppendCheckItem(overstrike, _("Block (&OVR Mode)"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto appearance = editor_->GetCaretAppearance();
            appearance.overstrikeBlock = !appearance.overstrikeBlock;
            editor_->SetCaretAppearance(appearance);
        },
        overstrike);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->GetCaretAppearance().overstrikeBlock); },
        overstrike);
    caret->AppendSeparator();
    const std::array modes = {wxTRANSLATE("Block (&INS Mode)"), wxTRANSLATE("Line Width &1"),
                              wxTRANSLATE("Line Width &2"), wxTRANSLATE("Line Width &3")};
    for (int width = 0; width <= 3; ++width) {
        const int id = wxWindow::NewControlId();
        caret->AppendRadioItem(id, wxGetTranslation(modes[width]));
        Bind(
            wxEVT_MENU,
            [this, width](wxCommandEvent&) {
                auto appearance = editor_->GetCaretAppearance();
                appearance.lineWidth = width;
                editor_->SetCaretAppearance(appearance);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, width](wxUpdateUIEvent& event) { event.Check(editor_->GetCaretAppearance().lineWidth == width); },
            id);
    }
    caret->AppendSeparator();
    const int blink = wxWindow::NewControlId();
    caret->AppendCheckItem(blink, _("&No Blink"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto appearance = editor_->GetCaretAppearance();
            appearance.blinkPeriod = appearance.blinkPeriod == 0 ? -1 : 0;
            editor_->SetCaretAppearance(appearance);
        },
        blink);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(editor_->GetCaretAppearance().blinkPeriod == 0); }, blink);
    const int selection = wxWindow::NewControlId();
    caret->AppendCheckItem(selection, _("Draw Block Caret in &Selection"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto appearance = editor_->GetCaretAppearance();
            appearance.blockAfterSelection = !appearance.blockAfterSelection;
            editor_->SetCaretAppearance(appearance);
        },
        selection);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Check(!editor_->GetCaretAppearance().blockAfterSelection); }, selection);
    view->AppendSubMenu(caret, _("C&aret Style"));
}

void EditorFrame::ToggleIndentationGuides() {
    const auto& preferences = settings_ ? settings_->values : defaults_;
    ApplyIndentationGuides(*editor_, document_.Language(), editor_->Send(Scintilla::Message::GetIndentationGuides) == 0,
                           preferences.simpleIndentGuides);
}

void EditorFrame::ToggleLongLineMarker() {
    const auto& preferences = settings_ ? settings_->values : defaults_;
    const bool show = editor_->Send(Scintilla::Message::GetEdgeMode) == 0;
    editor_->Send(Scintilla::Message::SetEdgeMode, show ? preferences.longLineMode : 0);
    if (show) ApplyLongLineColour(*editor_);
}

void EditorFrame::AppendMarginCommands(wxMenu* view) {
    const int lineNumbers = wxWindow::NewControlId();
    view->AppendCheckItem(lineNumbers, _("Line &Numbers\tCtrl+Shift+N"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetLineNumbersVisible(!editor_->LineNumbersVisible()); },
        lineNumbers);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->LineNumbersVisible()); }, lineNumbers);
    const int bookmarkMargin = wxWindow::NewControlId();
    view->AppendCheckItem(bookmarkMargin, _("&Bookmark Margin\tCtrl+Shift+M"));
    Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetBookmarkMarginVisible(!editor_->BookmarkMarginVisible()); },
        bookmarkMargin);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->BookmarkMarginVisible()); },
        bookmarkMargin);
    const int changeHistory = wxWindow::NewControlId();
    view->AppendCheckItem(changeHistory, _("Change &History Marker"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->ToggleChangeHistory(); }, changeHistory);
    Bind(
        wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Check(editor_->ChangeHistoryOptions() != 0); },
        changeHistory);
}
