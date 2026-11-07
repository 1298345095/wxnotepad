#include "native_search_helpers.h"
#include "native_paint_helpers.h"
#include <wx/app.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <wx/stattext.h>

namespace {
using Scintilla::Message;
using namespace NativeSearch;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void SelectByMouse(EditorControl* editor, intptr_t start, intptr_t end) {
    const auto x1 = editor->Send(Message::PointXFromPosition, 0, start);
    const auto x2 = editor->Send(Message::PointXFromPosition, 0, end);
    const auto y = editor->Send(Message::PointYFromPosition, 0, start) + editor->Send(Message::TextHeight, 0) / 2;
#ifdef __WXMSW__
    auto handle = static_cast<HWND>(editor->GetHandle());
    SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x1, y));
    SendMessageW(handle, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x2, y));
    SendMessageW(handle, WM_LBUTTONUP, 0, MAKELPARAM(x2, y));
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    auto* device = gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget)));
    for (auto type : {GDK_BUTTON_PRESS, GDK_MOTION_NOTIFY, GDK_BUTTON_RELEASE}) {
        auto* event = gdk_event_new(type);
        event->any.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
        event->any.send_event = TRUE;
        if (type == GDK_MOTION_NOTIFY) {
            event->motion.x = x2;
            event->motion.y = y;
            event->motion.state = GDK_BUTTON1_MASK;
        } else {
            event->button.time = GDK_CURRENT_TIME;
            event->button.x = type == GDK_BUTTON_PRESS ? x1 : x2;
            event->button.y = y;
            event->button.button = 1;
            event->button.state = type == GDK_BUTTON_RELEASE ? GDK_BUTTON1_MASK : 0;
        }
        gdk_event_set_device(event, device);
        gtk_widget_event(widget, event);
        gdk_event_free(event);
    }
#endif
    wxYield();
    Require(editor->Send(Message::GetSelectionStart) == start && editor->Send(Message::GetSelectionEnd) == end,
            "native mouse drag changes the actual editor selection");
}

void Ranges(EditorControl* editor, std::vector<std::pair<intptr_t, intptr_t>> expected, const char* message) {
    std::vector<std::pair<intptr_t, intptr_t>> actual;
    for (intptr_t pos = 0; pos < editor->Send(Message::GetLength); ++pos) {
        if (!editor->Send(Message::IndicatorValueAt, 8, pos)) continue;
        if (!actual.empty() && actual.back().second == pos)
            ++actual.back().second;
        else
            actual.emplace_back(pos, pos + 1);
    }
    Require(actual == expected, message);
}
class OccurrenceApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                auto* menus = frame->GetMenuBar();
                const int off = menus->FindMenuItem("View", "Off");
                Require(off != wxNOT_FOUND, "native Mark Occurrences Off command exists");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!menus->FindItem(off)->IsChecked(), "original default enables occurrence marking");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one ONE stone one\nONE one\n"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSel, 0, 3);
                wxYield();
                for (intptr_t position : {0, 4, 10, 14, 18, 22})
                    Require(editor->Send(Message::IndicatorValueAt, 8, position) != 0,
                            "automatic marking includes case-insensitive substring matches");
                Require(editor->Send(Message::GetAnchor) == 0 && editor->Send(Message::GetCurrentPos) == 3 &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "automatic marking preserves selection, bytes and history");
                Command(frame, off);
                Require(!editor->Send(Message::IndicatorValueAt, 8, 0), "Off clears occurrence indicators");
                std::puts("PASS: native occurrence default, selection matching, history and Off");
                SearchSession search(*editor);
                SearchRequest request;
                request.query = "ONE";
                request.options.matchCase = true;
                request.bookmarkMatchedLines = true;
                search.Configure(request);
                Require(search.FindAll().count == 2, "explicit Find All marks its independent query");
                Command(frame, off);
                Require(!search.MatchCount().has_value() && editor->Send(Message::IndicatorValueAt, 8, 0) &&
                            (editor->Send(Message::MarkerGet, 1) & 1),
                        "automatic marking replaces Find All count and ranges while retaining Find All bookmarks");
                Require(search.Request().query == "ONE" && search.Request().options.matchCase &&
                            search.RecentFind().empty(),
                        "automatic marking preserves explicit find options and history");
                std::puts("PASS: shared occurrence and Find All indicator/bookmark ownership");
                const int matchCase = menus->FindMenuItem("View", "Match Case");
                const int wholeWord = menus->FindMenuItem("View", "Match Whole Word Only");
                const int bookmark = menus->FindMenuItem("View", "Bookmark Matched Line");
                Require(matchCase != wxNOT_FOUND && wholeWord != wxNOT_FOUND && bookmark != wxNOT_FOUND,
                        "native occurrence option commands exist");
                Command(frame, matchCase);
                Ranges(editor, {{0, 3}, {10, 13}, {14, 17}, {22, 25}}, "original case-sensitive substring ranges");
                Command(frame, wholeWord);
                Ranges(editor, {{0, 3}, {14, 17}, {22, 25}}, "original whole-word selection ranges");
                Command(frame, bookmark);
                editor->Send(Message::MarkerAdd, 2, 0);
                editor->Send(Message::SetSel, 8, 11);
                wxYield();
                Require(!editor->Send(Message::IndicatorValueAt, 8, 0) && editor->Send(Message::MarkerNext, 0, 1) == -1,
                        "partial-word selection clears automatic markers including shared manual bookmarks");
                Command(frame, off);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(menus->FindItem(off)->IsChecked() && !menus->FindItem(matchCase)->IsEnabled() &&
                            !menus->FindItem(wholeWord)->IsEnabled() && !menus->FindItem(bookmark)->IsEnabled() &&
                            menus->FindItem(matchCase)->IsChecked() && menus->FindItem(wholeWord)->IsChecked() &&
                            menus->FindItem(bookmark)->IsChecked(),
                        "Off disables controls and retains their checked option bits");
                Command(frame, off);
                Command(frame, matchCase);
                Command(frame, wholeWord);
                Command(frame, bookmark);
                const auto load = [&](const char* text, intptr_t anchor, intptr_t caret) {
                    editor->Send(Message::SetSelectionMode, 0);
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    editor->Send(Message::SetSel, anchor, caret);
                    wxYield();
                };
                load("a  b  c", 1, 3);
                Ranges(editor, {{1, 3}, {4, 6}}, "whitespace is a valid automatic needle");
                load("one\none\n", 0, 4);
                Ranges(editor, {}, "multiline selection clears automatic occurrences");
                load("中文 中文 文", 3, 6);
                Ranges(editor, {{3, 6}, {10, 13}, {14, 17}}, "Unicode selection uses original UTF-8 byte boundaries");
                load("ä Ä ä", 0, 2);
                Ranges(editor, {{0, 2}, {3, 5}, {6, 8}}, "Unicode case folding uses the pinned editor matcher");
                load("one ONE one", 7, 4);
                Ranges(editor, {{0, 3}, {4, 7}, {8, 11}}, "reversed selection retains the same needle");
                load("one two one two", 0, 3);
                editor->Send(Message::AddSelection, 7, 4);
                wxYield();
                Ranges(editor, {}, "multiple selections use original combined selection text");
                load("one\none\n", 0, 7);
                editor->Send(Message::SetSelectionMode, 1);
                wxYield();
                Ranges(editor, {}, "rectangular multiline selection clears marks");
                load("one one", 0, 3);
                editor->Send(Message::InsertText, 3, reinterpret_cast<intptr_t>("one"));
                wxYield();
                Ranges(editor, {}, "edit collapsing the selection clears marks");
                editor->Send(Message::Undo);
                wxYield();
                Ranges(editor, {}, "original undo selection restoration leaves cleared marks until a selection update");
                Require(Text(editor) == "one one" && editor->Send(Message::CanRedo),
                        "mark clearing preserves Undo/Redo");
                load("one one", 0, 0);
                SelectByMouse(editor, 0, 3);
                Ranges(editor, {{0, 3}, {4, 7}}, "native mouse selection refreshes occurrence ranges");
                {
                    const auto x = editor->Send(Message::PointXFromPosition, 0, 4);
                    const auto end = editor->Send(Message::PointXFromPosition, 0, 7);
                    const wxRect area(static_cast<int>(x), 0, static_cast<int>(end - x),
                                      static_cast<int>(editor->Send(Message::TextHeight, 0)));
                    const auto marked = NativePaint::Read(editor, area);
                    Command(frame, off);
                    const auto cleared = NativePaint::Read(editor, area);
                    Require(marked != cleared, "native paint changes at the unselected occurrence when marking is off");
                    Command(frame, off);
                }
                Command(frame, menus->FindMenuItem("Language", "C/C++"));
                Ranges(editor, {{0, 3}, {4, 7}}, "syntax changes preserve occurrence channel 8");
                load("one one\none", 0, 3);
                editor->Send(Message::MarkerAdd, 1, 0);
                Command(frame, menus->FindMenuItem("Edit", "Goto Next"));
                Ranges(editor, {{0, 3}, {4, 7}, {8, 11}},
                       "bookmark navigation skips one original occurrence selection refresh");
                Require(editor->Send(Message::GetSelectionEmpty) && editor->Send(Message::GetCurrentPos) == 8,
                        "bookmark navigation collapses the actual selection while retaining occurrence marks");
                std::string large;
                const auto line = std::string("one") + std::string(1020, ' ') + "\n";
                for (int count = 0; count < 3072; ++count)
                    large += line;
                load(large.c_str(), 0, 3);
                const intptr_t lastNeedle = 3 * 1024 * 1024 - 1024;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!editor->Send(Message::IndicatorValueAt, 8, lastNeedle) &&
                       std::chrono::steady_clock::now() < deadline) {
                    wxYield();
                    wxMilliSleep(1);
                }
                Require(editor->Send(Message::IndicatorValueAt, 8, 0) &&
                            editor->Send(Message::IndicatorValueAt, 8, lastNeedle) &&
                            !editor->Send(Message::IndicatorValueAt, 8, lastNeedle + 3) && Text(editor) == large &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "original incremental marking reaches beyond 1 MiB without changing large-document state");
                Require(frame->New(), "new document succeeds");
                wxYield();
                Ranges(editor, {}, "new document stops and clears occurrence work");
                load("one one", 0, 3);
                Command(frame, wxID_FIND);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* action = wxDynamicCast(wxWindow::FindWindowByName("Find All", frame), wxButton);
                auto* result = wxDynamicCast(wxWindow::FindWindowByName("Find All result", frame), wxStaticText);
                Require(query && action && result, "native Find All exposes its query, action and result");
                query->SetValue("one");
                Click(query->GetParent(), action->GetId());
                Require(result->GetLabel() == "2 matches", "native Find All reports the shared count");
                Command(frame, off);
                Require(result->GetLabel().empty(), "Off refreshes native Find All feedback when shared marks clear");
                Command(frame, off);
                load("123 123\nmanual", 0, 3);
                Command(frame, bookmark);
                editor->Send(Message::MarkerAdd, 1, 0);
                Command(frame, matchCase);
                Require(
                    editor->Send(Message::MarkerGet, 1) & 1,
                    "case-invariant selection retains original cached marking and manual bookmarks across Match Case");
                request.query = "123";
                request.options.matchCase = false;
                search.Configure(request);
                Require(search.FindAll().count == 2, "explicit numeric Find All marks both matches");
                editor->Send(Message::MarkerAdd, 1, 0);
                request.options.matchCase = true;
                search.Configure(request);
                Require(search.FindAll().count == 2 && (editor->Send(Message::MarkerGet, 1) & 1),
                        "unchanged explicit case-invariant marking retains manual bookmarks across search option "
                        "submission");
                std::puts("PASS: original option bits, Unicode/white/multiple/rectangular needles, edits, native "
                          "mouse/paint and large-document lifecycle");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Destroy();
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
wxIMPLEMENT_APP(OccurrenceApp);
