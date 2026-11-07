#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "native_search_helpers.h"
#include "native_fold_helpers.h"
#include "native_paint_helpers.h"
#include "settings/settings_store.h"
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace {
using Scintilla::Message;
using namespace NativeSearch;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int BookmarkCommand(EditorFrame* frame, const wxString& label) {
    auto* edit = frame->GetMenuBar()->GetMenu(frame->GetMenuBar()->FindMenu("Edit"));
    auto* bookmarks = edit->FindItem(edit->FindItem("Bookmarks"));
    Require(bookmarks && bookmarks->GetSubMenu(), "Edit exposes the original Bookmarks submenu");
    const int id = bookmarks->GetSubMenu()->FindItem(label);
    Require(id != wxNOT_FOUND, "original bookmark menu command exists");
    return id;
}
void Key(wxWindow* window, int key, bool control = false, bool shift = false, bool alt = false) {
#ifdef __WXMSW__
    WindowsShortcut(window, key == WXK_F2 ? VK_F2 : VK_F6,
                    {{VK_CONTROL, static_cast<BYTE>(control ? 0x80 : 0)},
                     {VK_SHIFT, static_cast<BYTE>(shift ? 0x80 : 0)},
                     {VK_MENU, static_cast<BYTE>(alt ? 0x80 : 0)}});
#elif defined(__WXGTK__)
    auto* widget = window->GetConnectWidget();
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = key == WXK_F2 ? GDK_KEY_F2 : GDK_KEY_F6;
    event->key.state = static_cast<GdkModifierType>((control ? GDK_CONTROL_MASK : 0) | (shift ? GDK_SHIFT_MASK : 0) |
                                                    (alt ? GDK_MOD1_MASK : 0));
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
class BookmarksApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.selectOption = 11;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                const auto toggle = BookmarkCommand(frame, "Toggle");
                const auto next = BookmarkCommand(frame, "Goto Next");
                const auto previous = BookmarkCommand(frame, "Goto Previous");
                const auto clear = BookmarkCommand(frame, "Clear All");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("zero\none\ntwo\nlast"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSel, 0, 6);
                Command(frame, toggle);
                Require(editor->Send(Message::MarkerGet, 1) & 1, "Toggle marks the caret line in shared marker zero");
                Command(frame, toggle);
                Require(!(editor->Send(Message::MarkerGet, 1) & 1), "Toggle removes the same marker");
                Command(frame, toggle);
                editor->Send(Message::GotoPos, 14);
                Command(frame, toggle);
                editor->Send(Message::GotoPos, 6);
                Command(frame, next);
                Selection(editor, 13, 13, "Next skips the current marked line and lands at next line start");
                Command(frame, next);
                Selection(editor, 5, 5, "Next wraps to first bookmark");
                Command(frame, previous);
                Selection(editor, 13, 13, "Previous wraps to last bookmark");
                Command(frame, clear);
                Require(editor->Send(Message::MarkerNext, 0, 1) == -1, "Clear All removes shared bookmarks");
                editor->Send(Message::SetSel, 7, 3);
                Command(frame, next);
                Command(frame, previous);
                Require(editor->Send(Message::GetAnchor) == 7 && editor->Send(Message::GetCurrentPos) == 3,
                        "navigation without bookmarks preserves selection direction");
                Require(Text(editor) == "zero\none\ntwo\nlast" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "bookmark commands preserve bytes, save point and undo history");
                std::puts("PASS: native bookmark menus, caret-line toggle, wrapped navigation, clear and no-op");
                const auto select = BookmarkCommand(frame, "Select All");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("aa\r\nbb\ncc\rdd\nlast"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                for (const auto position : {4, 10, 13}) {
                    editor->Send(Message::GotoPos, position);
                    Command(frame, toggle);
                }
                editor->Send(Message::GotoPos, 8);
                Command(frame, select);
                Require(editor->Send(Message::GetSelections) == 3, "Select All creates one range per bookmark");
                Require(editor->Send(Message::GetSelectionNCaret, 0) == 4 &&
                            editor->Send(Message::GetSelectionNAnchor, 0) == 7 &&
                            editor->Send(Message::GetSelectionNCaret, 1) == 10 &&
                            editor->Send(Message::GetSelectionNAnchor, 1) == 13 &&
                            editor->Send(Message::GetSelectionNCaret, 2) == 13 &&
                            editor->Send(Message::GetSelectionNAnchor, 2) == 17,
                        "Select All selects reverse whole lines including EOL and final line without EOL");
                Require(editor->Send(Message::GetMainSelection) == 0,
                        "equidistant bookmarks choose the earlier line as main selection");
                editor->Send(Message::GotoPos, 14);
                Command(frame, select);
                Require(editor->Send(Message::GetMainSelection) == 2 && editor->Send(Message::GetCurrentPos) == 13,
                        "nearest bookmark selects the original main caret at line start");
                Command(frame, clear);
                Command(frame, select);
                Require(editor->Send(Message::GetSelections) == 3 && editor->Send(Message::GetMainSelection) == 2,
                        "Select All without bookmarks retains existing selections");
                Require(Text(editor) == "aa\r\nbb\ncc\rdd\nlast" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "whole-line selection preserves mixed EOL bytes, save point and undo history");
                std::puts("PASS: reverse whole-line ranges, final line, nearest main selection and earlier tie");
                editor->Send(Message::GotoPos, 5);
                Key(editor, WXK_F2, true);
                Require(editor->Send(Message::MarkerGet, 1) & 1, "native Ctrl+F2 toggles caret-line bookmark");
                editor->Send(Message::GotoPos, 14);
                Key(editor, WXK_F2, true);
                Key(editor, WXK_F2);
                Selection(editor, 4, 4, "native F2 wraps forward");
                Key(editor, WXK_F2, false, true);
                Selection(editor, 13, 13, "native Shift+F2 wraps backward");
                Key(editor, WXK_F6, false, false, true);
                Require(editor->Send(Message::GetSelections) == 2 && editor->Send(Message::GetMainSelection) == 1,
                        "native Alt+F6 selects bookmarks");
                editor->Send(Message::GotoPos, 8);
                Key(frame, WXK_F6, true, false, true);
                Require(editor->Send(Message::GetSelections) == 1 && editor->Send(Message::GetCurrentPos) == 8,
                        "native Ctrl+Alt+F6 remains distinct from Alt+F6 bookmark selection");
                Key(frame, WXK_F6, false, true, true);
                Require(editor->Send(Message::GetSelections) == 1 && editor->Send(Message::GetCurrentPos) == 8,
                        "native Alt+Shift+F6 remains distinct from Alt+F6 bookmark selection");
                editor->Send(Message::GotoPos, 14);
                Key(editor, WXK_F6, false, false, true);
                Key(frame, WXK_F2, true, true);
                Key(frame, WXK_F2, false, true, true);
                Require(editor->Send(Message::GetSelections) == 2 && (editor->Send(Message::MarkerGet, 4) & 1),
                        "extra Shift distinguishes Ctrl+Shift+F2 and Alt+Shift+F2 from bookmark actions");
                Key(editor, WXK_F2, false, false, true);
                Require(editor->Send(Message::MarkerNext, 0, 1) == -1, "native Alt+F2 clears bookmarks");
                std::puts("PASS: exact native F2/Ctrl+F2/Shift+F2/Alt+F2/Alt+F6 shortcuts and modifier exclusions");
                editor->Send(Message::GotoPos, 4);
                Command(frame, toggle);
                Command(frame, next);
                Selection(editor, 4, 4, "one bookmark wraps back to itself");
                editor->Send(Message::MarkerAdd, 2, 1);
                editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("prefix\n"));
                Require((editor->Send(Message::MarkerGet, 2) & 1) && !(editor->Send(Message::MarkerGet, 1) & 1),
                        "document insertion moves the shared bookmark through the original core");
                editor->Send(Message::Undo);
                Require((editor->Send(Message::MarkerGet, 1) & 1) && editor->Send(Message::CanRedo),
                        "Undo restores marker line and offers the real edit for Redo");
                const auto revision = editor->DocumentRevision();
                Command(frame, next);
                Command(frame, select);
                Command(frame, toggle);
                Command(frame, clear);
                Require(editor->Send(Message::MarkerGet, 2) & 2, "Clear All preserves other marker numbers");
                Require(editor->DocumentRevision() == revision && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                        "marker and selection commands preserve document revision, save point and pending Redo");
                editor->Send(Message::Redo);
                Require(Text(editor) == "prefix\naa\r\nbb\ncc\rdd\nlast",
                        "bookmark commands preserve the real redo edit");
                editor->Send(Message::Undo);
                std::puts("PASS: marker motion on edit/Undo, other markers and retained Redo action");

                editor->Send(Message::SetText, 0,
                             reinterpret_cast<intptr_t>("void f() {\n  hit();\n}\nvoid g() {\n  other();\n}\n"));
                Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                NativeFold::Paint(editor);
                editor->Send(Message::GotoPos, 14);
                Command(frame, toggle);
                editor->Send(Message::GotoPos, 0);
                editor->Send(Message::FoldAll, static_cast<uintptr_t>(Scintilla::FoldAction::Contract));
                Require(!editor->Send(Message::GetLineVisible, 1) && !editor->Send(Message::GetLineVisible, 4),
                        "fold fixture hides bookmarked and unrelated contents");
                Command(frame, next);
                Selection(editor, 11, 11, "bookmark navigation lands at folded logical line start");
                Require(editor->Send(Message::GetLineVisible, 1) && !editor->Send(Message::GetLineVisible, 4),
                        "bookmark navigation unfolds the destination and retains unrelated collapsed folds");
                std::puts("PASS: bookmark navigation reveals folded destination and retains unrelated folds");

                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("hit\nplain\nhit\nlast"));
                editor->Send(Message::MarkerDeleteAll, 0);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 5);
                Command(frame, toggle);
                NativePaint::Complete(editor);
                Command(frame, wxID_FIND);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* findAll = wxDynamicCast(wxWindow::FindWindowByName("Find All", frame), wxButton);
                auto* bookmark = wxDynamicCast(wxWindow::FindWindowByName("Bookmark matched line", frame), wxCheckBox);
                auto* result = wxDynamicCast(wxWindow::FindWindowByName("Find All result", frame), wxStaticText);
                Require(query && findAll && bookmark && result, "Find exposes bookmark controls and count");
                query->SetValue("hit");
                Toggle(bookmark, true);
                Click(query->GetParent(), findAll->GetId());
                Require((editor->Send(Message::MarkerGet, 0) & 1) && (editor->Send(Message::MarkerGet, 1) & 1) &&
                            (editor->Send(Message::MarkerGet, 2) & 1),
                        "first bookmarked Find All shares the existing manual marker set");
                Command(frame, clear);
                Require(editor->Send(Message::MarkerNext, 0, 1) == -1 &&
                            editor->Send(Message::IndicatorValueAt, 8, 0) && result->GetLabel() == "2 matches",
                        "Clear All removes Find All bookmarks while retaining indicators and count");
                editor->Send(Message::GotoPos, 14);
                Command(frame, toggle);
                NativePaint::Complete(editor);
                Click(query->GetParent(), findAll->GetId());
                Require((editor->Send(Message::MarkerGet, 0) & 1) && (editor->Send(Message::MarkerGet, 2) & 1) &&
                            !(editor->Send(Message::MarkerGet, 3) & 1),
                        "next bookmarked Find All replaces the shared set including manual additions");
                editor->Send(Message::GotoPos, 0);
                NativePaint::Complete(editor);
                Require(!editor->Send(Message::IndicatorValueAt, 8, 0) &&
                            !editor->Send(Message::IndicatorValueAt, 8, 10) &&
                            (editor->Send(Message::MarkerGet, 0) & 1) && (editor->Send(Message::MarkerGet, 2) & 1) &&
                            result->GetLabel().empty(),
                        "original empty-caret update clears Find All indicators/count and retains its bookmarks");
                Click(query->GetParent(), findAll->GetId());
                Command(frame, toggle);
                Require(!(editor->Send(Message::MarkerGet, 0) & 1) && editor->Send(Message::IndicatorValueAt, 8, 0),
                        "Toggle removes a Find All bookmark independently of its occurrence indicator");
                editor->Send(Message::GotoPos, 0);
                Key(query, WXK_F2, true, false, true);
                NativePaint::Complete(editor);
                Require(editor->Send(Message::GetAnchor) == 0 && editor->Send(Message::GetCurrentPos) == 3 &&
                            (editor->Send(Message::MarkerGet, 2) & 1),
                        "Ctrl+Alt+F2 in Find extends search selection without toggling or clearing bookmarks");
                editor->Send(Message::GotoPos, 18);
                Key(query, WXK_F2, true, true, true);
                NativePaint::Complete(editor);
                Require(editor->Send(Message::GetAnchor) == 18 && editor->Send(Message::GetCurrentPos) == 10 &&
                            (editor->Send(Message::MarkerGet, 2) & 1),
                        "Ctrl+Alt+Shift+F2 retains backward search-selection semantics");
                Click(query->GetParent(), findAll->GetId());
                Require((editor->Send(Message::MarkerGet, 0) & 1) && (editor->Send(Message::MarkerGet, 2) & 1) &&
                            result->GetLabel() == "2 matches",
                        "Find All restores explicit bookmark ownership after automatic selection marking");
                Toggle(bookmark, false);
                Click(query->GetParent(), findAll->GetId());
                Require(editor->Send(Message::MarkerNext, 0, 1) == -1,
                        "Find All with bookmarking disabled clears the prior shared Find All set");
                editor->Send(Message::GotoPos, 5);
                Command(frame, toggle);
                Click(query->GetParent(), wxID_CANCEL);
                Require(frame->New(), "clean New replaces the current document");
                NativePaint::Complete(editor);
                Require(editor->Send(Message::MarkerNext, 0, 1) == -1 && result->GetLabel().empty(),
                        "document replacement clears shared manual bookmarks and Find All state");
                std::puts(
                    "PASS: Find All shared markers, indicators/count, replacement and distinct search-selection keys");
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
    int OnExit() override { return result_; }

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(BookmarksApp);
