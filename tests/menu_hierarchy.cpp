#include "native_key_helpers.h"
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
    constexpr const char* names[] = {"File", "Edit", "Search", "View", "Scheme", "Settings"};
    Require(bar->GetMenuCount() == std::size(names), "delivered menus follow the original six groups");
    for (size_t index = 0; index < std::size(names); ++index)
        Require(bar->GetMenuLabelText(index) == names[index], "original top-level menu order");
    Order(bar->GetMenu(1), {"Undo", "Redo", "Cut", "Copy", "Paste", "Swap", "Clear Clipboard", "Copy to Clipboard",
                            "Selection", "Enclose Selection", "Lines", "Convert", "Insert", "Special"});
    Order(Submenu(bar->GetMenu(1), "Selection"),
          {"Duplicate", "Toggle Line Comment", "Toggle Block Comment", "Indent", "Unindent", "Strip Trailing Blanks",
           "Strip First Character", "Strip Last Character", "Strip Leading Blanks", "Merge Blank Lines",
           "Remove Blank Lines", "Merge Duplicate Lines", "Remove Duplicate Lines", "Pad With Spaces",
           "Compress Whitespace"});
    Order(Submenu(bar->GetMenu(1), "Lines"),
          {"Move Up", "Move Down", "Transpose", "Sort Lines...", "Modify Lines...", "Align Lines...", "Duplicate Line",
           "Cut Line", "Copy Line", "Delete Line", "Join Lines", "Split Lines", "Column Wrap...", "Join Paragraphs"});
    Order(Submenu(bar->GetMenu(1), "Special"),
          {"Character to Hex", "Hex to Character", "Escape C Chars", "Unescape C Chars"});
    Order(Submenu(bar->GetMenu(0), "Line Endings"),
          {"Windows (CR+LF)", "Unix/macOS (LF)", "Classic Mac OS (CR)", "Default..."});
    Order(bar->GetMenu(2),
          {"Find...", "Find Next", "Find Previous", "Replace...", "Find Matching Brace", "Select to Matching Brace",
           "Select Lines in Current Block", "Select to Next", "Select to Previous", "Bookmarks", "Goto"});
    Order(bar->GetMenu(3), {"Caret Style", "Word Wrap", "Long Line Marker", "Indentation Guides", "Show Whitespace",
                            "Show Line Endings", "Show Wrap Symbols", "Visual Brace Matching",
                            "Highlight Current Block", "Highlight Current Line", "Mark Occurrences", "Line Numbers",
                            "Bookmark Margin", "Change History Marker", "Show Code Folding", "Toggle Folds", "Zoom"});
    Order(Submenu(bar->GetMenu(2), "Goto"),
          {"Goto Line...", "Goto Block Start", "Goto Block End", "Goto Previous Block", "Goto Next Block",
           "Goto Previous Sibling Block", "Goto Next Sibling Block", "Goto Selection Start", "Goto Selection End"});
    Order(bar->GetMenu(5), {"Insert Tabs as Spaces", "Tab Settings...", "Long Line Settings...", "Auto Indent",
                            "Select and Edit Options", "Appearance", "File Change Notification...", "Other Settings",
                            "Save Settings On Exit", "Save Settings Now"});
    Order(Submenu(Submenu(bar->GetMenu(5), "Select and Edit Options"), "Line Selection Mode"),
          {"None", "Visual Studio", "Old Visual Studio", "Normal"});
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
