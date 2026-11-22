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
    picker.action = [&](wxDialog& dialog, wxTreeCtrl& tree) {
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
    picker.action = [&](wxDialog& dialog, wxTreeCtrl& tree) {
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
    fallback.action = [&](wxDialog& dialog, wxTreeCtrl&) {
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
    picker.action = [&](wxDialog& dialog, wxTreeCtrl& tree) {
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
        const auto toggle = [&](const wxString& label) {
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
    picker.action = [&](wxDialog& dialog, wxTreeCtrl& tree) {
        NativeTreeInput::Present(dialog);
        Require(NativeDialogs::SelectTreeLabel(tree, "Favorite Schemes"), "native pointer favorites group exists");
        const auto favorites = tree.GetSelection();
        wxTreeItemIdValue cookie;
        auto item = tree.GetFirstChild(favorites, cookie);
        NativeTreeInput::Click(tree, item);
        NativeTreeInput::Wait([&] { return tree.GetChildrenCount(favorites, false) == 1; },
                              "real mouse unchecks and removes exactly one favorite");
        Require(NativeDialogs::SelectTreeLabel(tree, "Go Source"), "native pointer can select a general scheme");
        NativeTreeInput::Click(tree, tree.GetSelection());
        NativeTreeInput::Wait([&] { return tree.GetChildrenCount(favorites, false) == 2; },
                              "real mouse checks and appends a favorite");
        item = tree.GetFirstChild(favorites, cookie);
        item = tree.GetNextChild(favorites, cookie);
        Require(tree.GetItemText(item) == "Go Source", "real pointer appended the requested stable scheme");
        NativeTreeInput::Drag(tree, item, favorites);
        NativeTreeInput::Wait(
            [&] {
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
    cancel.action = [&](wxDialog& dialog, wxTreeCtrl& tree) {
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
    reopened.action = [&](wxDialog& dialog, wxTreeCtrl& tree) {
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
void KeyboardChooser() {
    auto* frame = new EditorFrame;
    frame->SetTitle("wxnotepad scheme F12");
    frame->Show();
    bool presented = false;
    NativeKeys::AfterPresentation(frame, [&](bool ready) { presented = ready; });
    NativeTreeInput::Wait([&] { return presented; }, "F12 test frame is presented before native shortcut");
    wxYield();
    frame->SetTitle("wxnotepad scheme F12");
    Picker picker;
    picker.action = [&](wxDialog& dialog, wxTreeCtrl& tree) {
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
    management.action = [&](wxDialog& dialog, wxTreeCtrl& tree) {
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
void LocalizedLayout(EditorFrame* frame) {
    for (const bool favorite : {false, true}) {
        Picker picker;
        picker.title = wxGetTranslation(favorite ? "Favorite Schemes" : "Select Scheme");
        picker.action = [&](wxDialog& dialog, wxTreeCtrl& tree) {
            auto* selected = wxDynamicCast(wxWindow::FindWindowByName("Default scheme", &dialog), wxCheckBox);
            auto* automatic = wxDynamicCast(wxWindow::FindWindowByName("AutoSelect", &dialog), wxCheckBox);
            auto* ok = dialog.FindWindow(wxID_OK);
            auto* cancel = dialog.FindWindow(wxID_CANCEL);
            Require(selected && automatic && ok && cancel,
                    "translated native scheme dialog has original bottom controls");
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
            NativeSearch::Click(&dialog, wxID_CANCEL);
        };
        const auto caption =
            wxStripMenuCodes(wxGetTranslation(favorite ? "&Favorite Schemes..." : "&Syntax Scheme...\tF12"));
        int command = wxNOT_FOUND;
        for (auto* item : frame->GetMenuBar()->GetMenu(4)->GetMenuItems()) {
            if (item->GetItemLabelText() == caption) command = item->GetId();
        }
        Require(command != wxNOT_FOUND, "translated original Scheme command is visible");
        NativeSearch::Command(frame, command);
        Require(picker.error.empty(), picker.error.utf8_str());
        Require(picker.shown == 1, "translated native chooser opened exactly once");
    }
}
class SchemeApp final : public wxApp {
public:
    bool OnInit() override {
        wxString language;
        const bool layout = wxGetEnv("WXNOTEPAD_SCHEME_LAYOUT_LANGUAGE", &language);
        if (layout) AppLocalization::Initialize(static_cast<uint16_t>(wxAtoi(language)));
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame, layout] {
            try {
                if (layout) {
                    LocalizedLayout(frame);
                    result_ = 0;
                    frame->Destroy();
                    ExitMainLoop();
                    return;
                }
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
                DefaultScheme();
                Favorites();
                PointerFavorites();
                KeyboardChooser();
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SchemeApp);
