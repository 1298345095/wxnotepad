#include "app/editor_frame.h"
#include "app/localization.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include "syntax_test_helpers.h"
#include "native_dialog_helpers.h"
#include "native_search_helpers.h"
#include "status_helpers.h"
#include "file_helpers.h"
#include "native_tree_input.h"
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/timer.h>
#include <wx/treectrl.h>
#include <wx/convauto.h>
#include <filesystem>
#include <array>
#include <memory>
#include <string_view>
#include <cstdio>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
wxString Ini(const std::filesystem::path& path) {
    const auto bytes = TestFiles::Bytes(path);
    return wxString(bytes.data(), wxConvAuto(), bytes.size());
}
class Picker final : public wxTimer {
public:
    int shown = 0;
    wxString title = "Select Scheme";
    std::function<void(wxDialog&, wxTreeCtrl&)> action;
    wxString error;
    Picker() { Start(40); }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() || dialog->GetTitle() != title) continue;
            Stop();
            ++shown;
            try {
                auto* tree = wxDynamicCast(wxWindow::FindWindowByName("Syntax schemes", dialog), wxTreeCtrl);
                Require(tree, "native scheme dialog exposes its tree");
                std::printf("DIALOG: %s\n", title.utf8_str().data());
                action(*dialog, *tree);
            } catch (const std::exception& failure) {
                error = failure.what();
                dialog->EndModal(wxID_CANCEL);
            }
        }
    }
};
void Choose(EditorFrame* frame, const wxString& label, bool cancel = false) {
    Picker picker;
    picker.action = [label, cancel](wxDialog& dialog, wxTreeCtrl& tree) {
        Require(tree.GetItemText(tree.GetSelection()) == "Text File", "current base scheme initially selected");
        wxTreeItemIdValue cookie;
        auto item = tree.GetFirstChild(tree.GetRootItem(), cookie);
        Require(tree.GetItemText(item) == "Text File", "fixed Text File appears first");
        item = tree.GetNextChild(tree.GetRootItem(), cookie);
        Require(tree.GetItemText(item) == "Favorite Schemes", "favorites appear before alphabetic groups");
        Require(NativeDialogs::SelectTreeLabel(tree, label), "delivered base scheme exists in native tree");
        NativeSearch::Click(&dialog, cancel ? wxID_CANCEL : wxID_OK);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Syntax Scheme..."));
    picker.Stop();
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(picker.shown == 1, "Syntax Scheme opens exactly one real modal chooser");
}
void DefaultScheme() {
    const auto ini = TestFiles::TemporaryDirectory("scheme-default.ini");
    TestFiles::WriteBytes(ini, "[Settings]\nSaveSettings=0\n[Styles]\nDefaultScheme=32\nAutoSelect=1\n"
                               "FavoriteSchemes=999 67 1\nUnrelated=keep\n");
    SettingsStore settings(TestFiles::Filename(ini));
    auto* frame = new EditorFrame(&settings);
    frame->Show();
    Require(StatusMetadata(frame).Contains("Python") && !StatusMetadata(frame).Contains("override"),
            "original DefaultScheme=32 starts a new document in Python without an explicit override");
    Picker picker;
    picker.action = [](wxDialog& dialog, wxTreeCtrl& tree) {
        auto* defaultScheme = wxDynamicCast(wxWindow::FindWindowByName("Default scheme", &dialog), wxCheckBox);
        auto* automatic = wxDynamicCast(wxWindow::FindWindowByName("AutoSelect", &dialog), wxCheckBox);
        Require(defaultScheme && automatic, "original default and automatic-selection checkboxes exist");
        Require(defaultScheme->GetValue() && automatic->GetValue(), "checkboxes reflect original preferences");
        Require(NativeDialogs::SelectTreeLabel(tree, "Rust Source"), "Rust base scheme exists");
        Require(!defaultScheme->GetValue(), "default check follows the selected base scheme");
        NativeSearch::Toggle(defaultScheme, true);
        Require(NativeDialogs::SelectTreeLabel(tree, "C"), "alphabetic group is selectable");
        Require(!defaultScheme->IsEnabled() && defaultScheme->GetValue(),
                "selecting a group disables the unchanged temporary default checkbox");
        Require(NativeDialogs::SelectTreeLabel(tree, "Rust Source") && defaultScheme->GetValue(),
                "temporary default survives changing tree selection");
        NativeSearch::Toggle(automatic, false);
        NativeSearch::Click(&dialog, wxID_OK);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Syntax Scheme..."));
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(picker.shown == 1 && !settings.values.autoSelect, "accepted real dialog commits AutoSelect");
    Require(frame->NewDocument() && StatusMetadata(frame).Contains("Rust") &&
                !StatusMetadata(frame).Contains("override"),
            "New uses the accepted default and clears only the current-document override");
    const auto cpp = TestFiles::TemporaryDirectory("scheme-default.cpp");
    TestFiles::WriteBytes(cpp, "class Example {};\n");
    Require(frame->Open(TestFiles::Filename(cpp)).status == OpenStatus::Opened &&
                StatusMetadata(frame).Contains("Rust"),
            "AutoSelect off uses the default for future opens even with a recognized extension");
    Require(settings.Save(false) && Ini(ini).Contains("DefaultScheme=32") && !Ini(ini).Contains("AutoSelect=0"),
            "disabled exit saving preserves stored scheme preferences after accepted dialog edits");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
    const auto saved = Ini(ini);
    Require(saved.Contains("DefaultScheme=67") && saved.Contains("AutoSelect=0") &&
                saved.Contains("FavoriteSchemes=999 67 1") && saved.Contains("Unrelated=keep"),
            "Save Settings Now writes original stable identity and preserves untouched/unavailable favorites");
    frame->Destroy();
    // Pinned EditLexer.h: 5 is Resource Script, which is not delivered. Java's
    // original ID 2 is supplied by #255 and must become available automatically.
    TestFiles::WriteBytes(ini, "[Styles]\nDefaultScheme=5\n");
    SettingsStore unavailable(TestFiles::Filename(ini));
    frame = new EditorFrame(&unavailable);
    Require(StatusMetadata(frame).Contains("Plain Text"),
            "unavailable original Resource Script cannot alias an internal language ordinal");
    Picker fallback;
    fallback.action = [](wxDialog& dialog, wxTreeCtrl&) {
        auto* control = wxDynamicCast(wxWindow::FindWindowByName("Default scheme", &dialog), wxCheckBox);
        Require(control && control->GetValue(), "chooser displays the supported runtime fallback as default");
        NativeSearch::Click(&dialog, wxID_OK);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Syntax Scheme..."));
    Require(fallback.error.empty(), fallback.error.utf8_str());
    Require(unavailable.Save(true) && Ini(ini).Contains("DefaultScheme=5"),
            "an untouched unavailable default survives settings round-trip");
    frame->Destroy();
    TestFiles::WriteBytes(ini, "[Styles]\nDefaultScheme=54\n");
    SettingsStore json(TestFiles::Filename(ini));
    frame = new EditorFrame(&json);
    Require(StatusMetadata(frame).Contains("JSON") && frame->Editor()->Send(Scintilla::Message::GetTabWidth) == 2 &&
                frame->Editor()->Send(Scintilla::Message::GetUseTabs) == 0,
            "initial default applies its original independent editing metadata as well as highlighting");
    frame->Destroy();
}
void Favorites() {
    const auto ini = TestFiles::TemporaryDirectory("scheme-favorites.ini");
    TestFiles::WriteBytes(ini, "[Settings]\nSaveSettings=0\n[Styles]\nDefaultScheme=32\n"
                               "FavoriteSchemes=999 67 1\n");
    SettingsStore settings(TestFiles::Filename(ini));
    auto* frame = new EditorFrame(&settings);
    frame->Show();
    Picker picker;
    picker.title = "Favorite Schemes";
    picker.action = [](wxDialog& dialog, wxTreeCtrl& tree) {
        Require(NativeDialogs::SelectTreeLabel(tree, "Favorite Schemes"), "native favorites group exists");
        wxTreeItemIdValue cookie;
        auto item = tree.GetFirstChild(tree.GetSelection(), cookie);
        Require(item.IsOk() && tree.GetItemText(item) == "Rust Source", "favorites use original stable identities");
        Require(tree.GetItemState(item) == 1, "favorite-management tree exposes native checked state images");
        item = tree.GetNextChild(tree.GetSelection(), cookie);
        Require(item.IsOk() && tree.GetItemText(item) == "C/C++ Source", "favorites retain original preference order");
        Require(!tree.GetNextChild(tree.GetSelection(), cookie).IsOk(),
                "unavailable favorites do not alias delivered enums");
        Require(NativeDialogs::SelectTreeLabel(tree, "Text File") &&
                    tree.GetItemState(tree.GetSelection()) == wxTREE_ITEMSTATE_NONE,
                "fixed Text File has no favorite checkbox");
        const auto toggle = [&tree](const wxString& label) {
            Require(NativeDialogs::SelectTreeLabel(tree, label), "favorite checkbox target exists");
            wxTreeEvent click(wxEVT_TREE_STATE_IMAGE_CLICK, &tree, tree.GetSelection());
            Require(tree.GetEventHandler()->ProcessEvent(click), "native tree handles its state-image click");
        };
        toggle("Rust Source");
        Require(NativeDialogs::SelectTreeLabel(tree, "Rust Source") && tree.GetItemState(tree.GetSelection()) == 0,
                "unchecking removes favorite copy and clears its alphabetic copy");
        toggle("Go Source");
        Require(NativeDialogs::SelectTreeLabel(tree, "Favorite Schemes"), "favorite group remains");
        item = tree.GetFirstChild(tree.GetSelection(), cookie);
        Require(tree.GetItemText(item) == "C/C++ Source", "unchecking removes only the requested favorite");
        item = tree.GetNextChild(tree.GetSelection(), cookie);
        Require(tree.GetItemText(item) == "Go Source" && tree.GetItemState(item) == 1,
                "checking a general scheme appends a checked favorite");
        const auto go = item;
        Require(NativeDialogs::SelectTreeLabel(tree, "Favorite Schemes"), "favorite drop group exists");
        const auto dropTarget = tree.GetSelection();
        wxTreeEvent begin(wxEVT_TREE_BEGIN_DRAG, &tree, go);
        begin.Veto();
        Require(tree.GetEventHandler()->ProcessEvent(begin) && begin.IsAllowed(),
                "favorite mode allows native drag for a delivered non-text base scheme");
        wxTreeEvent drop(wxEVT_TREE_END_DRAG, &tree, dropTarget);
        Require(tree.GetEventHandler()->ProcessEvent(drop), "favorite mode handles native tree drop");
        Require(NativeDialogs::SelectTreeLabel(tree, "Favorite Schemes"), "favorite order remains visible");
        item = tree.GetFirstChild(tree.GetSelection(), cookie);
        Require(tree.GetItemText(item) == "Go Source", "dropping onto expanded favorites inserts first");
        item = tree.GetNextChild(tree.GetSelection(), cookie);
        Require(tree.GetItemText(item) == "C/C++ Source", "native drag preserves all other favorite order");
        Require(NativeDialogs::SelectTreeLabel(tree, "Go Source"), "favorite mode permits selecting a base scheme");
        auto* control = wxDynamicCast(wxWindow::FindWindowByName("Default scheme", &dialog), wxCheckBox);
        Require(control, "favorite mode exposes the original default checkbox");
        NativeSearch::Toggle(control, true);
        NativeSearch::Click(&dialog, wxID_OK);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Favorite Schemes..."));
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(picker.shown == 1 && StatusMetadata(frame).Contains("Python") &&
                !StatusMetadata(frame).Contains("override"),
            "favorite management accepts preferences without changing current document language");
    Require(frame->NewDocument() && StatusMetadata(frame).Contains("Go"),
            "favorite-mode default governs future New documents");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
    Require(Ini(ini).Contains("FavoriteSchemes=999 15 1"),
            "favorite changes save original identities in order while retaining unavailable entries");
    frame->Destroy();
}
void PointerFavorites() {
    const auto ini = TestFiles::TemporaryDirectory("scheme-pointer.ini");
    TestFiles::WriteBytes(ini, "[Settings]\nSaveSettings=0\n[Styles]\nFavoriteSchemes=67 1\n");
    SettingsStore settings(TestFiles::Filename(ini));
    auto* frame = new EditorFrame(&settings);
    frame->Show();
    Picker picker;
    picker.title = "Favorite Schemes";
    picker.action = [](wxDialog& dialog, wxTreeCtrl& tree) {
        NativeTreeInput::Present(dialog);
        Require(NativeDialogs::SelectTreeLabel(tree, "Favorite Schemes"), "native pointer favorites group exists");
        const auto favorites = tree.GetSelection();
        wxTreeItemIdValue cookie;
        auto item = tree.GetFirstChild(favorites, cookie);
        NativeTreeInput::Click(tree, item);
        NativeTreeInput::Wait([&tree, favorites] { return tree.GetChildrenCount(favorites, false) == 1; },
                              "real mouse unchecks and removes exactly one favorite");
        Require(NativeDialogs::SelectTreeLabel(tree, "Go Source"), "native pointer can select a general scheme");
        NativeTreeInput::Click(tree, tree.GetSelection());
        NativeTreeInput::Wait([&tree, favorites] { return tree.GetChildrenCount(favorites, false) == 2; },
                              "real mouse checks and appends a favorite");
        item = tree.GetFirstChild(favorites, cookie);
        item = tree.GetNextChild(favorites, cookie);
        Require(tree.GetItemText(item) == "Go Source", "real pointer appended the requested stable scheme");
        NativeTreeInput::Drag(tree, item, favorites);
        NativeTreeInput::Wait(
            [&tree, favorites] {
                wxTreeItemIdValue position;
                return tree.GetItemText(tree.GetFirstChild(favorites, position)) == "Go Source";
            },
            "real native drag reorders favorites before the first entry");
        NativeSearch::Click(&dialog, wxID_OK);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Favorite Schemes..."));
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(picker.shown == 1 && settings.values.favoriteSchemes == "15 1" &&
                StatusMetadata(frame).Contains("Plain Text"),
            "real pointer favorite editing commits order without changing the current document scheme");
    Picker cancel;
    cancel.title = "Favorite Schemes";
    cancel.action = [](wxDialog& dialog, wxTreeCtrl& tree) {
        NativeTreeInput::Present(dialog);
        Require(NativeDialogs::SelectTreeLabel(tree, "Rust Source"), "cancelled favorite edit selects Rust");
        NativeTreeInput::Click(tree, tree.GetSelection());
        auto* selected = wxDynamicCast(wxWindow::FindWindowByName("Default scheme", &dialog), wxCheckBox);
        auto* automatic = wxDynamicCast(wxWindow::FindWindowByName("AutoSelect", &dialog), wxCheckBox);
        NativeSearch::Toggle(selected, true);
        NativeSearch::Toggle(automatic, false);
        NativeSearch::Click(&dialog, wxID_CANCEL);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Favorite Schemes..."));
    Require(cancel.error.empty(), cancel.error.utf8_str());
    Require(settings.values.favoriteSchemes == "15 1" && settings.values.defaultScheme == 0 &&
                settings.values.autoSelect,
            "native Cancel discards favorite edits and both temporary scheme preferences");
    Picker reopened;
    reopened.action = [](wxDialog& dialog, wxTreeCtrl& tree) {
        Require(NativeDialogs::SelectTreeLabel(tree, "Favorite Schemes"), "normal chooser reopens favorites");
        wxTreeItemIdValue cookie;
        auto item = tree.GetFirstChild(tree.GetSelection(), cookie);
        Require(tree.GetItemText(item) == "Go Source" && tree.GetItemState(item) == wxTREE_ITEMSTATE_NONE,
                "normal chooser reopens accepted favorite order without management checkboxes");
        item = tree.GetNextChild(tree.GetSelection(), cookie);
        Require(tree.GetItemText(item) == "C/C++ Source" && !tree.GetNextChild(tree.GetSelection(), cookie).IsOk(),
                "cancelled favorite copy does not reappear on reopening");
        NativeSearch::Click(&dialog, wxID_CANCEL);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Syntax Scheme..."));
    Require(reopened.error.empty(), reopened.error.utf8_str());
    frame->Destroy();
}
void FavoriteCapacity() {
    // Independent pinned EditLexer.h IDs, all undelivered by #254/#255/#256.
    const wxString unavailable = "7 8 9 11 12 13 14 16 17 18 19 20 21 22 23 24 29 30 31 33 35 36 38 39 40 41 42 43 44";
    const auto ini = TestFiles::TemporaryDirectory("scheme-capacity.ini");
    TestFiles::WriteBytes(ini, "[Settings]\nSaveSettings=0\n[Styles]\nFavoriteSchemes=1 67 " +
                                   unavailable.utf8_string() + "\n");
    SettingsStore settings(TestFiles::Filename(ini));
    auto* frame = new EditorFrame(&settings);
    frame->Show();
    Picker picker;
    picker.title = "Favorite Schemes";
    picker.action = [](wxDialog& dialog, wxTreeCtrl& tree) {
        const auto toggle = [&tree](const wxString& label) {
            Require(NativeDialogs::SelectTreeLabel(tree, label), "favorite capacity target is delivered");
            wxTreeEvent click(wxEVT_TREE_STATE_IMAGE_CLICK, &tree, tree.GetSelection());
            Require(tree.GetEventHandler()->ProcessEvent(click), "native favorite checkbox handles capacity edit");
        };
        toggle("Go Source");
        Require(NativeDialogs::SelectTreeLabel(tree, "Go Source") && tree.GetItemState(tree.GetSelection()) == 0,
                "retained unavailable favorites count toward the original 31-slot limit");
        toggle("Rust Source");
        toggle("Go Source");
        Require(NativeDialogs::SelectTreeLabel(tree, "Go Source") && tree.GetItemState(tree.GetSelection()) == 1,
                "freeing one original favorite slot permits a replacement supported favorite");
        NativeSearch::Click(&dialog, wxID_OK);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Favorite Schemes..."));
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(settings.values.favoriteSchemes == "1 15 " + unavailable && settings.Save(true),
            "favorite capacity preserves every unavailable ID while replacing one supported slot");
    SettingsStore reopened(TestFiles::Filename(ini));
    Require(reopened.values.favoriteSchemes == "1 15 " + unavailable,
            "accepted favorite replacement remains inside original limits across settings restart");
    frame->Destroy();
    wxString wideIds;
    for (int identity = 1000; identity <= 1022; ++identity) {
        if (!wideIds.empty()) wideIds += ' ';
        wideIds += wxString::Format("%d", identity);
    }
    // Imported unavailable IDs can occupy more than the original three digits:
    // 119 initial characters + Go(3) + Python(3) = 125; adding JSON would be128.
    TestFiles::WriteBytes(ini,
                          "[Settings]\nSaveSettings=0\n[Styles]\nFavoriteSchemes=1 67 " + wideIds.utf8_string() + "\n");
    SettingsStore wide(TestFiles::Filename(ini));
    frame = new EditorFrame(&wide);
    frame->Show();
    Picker characters;
    characters.title = "Favorite Schemes";
    characters.action = [](wxDialog& dialog, wxTreeCtrl& tree) {
        for (const auto* label : {"Go Source", "Python Script", "JSON Document"}) {
            Require(NativeDialogs::SelectTreeLabel(tree, label), "character-budget checkbox target is delivered");
            wxTreeEvent click(wxEVT_TREE_STATE_IMAGE_CLICK, &tree, tree.GetSelection());
            Require(tree.GetEventHandler()->ProcessEvent(click), "native checkbox handles serialized-character limit");
        }
        Require(NativeDialogs::SelectTreeLabel(tree, "JSON Document") && tree.GetItemState(tree.GetSelection()) == 0,
                "127-character original budget rejects an edit even below31 stored identities");
        NativeSearch::Click(&dialog, wxID_OK);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Favorite Schemes..."));
    Require(characters.error.empty(), characters.error.utf8_str());
    Require(wide.values.favoriteSchemes == "1 67 " + wideIds + " 15 32" && wide.values.favoriteSchemes.length() == 125,
            "accepted edits retain all unavailable IDs without exceeding the original character buffer");
    frame->Destroy();
}
void KeyboardChooser() {
    auto* frame = new EditorFrame;
    frame->SetTitle("wxnotepad scheme F12");
    frame->Show();
    bool presented = false;
    NativeKeys::AfterPresentation(frame, [&presented](bool ready) { presented = ready; });
    NativeTreeInput::Wait([&presented] { return presented; }, "F12 test frame is presented before native shortcut");
    wxYield();
    frame->SetTitle("wxnotepad scheme F12");
    {
        Picker modified;
        modified.action = [](wxDialog& dialog, wxTreeCtrl&) { NativeSearch::Click(&dialog, wxID_CANCEL); };
        // Pinned Ctrl+F12 belongs to undelivered Customize Schemes, not the
        // public Syntax Scheme chooser. Do not let GTK alias it to bare F12.
        NativeKeys::Shortcut(frame, WXK_F12, true, false, false, nullptr, "wxnotepad scheme F12");
        Require(modified.error.empty(), modified.error.utf8_str());
        Require(modified.shown == 0, "Ctrl+F12 does not activate the unrelated bare-F12 scheme command");
    }
    Picker picker;
    picker.action = [](wxDialog& dialog, wxTreeCtrl& tree) {
        NativeTreeInput::Present(dialog);
        Require(NativeDialogs::SelectTreeLabel(tree, "Python Script"), "keyboard chooser exposes Python base");
        tree.SetFocus();
        NativeTreeInput::Enter(dialog);
    };
    NativeKeys::Shortcut(frame, WXK_F12, false, false, false, nullptr, "wxnotepad scheme F12");
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(picker.shown == 1 && StatusMetadata(frame).Contains("Python") && StatusMetadata(frame).Contains("override"),
            "native F12 and Enter select the current scheme");
    Picker management;
    management.title = "Favorite Schemes";
    management.action = [](wxDialog& dialog, wxTreeCtrl& tree) {
        NativeTreeInput::Present(dialog);
        Require(NativeDialogs::SelectTreeLabel(tree, "Go Source"), "favorite keyboard chooser exposes Go base");
        tree.SetFocus();
        NativeTreeInput::Enter(dialog);
    };
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Favorite Schemes..."));
    Require(management.error.empty(), management.error.utf8_str());
    Require(management.shown == 1 && StatusMetadata(frame).Contains("Python"),
            "favorite-mode keyboard confirmation preserves current scheme");
    frame->Destroy();
}
using LocalizedSchemeCaptions = std::array<std::array<const char*, 3>, 21>;

void CheckLocalizedCaptions(wxTreeCtrl& tree, const LocalizedSchemeCaptions& captions, size_t column) {
    for (const auto& caption : captions) {
        const auto expected = wxString::FromUTF8(caption[column]);
        std::printf("CAPTION: %s\n", expected.utf8_str().data());
        Require(NativeDialogs::SelectTreeLabel(tree, expected),
                "every delivered base scheme has its independently expected localized tree caption");
    }
}

wxCheckBox* CheckLocalizedDialogLayout(wxDialog& dialog, wxTreeCtrl& tree) {
    auto* selected = wxDynamicCast(wxWindow::FindWindowByName("Default scheme", &dialog), wxCheckBox);
    auto* automatic = wxDynamicCast(wxWindow::FindWindowByName("AutoSelect", &dialog), wxCheckBox);
    auto* ok = dialog.FindWindow(wxID_OK);
    auto* cancel = dialog.FindWindow(wxID_CANCEL);
    Require(selected && automatic && ok && cancel, "translated native scheme dialog has original bottom controls");
    for (auto* control : {selected, automatic}) {
        Require(control->GetSize().x >= control->GetBestSize().x &&
                    control->GetRect().GetRight() < dialog.GetClientSize().x,
                "translated default/automatic captions fit their native controls");
    }
    Require(tree.GetRect().GetBottom() < selected->GetPosition().y &&
                selected->GetRect().GetBottom() < automatic->GetPosition().y &&
                automatic->GetRect().GetBottom() < ok->GetPosition().y,
            "tree and bottom controls retain original vertical order without overlap");
    const auto client = dialog.GetClientSize();
    const auto treeSize = tree.GetSize();
    const auto optionPosition = selected->GetPosition();
    const auto buttonPosition = ok->GetPosition();
    dialog.SetClientSize(client + dialog.FromDIP(wxSize(120, 100)));
    dialog.Layout();
    wxYield();
    const auto growth = dialog.GetClientSize() - client;
    Require(growth.x > 0 && growth.y > 0 && tree.GetSize() - treeSize == growth &&
                selected->GetPosition().y - optionPosition.y == growth.y &&
                ok->GetPosition() - buttonPosition == wxPoint(growth.x, growth.y) &&
                cancel->GetPosition().y == ok->GetPosition().y,
            "two-axis resize grows the tree and moves original bottom options/buttons together");
    return selected;
}

void OpenLocalizedChooser(EditorFrame* frame, const LocalizedSchemeCaptions& captions, size_t column,
                          const wxString& python, bool favorite) {
    Picker picker;
    picker.title = wxGetTranslation(favorite ? "Favorite Schemes" : "Select Scheme");
    picker.action = [&captions, column, python, favorite](wxDialog& dialog, wxTreeCtrl& tree) {
        CheckLocalizedCaptions(tree, captions, column);
        auto* selected = CheckLocalizedDialogLayout(dialog, tree);
        if (!favorite) {
            Require(NativeDialogs::SelectTreeLabel(tree, python), "localized Python chooser item is selectable");
            NativeSearch::Toggle(selected, true);
        }
        NativeSearch::Click(&dialog, favorite ? wxID_CANCEL : wxID_OK);
    };
    const auto caption =
        wxStripMenuCodes(wxGetTranslation(favorite ? "&Favorite Schemes..." : "&Syntax Scheme...\tF12"));
    int command = wxNOT_FOUND;
    for (auto* item : frame->GetMenuBar()->GetMenu(4)->GetMenuItems()) {
        if (item->GetItemLabelText() == caption) command = item->GetId();
    }
    Require(command != wxNOT_FOUND, "translated original Scheme command is visible");
    if (favorite)
        NativeSearch::Command(frame, command);
    else
        NativeKeys::Shortcut(frame, WXK_F12, false, false, false, nullptr, "wxnotepad localized scheme F12");
    Require(picker.error.empty(), picker.error.utf8_str());
    Require(picker.shown == 1, "translated native chooser opened exactly once");
}

void CheckLocalizedIni(SettingsStore& settings, const LocalizedSchemeCaptions& captions, size_t column) {
    Require(settings.Save(true), "accepted localized scheme settings save through the ordinary lifecycle");
    const auto saved = Ini(TestFiles::Utf8Path(settings.Path().utf8_string()));
    Require(saved.Contains("DefaultScheme=32") && saved.Contains("FavoriteSchemes=1 67") &&
                saved.Contains("[Python Script]") && saved.Contains("[C/C++ Source]"),
            "localized chooser preserves original numeric default/favorites and English INI identities");
    if (column != 0) {
        for (const auto& caption : captions)
            if (std::string_view(caption[0]) != caption[column])
                Require(!saved.Contains("[" + wxString::FromUTF8(caption[column]) + "]"),
                        "visible translated captions never become INI section identities");
    }
    SettingsStore reopened(settings.Path());
    Require(reopened.values.defaultScheme == 32 && reopened.values.favoriteSchemes == "1 67",
            "localized default and favorite identities survive configuration reload");
    std::puts("PASS: all localized F12/favorite captions and original INI identities");
}

void LocalizedLayout(EditorFrame* frame, SettingsStore& settings, int language) {
    // Independent expected captions: pinned original scheme titles, existing
    // application terminology, and original zh-Hans/zh-Hant NP2LEX_TEXTFILE.
    constexpr LocalizedSchemeCaptions captions = {{
        {"Text File", "文本文件", "文字檔案"},
        {"C/C++ Source", "C/C++ 源代码", "C/C++ 原始碼"},
        {"C# Source", "C# 源代码", "C# 原始碼"},
        {"Java Source", "Java 源代码", "Java 原始碼"},
        {"Kotlin Source", "Kotlin 源代码", "Kotlin 原始碼"},
        {"Python Script", "Python 脚本", "Python 指令碼"},
        {"JSON Document", "JSON 文档", "JSON 文件"},
        {"JavaScript", "JavaScript", "JavaScript"},
        {"TypeScript", "TypeScript", "TypeScript"},
        {"Shell Script", "Shell 脚本", "Shell 指令碼"},
        {"Web Source Code", "Web 源代码", "網頁原始碼"},
        {"XML Document", "XML 文档", "XML 文件"},
        {"Rust Source", "Rust 源代码", "Rust 原始碼"},
        {"Go Source", "Go 源代码", "Go 原始碼"},
        {"Lua Script", "Lua 脚本", "Lua 指令碼"},
        {"Markdown", "Markdown", "Markdown"},
        {"TOML File", "TOML 文件", "TOML 檔案"},
        {"YAML Document", "YAML 文档", "YAML 文件"},
        {"CMake Script", "CMake 脚本", "CMake 指令碼"},
        {"CSS Style Sheet", "CSS 样式表", "CSS 樣式表"},
        {"SQL Query", "SQL 查询", "SQL 查詢"},
    }};
    constexpr int SimplifiedChineseLanguage = 2052;
    constexpr int TraditionalChineseLanguage = 1028;
    constexpr size_t PythonCaptionIndex = 5;
    const size_t column = language == SimplifiedChineseLanguage ? 1 : language == TraditionalChineseLanguage ? 2 : 0;
    const auto python = wxString::FromUTF8(captions[PythonCaptionIndex][column]);
    frame->SetTitle("wxnotepad localized scheme F12");
    bool presented = false;
    NativeKeys::AfterPresentation(frame, [&presented](bool ready) { presented = ready; });
    NativeTreeInput::Wait([&presented] { return presented; },
                          "localized F12 frame is presented before native shortcut");
    wxYield();
    // Initial editor-state events refresh the document title during mapping.
    // Set the owned focus target after that refresh, as KeyboardChooser does.
    frame->SetTitle("wxnotepad localized scheme F12");
    for (const bool favorite : {false, true})
        OpenLocalizedChooser(frame, captions, column, python, favorite);
    Require(frame->Editor()->Send(Scintilla::Message::GetTabWidth) == 6,
            "localized Python selection loads the original English INI scheme section");
    CheckLocalizedIni(settings, captions, column);
}
class SchemeApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        wxString language;
        const bool layout = wxGetEnv("WXNOTEPAD_SCHEME_LAYOUT_LANGUAGE", &language);
        if (layout) AppLocalization::Initialize(static_cast<uint16_t>(wxAtoi(language)));
        if (layout) {
            const auto ini = TestFiles::TemporaryDirectory("scheme-localized.ini");
            TestFiles::WriteBytes(ini, "[Settings]\nSaveSettings=0\n[Styles]\nFavoriteSchemes=1 67\n"
                                       "[C/C++ Source]\nTabWidth=11\nUseGlobalTabSettings=0\n"
                                       "[Python Script]\nTabWidth=6\nUseGlobalTabSettings=0\n");
            settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(ini));
        }
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame, layout, language] {
            try {
                if (layout) {
                    LocalizedLayout(frame, *settings_, wxAtoi(language));
                    result_ = 0;
                    frame->Destroy();
                    ExitMainLoop();
                    return;
                }
                std::puts("PHASE: normal scheme selection/cancel/history");
                const int choose = frame->GetMenuBar()->FindMenuItem("Scheme", "Syntax Scheme...");
                Require(choose != wxNOT_FOUND, "Scheme exposes the original F12 Syntax Scheme command");
                Require(frame->GetMenuBar()->FindMenuItem("Scheme", "Favorite Schemes...") != wxNOT_FOUND,
                        "Scheme exposes the original favorite-management command");
                auto* editor = frame->Editor();
                editor->Send(Scintilla::Message::AddText, 17, reinterpret_cast<intptr_t>("class Example {};"));
                editor->Send(Scintilla::Message::SetSel, 1, 9);
                const auto undo = editor->Send(Scintilla::Message::GetUndoActions);
                Choose(frame, "C/C++ Source", true);
                Require(StatusMetadata(frame).Contains("Plain Text"), "cancel preserves current scheme");
                Choose(frame, "C/C++ Source");
                Require(StatusMetadata(frame).Contains("C/C++") && StatusMetadata(frame).Contains("override"),
                        "native chooser applies current-document override through existing scheme path");
                Require(TestEditor::Text(editor) == "class Example {};" &&
                            editor->Send(Scintilla::Message::GetAnchor) == 1 &&
                            editor->Send(Scintilla::Message::GetCurrentPos) == 9 &&
                            editor->Send(Scintilla::Message::GetUndoActions) == undo,
                        "chooser acceptance/cancel preserve exact text, selection and undo history");
                std::puts("PHASE: default/automatic settings");
                DefaultScheme();
                std::puts("PHASE: favorite checking and drag events");
                Favorites();
                std::puts("PHASE: retained favorite capacity");
                FavoriteCapacity();
                std::puts("PHASE: real pointer favorite management");
                PointerFavorites();
                std::puts("PHASE: native F12/Enter");
                KeyboardChooser();
                std::puts("PASS: native scheme selection and favorite workflows");
                result_ = 0;
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SchemeApp);
