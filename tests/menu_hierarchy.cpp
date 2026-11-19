#include "native_key_helpers.h"
#include <array>
#include "settings/settings_store.h"
#include <wx/menu.h>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
wxMenu* Submenu(wxMenu* menu, const char* label) {
    for (auto* item : menu->GetMenuItems())
        if (item->GetItemLabelText() == label && item->GetSubMenu()) return item->GetSubMenu();
    throw std::runtime_error(std::string("missing submenu: ") + label);
}
void Order(wxMenu* menu, std::initializer_list<const char*> names) {
    auto expected = names.begin();
    for (auto* item : menu->GetMenuItems()) {
        if (item->IsSeparator()) continue;
        if (expected == names.end() || item->GetItemLabelText() != *expected) {
            std::fprintf(stderr, "menu item '%s', expected '%s'\n", item->GetItemLabelText().utf8_str().data(),
                         expected == names.end() ? "<end>" : *expected);
            throw std::runtime_error("delivered commands retain original within-group order");
        }
        ++expected;
    }
    Require(expected == names.end(), "all delivered commands appear in their original group");
}
void CheckMenu(EditorFrame* frame) {
    auto* bar = frame->GetMenuBar();
    constexpr std::array<const char*, 7> names = {{"File", "Edit", "Search", "View", "Scheme", "Settings", "Tools"}};
    Require(bar->GetMenuCount() == std::size(names), "delivered menus follow the original groups");
    for (size_t index = 0; index < std::size(names); ++index)
        Require(bar->GetMenuLabelText(index) == names[index], "original top-level menu order");
    Order(bar->GetMenu(1),
          {"Undo", "Redo", "Cut", "Copy", "Paste", "Select All", "Swap", "Clear Clipboard", "Copy to Clipboard",
           "Selection", "Enclose Selection", "Lines", "Convert", "Insert", "Special"});
    Order(Submenu(bar->GetMenu(1), "Selection"),
          {"Duplicate", "Toggle Line Comment", "Toggle Block Comment", "Indent", "Unindent", "Strip Trailing Blanks",
           "Strip First Character", "Strip Last Character", "Strip Leading Blanks", "Merge Blank Lines",
           "Remove Blank Lines", "Merge Duplicate Lines", "Remove Duplicate Lines", "Pad With Spaces",
           "Compress Whitespace"});
    Order(Submenu(bar->GetMenu(1), "Lines"),
          {"Move Up", "Move Down", "Transpose", "Sort Lines...", "Modify Lines...", "Align Lines...", "Duplicate Line",
           "Cut Line", "Copy Line", "Delete Line", "Join Lines", "Split Lines", "Column Wrap...", "Join Paragraphs"});
    // Notepad4.rc 89b8b36: the character-information commands follow Hex to Character.
    Order(Submenu(bar->GetMenu(1), "Special"),
          {"Complete Word", "Character to Hex", "Hex to Character", "Show Hex Code", "Show Character Info",
           "Escape C Chars", "Unescape C Chars"});
    Order(Submenu(bar->GetMenu(0), "Line Endings"),
          {"Windows (CR+LF)", "Unix/macOS (LF)", "Classic Mac OS (CR)", "Default..."});
    // Notepad4.rc at 89b8b36: the three source-codec commands precede
    // Default Encoding and With Encoding, retaining their original separator.
    auto* reload = Submenu(bar->GetMenu(0), "Reload");
    Order(reload, {"As UTF-8", "As ANSI", "As OEM", "Default Encoding", "With Encoding..."});
    Require(reload->GetMenuItemCount() == 6 && reload->FindItemByPosition(3)->IsSeparator(),
            "Reload retains the original separator between quick reload and the encoding chooser");
    Order(bar->GetMenu(2),
          {"Find...", "Save Find Text", "Find Next", "Find Previous", "Replace...", "Replace Next",
           "Find Matching Brace", "Select to Matching Brace", "Select Word", "Select Lines (Expand Selection)",
           "Select Lines in Current Block", "Select to Document Start", "Select to Document End", "Select to Next",
           "Select to Previous", "Bookmarks", "Goto"});
    Order(bar->GetMenu(3), {"Default Code Font...",
                            "Default Text Font...",
                            "Caret Style",
                            "Word Wrap",
                            "Long Line Marker",
                            "Indentation Guides",
                            "Show Whitespace",
                            "Show Line Endings",
                            "Show Wrap Symbols",
                            "Unicode Control Character",
                            "Visual Brace Matching",
                            "Highlight Current Block",
                            "Highlight Current Line",
                            "Mark Occurrences",
                            "Show CallTip",
                            "Line Numbers",
                            "Bookmark Margin",
                            "Change History Marker",
                            "Show Code Folding",
                            "Toggle Folds",
                            "Zoom",
#ifdef __WXMSW__
                            "Toggle Full Screen"});
#else
                            "Toggle Full Screen [Esc]"});
#endif
    auto* folds = Submenu(bar->GetMenu(3), "Toggle Folds");
    Order(folds, {"Default Levels", "All Levels", "Current Block", "Current Level", "Level 1", "Level 2", "Level 3",
                  "Level 4", "Level 5", "Level 6", "Level 7", "Level 8", "Level 9", "Level 10"});
    Require(folds->GetMenuItemCount() == 15 && folds->FindItemByPosition(4)->IsSeparator(),
            "Toggle Folds separates its four general commands from the ten numeric levels");
    Order(Submenu(bar->GetMenu(2), "Goto"),
          {"Goto Line...", "Goto Block Start", "Goto Block End", "Goto Previous Block", "Goto Next Block",
           "Goto Previous Sibling Block", "Goto Next Sibling Block", "Goto Selection Start", "Goto Selection End"});
    Order(bar->GetMenu(5),
          {"AutoSave Settings...", "Language", "Insert Tabs as Spaces", "Tab Settings...", "Word Wrap Settings...",
           "Long Line Settings...", "Auto Completion Settings...", "Auto Completion Ignore Case", "Auto Indent",
           "Select and Edit Options", "Appearance", "File Change Notification...", "Other Settings",
#ifdef __WXMSW__
           "Advanced Settings",
#endif
           "Save Settings On Exit", "Save Settings Now"});
#ifdef __WXMSW__
    Order(Submenu(bar->GetMenu(5), "Advanced Settings"), {"Rendering Technology"});
    Order(Submenu(Submenu(bar->GetMenu(5), "Advanced Settings"), "Rendering Technology"), {"Legacy GDI", "Direct2D"});
#endif
    Order(Submenu(bar->GetMenu(5), "Select and Edit Options"),
          {"Enable Multiple Selection", "Copy Selection As Find Text", "Copy Paste Buffer As Find Text",
           "Undo Redo Remember Selection", "Line Selection Mode"});
    Order(Submenu(Submenu(bar->GetMenu(5), "Select and Edit Options"), "Line Selection Mode"),
          {"None", "Visual Studio", "Old Visual Studio", "Normal"});
    Order(bar->GetMenu(6),
          {"Save Before Running Tools", "Execute Document", "Open Document With...", "Run Command...", "Base64"});
    const int execute = bar->FindMenuItem("Tools", "Execute Document");
    const int openWith = bar->FindMenuItem("Tools", "Open Document With...");
    const int saveTools = bar->FindMenuItem("Tools", "Save Before Running Tools");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!bar->IsEnabled(execute) && bar->IsEnabled(openWith), "only Execute Document requires a named file");
    Require(bar->FindItem(saveTools)->IsCheckable() && !bar->IsChecked(saveTools),
            "save-before-tools is visible and defaults off");
    Order(Submenu(bar->GetMenu(6), "Base64"),
          {"Standard Encode", "URL Safe Encode", "Encode as HTML Embedded Image", "Decode", "Decode as Hex"});
}
void Actions(EditorFrame* frame, SettingsStore& settings) {
    using Scintilla::Message;
    auto* editor = frame->Editor();
    auto* bar = frame->GetMenuBar();
    const auto command = [frame, bar](const char* menu, const char* label) {
        const int id = bar->FindMenuItem(menu, label);
        Require(id != wxNOT_FOUND, "moved native command exists");
        NativeSearch::Command(frame, id);
    };
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("alpha\r\nbeta\r\n"));
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    editor->Send(Message::SetSel, 1, 4);
    const auto encoding = frame->DocumentEncoding();
    const auto saveFind = settings.values.saveFindReplace;
    CheckMenu(frame);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(NativeSearch::Text(editor) == "alpha\r\nbeta\r\n" && editor->Send(Message::GetAnchor) == 1 &&
                editor->Send(Message::GetCurrentPos) == 4 && !editor->Send(Message::CanUndo) &&
                frame->DocumentEncoding() == encoding && settings.values.saveFindReplace == saveFind,
            "inspecting every menu leaves bytes, selection, encoding, history and preferences unchanged");
    command("Search", "Goto Selection Start");
    NativeSearch::Selection(editor, 1, 4, "Goto retains selected bytes while changing active boundary");
    Require(editor->Send(Message::GetCurrentPos) == 1, "selection-start navigation retains original handler");
    command("Edit", "Duplicate Line");
    Require(NativeSearch::Text(editor) == "alpha\r\nalpha\r\nbeta\r\n", "moved line action retains actual edit");
    editor->Send(Message::Undo);
    NativeKeys::Shortcut(frame, 'D', true);
    Require(NativeSearch::Text(editor) == "alpha\r\nalpha\r\nbeta\r\n",
            "actual Ctrl+D dispatch reaches moved line action");
    editor->Send(Message::Undo);
    command("File", "Unix/macOS (LF)");
    Require(NativeSearch::Text(editor) == "alpha\nbeta\n", "moved EOL action converts actual bytes");
    Require(bar->IsChecked(bar->FindMenuItem("File", "Unix/macOS (LF)")), "moved EOL checked state follows editor");
    editor->Send(Message::Undo);
    command("File", "Read Only Mode");
    command("Edit", "Duplicate Line");
    Require(NativeSearch::Text(editor) == "alpha\r\nbeta\r\n", "moved line action retains read-only behavior");
    command("File", "Read Only Mode");
    editor->Send(Message::SetSel, 0, 0);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!bar->IsEnabled(bar->FindMenuItem("Edit", "Upper Case")) &&
                !bar->IsEnabled(bar->FindMenuItem("Edit", "Character to Hex")) &&
                !bar->IsEnabled(bar->FindMenuItem("Edit", "Split Lines")),
            "moved conversion and line actions retain empty-selection disabled state");
    command("Settings", "Remember Search Options");
    Require(settings.values.saveFindReplace != saveFind &&
                bar->IsChecked(bar->FindMenuItem("Settings", "Remember Search Options")) ==
                    settings.values.saveFindReplace,
            "moved search preference retains value and checked state");
    command("Settings", "Remember Search Options");
}
class MenuApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame(&settings_);
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "owned native window must be presented");
                Actions(frame, settings_);
                result_ = 0;
                std::puts("PASS: original delivered menu hierarchy");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    SettingsStore settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(MenuApp);
