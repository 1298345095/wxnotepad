#include "diagnostics/longline_probe.h"
#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "steady_stopwatch.h"
#include <wx/filename.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/weakref.h>
#include <array>
#include <cstdio>
#include <print>
#include <stdexcept>

namespace {
using enum Scintilla::Message;
constexpr int InitialRegexAndBackslashFlags = 8294;

void ReportStage(const char* stage) {
    LonglineProbe::Stage(stage);
    static const TestClock::StopWatch elapsed;
    std::println(stderr, "Search continuation elapsed_ms={} stage={}", elapsed.Time(), stage);
    std::fflush(stderr);
}

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

#ifdef __WXGTK__
class PendingNativeActivity final : public wxEvtHandler, public wxEventFilter {
public:
    explicit PendingNativeActivity(EditorControl* editor) : editor_(editor) {
        // The X11 focus preparation uses finite dispatch. Wayland retains its
        // existing compositor-transition drain and the full original scenario.
        if (!GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(GTK_WIDGET(editor->GetHandle())))) {
            source_ = g_idle_add(+[](gpointer) -> gboolean { return G_SOURCE_CONTINUE; }, nullptr);
            Require(source_ != 0, "keep real native activity queued during the selection-limit shortcut");
            LonglineProbe::Event(LonglineProbe::Kind::NativeIdleAdded, 0, source_);
        }
        wxEvtHandler::AddFilter(this);
    }
    ~PendingNativeActivity() override {
        wxEvtHandler::RemoveFilter(this);
        if (source_) g_source_remove(source_);
    }
    int FilterEvent(wxEvent& event) override {
        // Only the real shortcut reaching this editor ends the queued work.
        // A focus acknowledgement or request publication cannot satisfy it.
        if (source_ && event.GetEventType() == wxEVT_CHAR_HOOK && event.GetEventObject() == editor_ &&
            static_cast<wxKeyEvent&>(event).GetKeyCode() == WXK_F3) {
            g_source_remove(source_);
            LonglineProbe::Event(LonglineProbe::Kind::NativeIdleRemoved, 0, source_);
            source_ = 0;
            observedPress_ = true;
        }
        if (observedPress_ && !observedRelease_ && event.GetEventType() == wxEVT_KEY_UP &&
            event.GetEventObject() == editor_ && static_cast<wxKeyEvent&>(event).GetKeyCode() == WXK_F3) {
            observedRelease_ = true;
            // wxYield services queued wx callbacks after its native drain.
            // Start this work there, after the key-up drain has returned, so
            // only the shortcut's final dispatch sees the new ready source.
            CallAfter([this] {
                source_ = g_idle_add(+[](gpointer) -> gboolean { return G_SOURCE_CONTINUE; }, nullptr);
                Require(source_ != 0, "keep real native activity queued after the actual shortcut release");
                LonglineProbe::Event(LonglineProbe::Kind::NativeIdleAdded, 0, source_);
            });
        }
        return Event_Skip;
    }

private:
    EditorControl* editor_;
    guint source_ = 0;
    bool observedPress_ = false;
    bool observedRelease_ = false;
};
#endif

void RequireSelection(EditorControl* editor, intptr_t anchor, intptr_t caret, const char* message) {
    const auto actualAnchor = editor->Send(GetAnchor);
    const auto actualCaret = editor->Send(GetCurrentPos);
    if (actualAnchor != anchor || actualCaret != caret) {
        std::println(stderr, "Anchor/caret: {:d}/{:d}, expected {:d}/{:d}", static_cast<long long>(actualAnchor),
                     static_cast<long long>(actualCaret), static_cast<long long>(anchor),
                     static_cast<long long>(caret));
        throw std::runtime_error(message);
    }
}

void SaveWord(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto searchMenu = frame->GetMenuBar()->FindMenu("Search");
    Require(searchMenu != wxNOT_FOUND, "the original Search menu is available");
    auto* menuItems = frame->GetMenuBar()->GetMenu(static_cast<size_t>(searchMenu));
    Require(menuItems->FindItemByPosition(1)->GetItemLabel() == "Sa&ve Find Text\tAlt+F3" &&
                menuItems->FindItemByPosition(5)->GetItemLabel() == "Repl&ace Next\tF4" &&
                menuItems->FindItemByPosition(6)->IsSeparator(),
            "Save Find Text and Replace Next occupy the pinned original Search positions");
    TestEditor::ResetStream(editor, "  dog cat dog", 1, 1);
    NativeKeys::Shortcut(frame, WXK_F3, false, true);
    RequireSelection(editor, 2, 5, "Alt+F3 must select the word without advancing");
    NativeKeys::Shortcut(frame, WXK_F3, false);
    RequireSelection(editor, 10, 13, "F3 must use the saved word");
    TestEditor::ResetStream(editor, "  ++  \n", 3, 3);
    NativeKeys::Shortcut(frame, WXK_F3, false, true, true);
    RequireSelection(editor, 3, 3, "Alt+Shift+F3 must not invoke Save Find Text or Find Previous");
    const auto menu = frame->GetMenuBar()->FindMenuItem("Search", "Save Find Text");
    NativeSearch::Command(frame, menu);
    RequireSelection(editor, 2, 6, "the Search menu uses the original indented line fallback");
}

wxTextCtrl* FindQuery(EditorFrame* frame) {
    return wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
}

bool FindVisible(EditorFrame* frame) {
    auto* query = FindQuery(frame);
    return query && query->GetParent()->IsShown();
}

void SelectionSearch(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "dog cat dog", 1, 1);
    NativeKeys::Shortcut(frame, WXK_F3, true);
    RequireSelection(editor, 8, 11, "Ctrl+F3 selects the current word before seeking its next match");
    editor->Send(SetSel, 11, 8);
    NativeKeys::Shortcut(frame, WXK_F3, true, false, true);
    RequireSelection(editor, 0, 3, "Ctrl+Shift+F3 seeks backward from a reversed selection");
    TestEditor::ResetStream(editor, "猫😀 x 猫😀", 0, 7);
    NativeKeys::Shortcut(frame, WXK_F3, true);
    RequireSelection(editor, 10, 17, "selection search must keep Unicode byte boundaries");
    TestEditor::ResetStream(editor, "a\r\nb x a\r\nb", 0, 4);
    NativeKeys::Shortcut(frame, WXK_F3, true);
    RequireSelection(editor, 7, 11, "selection search uses the raw CRLF selection");
    Require(!FindVisible(frame) && !editor->Send(CanUndo), "selection search neither opens Find nor edits text");
}

void ImplicitNext(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "a[.] x a[.]", 0, 4);
    NativeKeys::Shortcut(frame, WXK_F3, false);
    RequireSelection(editor, 7, 11, "first F3 seeds the eligible selection and clears regex and transform");
    Require(!FindVisible(frame), "first F3 with an eligible selection does not open Find");
}

void ImplicitPrevious(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "dog cat dog", 8, 11);
    NativeKeys::Shortcut(frame, WXK_F3, false, false, true);
    RequireSelection(editor, 0, 3, "first Shift+F3 uses the selection in the backward direction");
    Require(!FindVisible(frame), "first Shift+F3 with an eligible selection does not open Find");
}

void DisabledSeed(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "dog cat dog", 0, 3);
    NativeKeys::Shortcut(frame, WXK_F3, false);
    Require(FindVisible(frame) && FindQuery(frame)->IsEmpty(), "first F3 with selection seeding off opens empty Find");
    RequireSelection(editor, 0, 3, "disabled implicit seeding preserves the selection");
    NativeSearch::Click(FindQuery(frame)->GetParent(), wxID_CANCEL);
    NativeKeys::Shortcut(frame, WXK_F3, true);
    RequireSelection(editor, 8, 11, "explicit selection search ignores the implicit seed preference");
}

void EmptySeed(EditorFrame* frame) {
    auto* editor = frame->Editor();
    NativeKeys::Shortcut(frame, WXK_F3, false);
    NativeKeys::Shortcut(frame, WXK_F3, true, false, true);
    NativeKeys::Shortcut(frame, WXK_F4, false);
    Require(!FindVisible(frame), "ordinary and explicit selection search are inert in an empty document");
    TestEditor::ResetStream(editor, "dog cat dog", 1, 1);
    NativeKeys::Shortcut(frame, WXK_F3, false);
    Require(FindVisible(frame) && FindQuery(frame)->IsEmpty(), "first F3 without a selection opens empty Find");
    RequireSelection(editor, 1, 1, "implicit seeding does not select the current word");
}

void SelectionLimit(EditorFrame* frame) {
    const LonglineProbe::Scenario observation;
    constexpr intptr_t OneMiB = 1024 * 1024;
    const std::string text(OneMiB, 'a');
    auto* editor = frame->Editor();
    ReportStage("selection limit ResetStream begin");
    TestEditor::ResetStream(editor, text.c_str(), 0, OneMiB);
    editor->Send(SetCaretPeriod, 50);
    ReportStage("selection limit ResetStream complete; save begin");
    {
#ifdef __WXGTK__
        const PendingNativeActivity activity(editor);
#endif
        NativeKeys::Shortcut(frame, WXK_F3, false, true);
    }
    ReportStage("selection limit save complete; Find begin");
    NativeKeys::Shortcut(frame, WXK_F3, false);
    ReportStage("selection limit Find complete; Cancel begin");
    Require(FindVisible(frame) && FindQuery(frame)->IsEmpty(), "explicit save rejects a selection at the byte limit");
    NativeSearch::Click(FindQuery(frame)->GetParent(), wxID_CANCEL);
    ReportStage("selection limit Cancel complete; near-limit SetSel begin");
    editor->Send(SetSel, 0, OneMiB - 1);
    ReportStage("near-limit SetSel complete; save begin");
    NativeKeys::Shortcut(frame, WXK_F3, false, true);
    ReportStage("near-limit save complete; Find begin");
    NativeKeys::Shortcut(frame, WXK_F3, false);
    ReportStage("near-limit Find complete; validation begin");
    Require(!FindVisible(frame), "explicit save accepts a selection one byte below the limit");
    RequireSelection(editor, 0, OneMiB - 1, "the saved near-limit query selects the exact original range");
    ReportStage("near-limit validation complete");
}

wxTextCtrl* Replacement(EditorFrame* frame) {
    return wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
}

void UninitializedReplacement(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "dog cat dog", 0, 0);
    NativeKeys::Shortcut(frame, WXK_F4, false);
    Require(FindVisible(frame) && Replacement(frame) && Replacement(frame)->IsShown(),
            "F4 opens Replace before the first replacement submission");
    Require(FindQuery(frame)->GetValue() == "dog" && Replacement(frame)->GetValue() == "fox",
            "the opened Replace may display uninitialized query and replacement history");
    RequireSelection(editor, 0, 0, "uninitialized F4 does not select or edit a match from history");
    NativeSearch::Click(FindQuery(frame)->GetParent(), wxID_CANCEL);
    NativeSearch::Command(frame, wxID_FIND);
    NativeSearch::Click(FindQuery(frame)->GetParent(), wxID_FORWARD);
    NativeSearch::Click(FindQuery(frame)->GetParent(), wxID_CANCEL);
    NativeKeys::Shortcut(frame, WXK_F4, false);
    Require(FindVisible(frame) && Replacement(frame)->IsShown() && TestEditor::Text(editor) == "dog cat dog",
            "submitting only Find does not initialize the replacement shown from history");
}

void DialogShortcut(wxWindow* field, int key, int modifiers = wxMOD_NONE) {
#ifdef __WXMSW__
    const int virtualKey = key == WXK_F3 ? VK_F3 : VK_F4;
    const auto state = [modifiers](int flag) { return static_cast<BYTE>(modifiers & flag ? 0x80 : 0); };
    const bool handled = NativeSearch::WindowsShortcut(
        field, virtualKey,
        {{VK_CONTROL, state(wxMOD_CONTROL)}, {VK_MENU, state(wxMOD_ALT)}, {VK_SHIFT, state(wxMOD_SHIFT)}});
    Require(handled, "the owned native dialog translates the search shortcut");
#elif defined(__WXGTK__)
    auto* widget = field->GetConnectWidget();
    auto* window = gtk_widget_get_window(widget);
    Require(window, "native Find/Replace text control must own a presented GTK window");
    auto* keyPress = gdk_event_new(GDK_KEY_PRESS);
    keyPress->key.window = GDK_WINDOW(g_object_ref(window));
    keyPress->key.send_event = TRUE;
    keyPress->key.keyval = key == WXK_F3 ? GDK_KEY_F3 : GDK_KEY_F4;
    keyPress->key.state = (modifiers & wxMOD_CONTROL ? GDK_CONTROL_MASK : 0) |
                          (modifiers & wxMOD_SHIFT ? GDK_SHIFT_MASK : 0) | (modifiers & wxMOD_ALT ? GDK_MOD1_MASK : 0);
    gtk_widget_event(widget, keyPress);
    gdk_event_free(keyPress);
#endif
    wxYield();
}

wxCheckBox* SearchOption(EditorFrame* frame, const wxString& name) {
    auto* checkbox = wxDynamicCast(wxWindow::FindWindowByName(name, frame), wxCheckBox);
    Require(checkbox, "native Find/Replace option exists");
    return checkbox;
}

void DialogSelectionCommands(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "dog cat DOG dog", 0, 3);
    NativeSearch::Command(frame, wxID_FIND);
    auto* query = FindQuery(frame);
    Require(query && query->IsShown(), "Find dialog presents its query field");
    auto* matchCase = SearchOption(frame, "Match case");
    auto* wholeWord = SearchOption(frame, "Whole word");
    auto* regex = SearchOption(frame, "Regular expression");
    auto* transform = SearchOption(frame, "Transform backslashes");
    NativeSearch::Toggle(matchCase, true);
    NativeSearch::Toggle(wholeWord, true);
    NativeSearch::Toggle(regex, true);
    NativeSearch::Click(query->GetParent(), wxID_FORWARD);
    RequireSelection(editor, 12, 15, "submitted case-sensitive regex skips the uppercase match");
    editor->Send(SetSel, 0, 3);
    query->SetValue("draft");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Search", "Save Find Text"));
    Require(query->GetValue() == "draft" && regex->GetValue(), "frame Save leaves the open dialog draft alone");
    DialogShortcut(query, WXK_F3, wxMOD_ALT);
    Require(query->GetValue() == "dog" && matchCase->GetValue() && wholeWord->GetValue() && !regex->GetValue() &&
                !transform->GetValue(),
            "dialog Alt+F3 recalls the raw query, clears regex and transform and preserves case and whole word");
    RequireSelection(editor, 0, 3, "Alt+F3 in Find only saves the query");
    query->SetValue("draft");
    DialogShortcut(query, WXK_F4);
    Require(FindVisible(frame) && !Replacement(frame)->IsShown(), "F4 does nothing in Find mode");
    DialogShortcut(query, WXK_F3, wxMOD_CONTROL);
    RequireSelection(editor, 12, 15, "Ctrl+F3 in Find searches its editor selection with saved flags");
    DialogShortcut(query, WXK_F3, wxMOD_CONTROL | wxMOD_SHIFT);
    RequireSelection(editor, 0, 3, "Ctrl+Shift+F3 in Find searches backward using its editor selection");
    Require(query->GetValue() == "draft", "selection shortcuts do not submit or replace the dialog draft");
}

void SavedSelectionHistory(const OriginalSettings& saved) {
    Require(saved.recentFind == std::vector<wxString>{"dog"} && saved.recentReplace.empty() &&
                saved.findReplaceFlag == 6,
            "Save/selection shortcuts preserve submitted history and persist the original case and whole-word flags");
}

void InitializedEmptyReplacement(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "dog cat dog", 0, 0);
    NativeSearch::Command(frame, wxID_REPLACE);
    auto* query = FindQuery(frame);
    Require(query && query->IsShown() && Replacement(frame)->IsEmpty(), "Replace begins with an empty value");
    query->SetValue("dog");
    NativeSearch::Click(query->GetParent(), wxID_FORWARD);
    RequireSelection(editor, 0, 3, "Find in Replace selects the first match and submits the empty replacement");
    NativeSearch::Click(query->GetParent(), wxID_REPLACE);
    Require(TestEditor::Text(editor) == "dog cat dog" && !editor->Send(CanUndo),
            "the original fresh default empty Replace button advances without editing bytes");
    RequireSelection(editor, 8, 11, "the Replace button continues from the current match end");
    DialogShortcut(query, WXK_F4);
    RequireSelection(editor, 0, 3, "dialog F4 shares the empty Replace behavior and wraps");
    NativeSearch::Click(query->GetParent(), wxID_CANCEL);
    NativeKeys::Shortcut(frame, WXK_F4, false, false, true);
    RequireSelection(editor, 0, 3, "Shift+F4 must not fall through to Replace Next");
    NativeKeys::Shortcut(frame, WXK_F4, false);
    Require(!FindVisible(frame), "initialized F4 continues with an intentionally empty replacement");
    RequireSelection(editor, 8, 11, "frame F4 advances to the second match without deleting it");
    Require(TestEditor::Text(editor) == "dog cat dog" && !editor->Send(CanUndo),
            "none of the fresh empty Replace routes creates an edit");
}

void SavedEmptyHistory(const OriginalSettings& saved) {
    constexpr int LastReplaceEmptyFlag = 32 << 10;
    Require(saved.recentFind == std::vector<wxString>{"dog"} && saved.recentReplace.empty() &&
                saved.findReplaceFlag == LastReplaceEmptyFlag,
            "initialized empty replacement saves the original flag without adding a replacement history entry");
}

void ReplacementFeatures(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "dog cat dog", 0, 0);
    NativeSearch::Command(frame, wxID_REPLACE);
    auto* query = FindQuery(frame);
    Require(query && query->IsShown(), "native Replace exposes its query");
    query->SetValue("(dog)");
    Replacement(frame)->SetValue("$1s");
    NativeSearch::Toggle(SearchOption(frame, "Regular expression"), true);
    NativeSearch::Click(query->GetParent(), wxID_FORWARD);
    NativeSearch::Click(query->GetParent(), wxID_CANCEL);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Search", "Replace Next"));
    Require(TestEditor::Text(editor) == "dogs cat dog", "Replace Next expands the original captured group");
    RequireSelection(editor, 9, 12, "Replace Next selects the next match after the expanded insertion");
    NativeSearch::Command(frame, wxID_UNDO);
    Require(TestEditor::Text(editor) == "dog cat dog" && !editor->Send(CanUndo) && editor->Send(CanRedo),
            "a native Undo restores the original document after one F4 edit");
    RequireSelection(editor, 3, 3, "Undo restores the original endpoint with selection history disabled");
    NativeSearch::Command(frame, wxID_REDO);
    Require(TestEditor::Text(editor) == "dogs cat dog", "a native Redo restores the capture expansion");
    RequireSelection(editor, 4, 4, "Redo restores the original expanded endpoint");

    TestEditor::ResetStream(editor, "dog cat dog", 0, 0);
    NativeSearch::Command(frame, wxID_REPLACE);
    query->SetValue("(?=dog)");
    Replacement(frame)->SetValue("!");
    NativeSearch::Click(query->GetParent(), wxID_FORWARD);
    RequireSelection(editor, 0, 0, "the C++ regex engine finds a zero-width match at the caret");
    NativeSearch::Click(query->GetParent(), wxID_CANCEL);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Search", "Replace Next"));
    Require(TestEditor::Text(editor) == "!dog cat dog", "Replace Next inserts once at the original zero-width match");
    RequireSelection(editor, 1, 1, "zero-width Replace Next leaves the caret at the end of the insertion");
    NativeSearch::Command(frame, wxID_UNDO);
    Require(TestEditor::Text(editor) == "dog cat dog" && !editor->Send(CanUndo) && editor->Send(CanRedo),
            "a single native Undo reverses the zero-width insertion");
    NativeSearch::Command(frame, wxID_REDO);
    Require(TestEditor::Text(editor) == "!dog cat dog", "native Redo restores the zero-width insertion");

    TestEditor::ResetStream(editor, "dog cat dog", 0, 0);
    NativeSearch::Command(frame, wxID_REPLACE);
    query->SetValue("dog");
    Replacement(frame)->SetValue("fox");
    NativeSearch::Toggle(SearchOption(frame, "Regular expression"), false);
    NativeSearch::Click(query->GetParent(), wxID_FORWARD);
    NativeSearch::Click(query->GetParent(), wxID_CANCEL);
    editor->Send(SetReadOnly, 1);
    NativeKeys::Shortcut(frame, WXK_F4, false);
    Require(TestEditor::Text(editor) == "dog cat dog" && editor->Send(GetReadOnly) && !editor->Send(CanUndo),
            "F4 cannot edit a protected document or create an Undo action");
    RequireSelection(editor, 0, 3, "protected replacement keeps the original match selected");
}

void DialogWildcardAndTransform(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "a\r\nb x a\r\nb", 0, 4);
    NativeSearch::Command(frame, wxID_FIND);
    auto* query = FindQuery(frame);
    auto* transform = SearchOption(frame, "Transform backslashes");
    Require(query && transform->GetValue(), "initial Find seeds the multiline query with transform enabled");
    DialogShortcut(query, WXK_F3, wxMOD_ALT);
    Require(!transform->GetValue(), "dialog Alt+F3 clears transform before searching the raw selection");
    NativeSearch::Command(frame, wxID_FORWARD);
    RequireSelection(editor, 7, 11, "after saving, ordinary Next finds the second raw multiline selection");

    TestEditor::ResetStream(editor, "d*g x dog d*g", 0, 3);
    query->SetValue("d*g");
    auto* wildcard = SearchOption(frame, "Wildcard search");
    NativeSearch::Toggle(wildcard, true);
    NativeSearch::Click(query->GetParent(), wxID_FORWARD);
    editor->Send(SetSel, 0, 3);
    DialogShortcut(query, WXK_F3, wxMOD_ALT);
    Require(wildcard->GetValue(), "the original Save Find Text leaves wildcard mode enabled");
    NativeSearch::Command(frame, wxID_FORWARD);
    RequireSelection(editor, 6, 13, "saved text retains original wildcard matching through ordinary Next");
}

void SavedWildcardHistory(const OriginalSettings& saved) {
    constexpr int WildcardFlag = 16 << 10;
    Require(saved.recentFind == std::vector<wxString>{"d*g"} && saved.findReplaceFlag == WildcardFlag,
            "Save Find Text retains the submitted wildcard history and original persisted flag");
}

class ReviewFeedback final : public wxModalDialogHook {
public:
    ReviewFeedback() { Register(); }
    int response = wxID_CANCEL;
    std::vector<wxString> messages;
    bool released = true;
    bool responseBeforeRelease = false;
    std::chrono::steady_clock::time_point releaseDeadline;

    void Shortcut(EditorFrame* frame, bool control) {
#ifndef __WXMSW__
        released = false;
#endif
        NativeKeys::Shortcut(frame, WXK_F3, control, false, false, &released, "wxnotepad", &releaseDeadline);
    }

    int Enter(wxDialog* dialog) override {
        if (dialog->GetName() != "Search feedback") return wxID_NONE;
        for (auto* child : dialog->GetChildren()) {
            if (auto* label = wxDynamicCast(child, wxStaticText)) messages.push_back(label->GetLabel());
        }
        wxTheApp->CallAfter([this, dialog] {
            // Closing the modal while F3 is still held can leave Wayland with
            // no keyboard target for its release. Share the caller's deadline.
            while (!released && std::chrono::steady_clock::now() < releaseDeadline) {
                wxYield();
                wxMilliSleep(10);
            }
            responseBeforeRelease |= !released;
            NativeSearch::Click(dialog, response);
        });
        return wxID_NONE;
    }
};

void FeedbackTransitions(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "dog cat dog", 11, 8);
    ReviewFeedback feedback;
    feedback.Shortcut(frame, true);
    RequireSelection(editor, 11, 8, "cancelling selection wrap preserves the reversed original range");
    feedback.response = wxID_OK;
    feedback.Shortcut(frame, true);
    RequireSelection(editor, 0, 3, "accepting selection wrap selects the first original match");
    TestEditor::ResetStream(editor, "cat x", 0, 0);
    feedback.Shortcut(frame, false);
    RequireSelection(editor, 0, 0, "a missing saved query preserves the untouched current range");
    const wxString wrap = "Reached the end of the document, restarting search at the beginning.";
    Require(feedback.messages == std::vector<wxString>{wrap, wrap, "The specified text was not found."},
            "selection continuation shows the original wrap and failure feedback in order");
    Require(!feedback.responseBeforeRelease, "feedback responses must follow the opening shortcut's native release");
}

struct Scenario {
    void (*run)(EditorFrame*);
    int selectionOptions = SelectOption_CopySelectionAsFindText;
    int findFlags = 0;
    bool withHistory = false;
    void (*inspectSettings)(const OriginalSettings&) = nullptr;
    bool acceptFeedback = true;
};

constexpr std::array scenarios{
    Scenario{SaveWord},
    Scenario{SelectionSearch},
    Scenario{ImplicitNext, SelectOption_CopySelectionAsFindText, InitialRegexAndBackslashFlags},
    Scenario{ImplicitPrevious},
    Scenario{DisabledSeed, 0},
    Scenario{EmptySeed},
    Scenario{SelectionLimit, 0},
    Scenario{UninitializedReplacement, SelectOption_CopySelectionAsFindText, 0, true},
    Scenario{DialogSelectionCommands, SelectOption_CopySelectionAsFindText, 0, false, SavedSelectionHistory},
    Scenario{InitializedEmptyReplacement, SelectOption_CopySelectionAsFindText, 0, false, SavedEmptyHistory},
    Scenario{ReplacementFeatures},
    Scenario{DialogWildcardAndTransform, SelectOption_CopySelectionAsFindText, 0, false, SavedWildcardHistory},
    Scenario{FeedbackTransitions, SelectOption_CopySelectionAsFindText, 0, false, nullptr, false}};

class ContinuationApp final : public wxApp {
    int result_ = 1;
    size_t next_ = 0;
    std::unique_ptr<SettingsStore> settings_;
    wxWeakRef<EditorFrame> previousFrame_;

    void Next() {
        if (previousFrame_) {
            std::fputs("FAIL: the previous search window must be destroyed before replacing its settings\n", stderr);
            ExitMainLoop();
            return;
        }
        if (next_ == scenarios.size()) {
            result_ = 0;
            ExitMainLoop();
            return;
        }
        const auto scenario = scenarios[next_++];
        const auto path =
            scenario.inspectSettings ? wxFileName::CreateTempFileName("wxnotepad-search-continuation") : wxString{};
        settings_ = std::make_unique<SettingsStore>(path);
        settings_->values.selectOption = scenario.selectionOptions;
        settings_->values.findReplaceFlag = scenario.findFlags;
        settings_->values.wordWrap = false;
        settings_->values.saveFindReplace = scenario.inspectSettings != nullptr;
        if (scenario.withHistory) {
            settings_->values.recentFind = {"dog"};
            settings_->values.recentReplace = {"fox"};
        }
        auto* frame = new EditorFrame(settings_.get());
        previousFrame_ = frame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame, scenario, path](bool ready) {
            try {
                Require(ready, "private compositor must present the owned search fixture");
                std::optional<NativeSearch::AcceptFeedback> feedback;
                if (scenario.acceptFeedback) feedback.emplace();
                std::println(stderr, "Search continuation scenario begin: {}/{}", next_, scenarios.size());
                std::fflush(stderr);
                scenario.run(frame);
                std::println(stderr, "Search continuation scenario complete: {}/{}", next_, scenarios.size());
                std::fflush(stderr);
                if (scenario.inspectSettings) {
                    frame->Editor()->Send(SetSavePoint);
                    Require(frame->Close(), "closing the owned frame saves its search settings");
                    frame->Hide();
                    const SettingsStore reloaded(path);
                    scenario.inspectSettings(reloaded.values);
                    Require(wxRemoveFile(path), "the owned search settings fixture can be removed");
                } else {
                    frame->Hide();
                    frame->Destroy();
                }
                // Destroy() defers the actual deletion to idle. Keep its settings
                // alive until that window can no longer translate native input.
                frame->Bind(wxEVT_DESTROY, [this, frame](wxWindowDestroyEvent& event) {
                    if (event.GetEventObject() == frame) CallAfter([this] { Next(); });
                    event.Skip();
                });
            } catch (const std::exception& error) {
                std::println(stderr, "Search continuation scenario: {:d}/{:d}", next_, scenarios.size());
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                if (!frame->IsBeingDeleted()) frame->Destroy();
                ExitMainLoop();
            }
        });
    }

public:
    bool OnInit() override {
        SetExitOnFrameDelete(false);
        wxString mode;
        if (wxGetEnv("WXNOTEPAD_SEARCH_FEEDBACK_RELEASE", &mode) && mode == "1") {
            const auto scenario = std::find_if(scenarios.begin(), scenarios.end(),
                                               [](const Scenario& item) { return item.run == FeedbackTransitions; });
            next_ = static_cast<size_t>(scenario - scenarios.begin());
        }
        Next();
        return true;
    }

    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
};
} // namespace
wxIMPLEMENT_APP(ContinuationApp);
