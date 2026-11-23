#include "native_key_helpers.h"
#include "native_completion_helpers.h"
#include "native_paint_helpers.h"
#include "native_search_helpers.h"
#include "native_renderer_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#ifdef __WXMSW__
#include "native_tree_input.h"
#endif
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <SciLexer.h>
#include <wx/dcmemory.h>
#include <wx/filename.h>
#include <wx/imagpng.h>
#include <wx/menu.h>
#include <wx/display.h>
#include <wx/colordlg.h>
#include <wx/modalhook.h>

namespace {
using Scintilla::Message;

void Require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

wxMenu* CallTipMenu(EditorFrame* frame) {
    auto* bar = frame->GetMenuBar();
    const int view = bar->FindMenu("View");
    Require(view != wxNOT_FOUND, "View menu is available");
    for (auto* item : bar->GetMenu(view)->GetMenuItems()) {
        if (item->GetItemLabelText() == "Show CallTip") return item->GetSubMenu();
    }
    return nullptr;
}

void CheckModes(EditorFrame* frame) {
    auto* modes = CallTipMenu(frame);
    Require(modes, "View exposes the native Show CallTip menu");
    constexpr std::array labels{"Off", "RGBA", "ARGB", "BGRA", "ABGR"};
    std::array<wxMenuItem*, labels.size()> items{};
    for (size_t index = 0; index < labels.size(); ++index) {
        items[index] = modes->FindItem(modes->FindItem(labels[index]));
        Require(items[index] && items[index]->GetKind() == wxITEM_RADIO, "each original mode is a radio item");
    }
    auto* editor = frame->Editor();
    constexpr int originalDisabledDwell = 10000000;
    constexpr int originalColorDwellMs = 250;
    Require(items.front()->IsChecked() && editor->Send(Message::GetMouseDwellTime) == originalDisabledDwell,
            "CallTip defaults to Off and disables editor dwell");
    for (const size_t index : {1, 2, 3, 4, 0}) {
        NativeSearch::Command(frame, items[index]->GetId());
        for (size_t other = 0; other < items.size(); ++other)
            Require(items[other]->IsChecked() == (index == other), "native menu shows the selected format");
        Require(editor->Send(Message::GetMouseDwellTime) == (index ? originalColorDwellMs : originalDisabledDwell),
                "choosing a mode updates the original dwell time immediately");
    }
    std::puts("PASS: native CallTip radio commands and dwell timing");
}

template <typename Check>
bool WaitFor(Check check, std::chrono::milliseconds timeout = std::chrono::milliseconds(1500)) {
    using namespace std::chrono_literals;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        wxYield();
        if (check()) return true;
        wxMilliSleep(5);
    }
    return false;
}

struct HoverObservation {
    wxPoint point;
    wxPoint previous;
};

HoverObservation Hover(EditorControl* editor, intptr_t position) {
    static wxPoint previous{-1, -1};
    const int x = static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position)) + 1;
    const int y = static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position) +
                                   editor->Send(Message::TextHeight, 0) / 2);
#ifdef __WXGTK__
    auto* widget = editor->GetConnectWidget();
    auto* event = gdk_event_new(GDK_MOTION_NOTIFY);
    event->motion.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
    event->motion.send_event = TRUE;
    event->motion.x = x;
    event->motion.y = y;
    gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget))));
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#else
#ifdef __WXMSW__
    // The next genuine mouse message must agree with this fixture's hover.
    // Retain wx motion delivery: wxMSW filters repeated native coordinates.
    NativeTreeInput::Move(static_cast<HWND>(editor->GetHandle()), editor->ClientToScreen({x, y}));
#endif
    wxMouseEvent event(wxEVT_MOTION);
    event.SetPosition(wxPoint(x, y));
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
#endif
    const HoverObservation observed{{x, y}, previous};
    previous = observed.point;
    return observed;
}

class DwellEvents final : public wxEvtHandler {
public:
    explicit DwellEvents(EditorControl* editor) : editor_(editor) { editor_->PushEventHandler(this); }
    ~DwellEvents() override { editor_->PopEventHandler(false); }
    bool ProcessEvent(wxEvent& event) override {
        const auto type = event.GetEventType();
        const char* kind = type == wxEVT_MOTION         ? "motion"
                           : type == wxEVT_LEAVE_WINDOW ? "leave"
                           : type == wxEVT_KEY_DOWN     ? "key"
                           : type == wxEVT_TIMER        ? "timer"
                                                        : nullptr;
        if (kind) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_)
                    .count();
            const auto point = type == wxEVT_MOTION || type == wxEVT_LEAVE_WINDOW
                                   ? static_cast<wxMouseEvent&>(event).GetPosition()
                                   : wxPoint{-1, -1};
            const auto interval = type == wxEVT_TIMER ? static_cast<wxTimerEvent&>(event).GetInterval() : 0;
            events_.push_back({kind, elapsed, event.GetId(), event.GetTimestamp(), point, interval});
        }
        return wxEvtHandler::ProcessEvent(event);
    }
    void Report() const {
        for (const auto& event : events_)
            std::fprintf(stderr,
                         "color preview event: kind=%s elapsed_ms=%lld id=%d timestamp=%ld point=%d,%d "
                         "timer_interval_ms=%d\n",
                         event.kind, event.elapsed, event.id, event.timestamp, event.point.x, event.point.y,
                         event.interval);
    }

private:
    struct Event {
        const char* kind;
        long long elapsed;
        int id;
        long timestamp;
        wxPoint point;
        int interval;
    };
    EditorControl* editor_;
    const std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    std::vector<Event> events_;
};

wxWindow* VisibleCallTip(EditorControl* editor) {
    auto* popup = wxWindow::FindWindowByName("Color CallTip", editor);
    return popup && popup->IsShownOnScreen() ? popup : nullptr;
}

void CapturePreview(wxWindow* popup, const char* placement) {
    wxString directory;
    if (!wxGetEnv("WXNOTEPAD_CALLTIP_CAPTURE_DIR", &directory)) return;
    Require(wxFileName::DirExists(directory) || wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL),
            "create native CallTip capture directory");
    const auto size = popup->GetClientSize();
#ifdef WXNOTEPAD_TEST_DIRECTWRITE
    constexpr auto renderer = "directwrite";
#elif defined(__WXMSW__)
    constexpr auto renderer = "gdi";
#else
    constexpr auto renderer = "gtk";
#endif
    const auto filename =
        wxFileName(directory, wxString::Format("calltip-%s-%s-%d-dpi.png", renderer, placement, popup->GetDPI().x))
            .GetFullPath();
    NativePaint::Observe(popup, [&](wxDC& dc) {
        int width = size.x;
        int height = size.y;
#ifdef __WXGTK__
        // Copy the composited paint at its device scale instead of redrawing the popup.
        auto* context = static_cast<cairo_t*>(dc.GetGraphicsContext()->GetNativeContext());
        auto* target = cairo_get_target(context);
        double x = 0, y = 0, scaleX = 1, scaleY = 1;
        cairo_user_to_device(context, &x, &y);
        cairo_surface_get_device_scale(target, &scaleX, &scaleY);
        width = static_cast<int>(size.x * scaleX);
        height = static_cast<int>(size.y * scaleY);
        auto* copy = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
        cairo_surface_set_device_scale(copy, scaleX, scaleY);
        auto* canvas = cairo_create(copy);
        cairo_set_source_surface(canvas, target, -x, -y);
        cairo_paint(canvas);
        cairo_destroy(canvas);
        const auto result = cairo_surface_write_to_png(copy, filename.utf8_str());
        cairo_surface_destroy(copy);
        Require(result == CAIRO_STATUS_SUCCESS, "save actual GTK CallTip pixels");
#else
        wxBitmap bitmap(size.x, size.y);
        wxMemoryDC copy(bitmap);
        Require(copy.Blit(0, 0, size.x, size.y, &dc, 0, 0), "read actual Windows CallTip pixels");
        copy.SelectObject(wxNullBitmap);
        if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG)) wxImage::AddHandler(new wxPNGHandler);
        Require(bitmap.SaveFile(filename, wxBITMAP_TYPE_PNG), "save actual Windows CallTip pixels");
#endif
        std::printf("PASS: captured native CallTip paint at %dx%d physical pixels\n", width, height);
    });
}

void CheckPreview(EditorFrame* frame, bool typingInteraction = true) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    auto* modes = CallTipMenu(frame);
    NativeSearch::Command(frame, modes->FindItem("RGBA"));
#ifdef __WXGTK__
    wxString focusFile;
    NativeKeys::FocusEditor(frame, "wxnotepad", focusFile);
#endif
    const auto* focus = wxWindow::FindFocus();
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>("pre #aB3 and"));
    editor->Send(SetSel, 0, 0);
    NativePaint::Complete(editor);
    Hover(editor, 5);
    Require(WaitFor([editor] { return VisibleCallTip(editor); }), "dwell shows a native preview of the original color");
    auto* popup = VisibleCallTip(editor);
    Require(editor->Send(CallTipActive) && wxWindow::FindFocus() == focus,
            "the mapped popup keeps the actual calltip active without taking keyboard focus");
    const auto sample = NativePaint::Read(popup, wxRect(3, 3, 2, 2));
    Require(std::ranges::all_of(sample, [](const wxColour& color) { return color == wxColour(170, 187, 51); }),
            "the native popup paints the RGB independently observed in the original chooser");
    const auto display = wxDisplay::GetFromWindow(frame);
    Require(display != wxNOT_FOUND && wxDisplay(display).GetClientArea().Contains(popup->GetScreenRect()),
            "the native popup fits the active monitor's work area");
    std::printf("PASS: native popup RGB, editor focus and work area; DPI=%dx%d scale=%.1f size=%dx%d\n",
                popup->GetDPI().x, popup->GetDPI().y, popup->GetContentScaleFactor(), popup->GetSize().x,
                popup->GetSize().y);
    CapturePreview(popup, "inline");
    if (!typingInteraction) {
#ifdef __WXMSW__
        NativeCompletion::Key(editor, VK_ESCAPE);
#else
        NativeCompletion::Key(editor, GDK_KEY_Escape);
#endif
        Require(!VisibleCallTip(editor), "native Escape dismisses the language's color preview");
        return;
    }
    wxMouseEvent leave(wxEVT_LEAVE_WINDOW);
    leave.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(leave);
    Require(VisibleCallTip(editor), "leaving the editor keeps the current CallTip visible");
    Hover(editor, 8);
    Require(WaitFor([editor] { return !VisibleCallTip(editor); }) && !editor->Send(CallTipActive),
            "dwelling just outside the accepted color range dismisses the popup");
    Hover(editor, 5);
    Require(WaitFor([editor] { return VisibleCallTip(editor); }), "dwell reopens the preview before typing");
    wxKeyEvent letter(wxEVT_CHAR);
    letter.SetEventObject(editor);
    letter.m_uniChar = 'q';
    editor->GetEventHandler()->ProcessEvent(letter);
    if (!VisibleCallTip(editor) || NativeSearch::Text(editor) != "qpre #aB3 and")
        std::fprintf(stderr, "color typing: lexer=%zd visible=%d text=[%s]\n", editor->Send(GetLexer),
                     VisibleCallTip(editor) != nullptr, NativeSearch::Text(editor).c_str());
    Require(VisibleCallTip(editor) && NativeSearch::Text(editor) == "qpre #aB3 and",
            "ordinary typing changes the document and retains the existing CallTip");
    NativeSearch::Command(frame, modes->FindItem("Off"));
    Require(VisibleCallTip(editor), "choosing Off keeps the current preview until it is dismissed");
#ifdef __WXMSW__
    NativeCompletion::Key(editor, VK_ESCAPE);
#else
    NativeCompletion::Key(editor, GDK_KEY_Escape);
#endif
    Require(!VisibleCallTip(editor), "Escape closes the retained CallTip while dwell is disabled");
}

void ClickCallTip(wxWindow* popup) {
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(popup->GetHandle()), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(4, 4));
#elif defined(__WXGTK__)
    auto* widget = popup->GetConnectWidget();
    auto* event = gdk_event_new(GDK_BUTTON_PRESS);
    event->button.window = static_cast<GdkWindow*>(g_object_ref(popup->GTKGetDrawingWindow()));
    event->button.send_event = TRUE;
    event->button.button = 1;
    event->button.x = event->button.y = 4;
    gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget))));
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
}

class ChooseColor final : public wxModalDialogHook {
public:
    ChooseColor(const wxColour& original, bool cancel, const wxColour& wanted, [[maybe_unused]] bool shift)
        : original_(original), wanted_(wanted), cancel_(cancel) {
#ifdef __WXMSW__
        shift_ = shift;
#endif
        Register();
    }

    ~ChooseColor() override {
#ifdef __WXMSW__
        if (timer_) KillTimer(nullptr, timer_);
        if (shiftSet_) SetKeyboardState(savedKeys_.data());
        active_ = nullptr;
#endif
    }

    int Enter(wxDialog* window) override {
        auto* dialog = wxDynamicCast(window, wxColourDialog);
        if (!dialog) return wxID_NONE;
#ifdef __WXMSW__
        active_ = this;
        timer_ = SetTimer(nullptr, 0, USER_TIMER_MINIMUM, DriveNativeDialog);
        if (!timer_) return wxID_CANCEL;
#elif defined(__WXGTK__)
        wxTheApp->CallAfter([this, dialog] {
            NativeKeys::AfterPresentation(dialog, [this, dialog](bool presented) {
                auto* native = GTK_DIALOG(dialog->GetHandle());
                GObject* selection = nullptr;
                g_object_get(native, "color-selection", &selection, nullptr);
                if (presented && selection) {
                    GdkRGBA* rgb = nullptr;
                    g_object_get(selection, "current-rgba", &rgb, nullptr);
                    matched = rgb && wxColour(*rgb) == original_;
                    if (rgb) gdk_rgba_free(rgb);
                    const GdkRGBA wanted{wanted_.Red() / 255.0, wanted_.Green() / 255.0, wanted_.Blue() / 255.0, 1};
                    g_object_set(selection, "current-rgba", &wanted, nullptr);
                }
                if (selection) g_object_unref(selection);
                gtk_dialog_response(native, presented && matched && !cancel_ ? GTK_RESPONSE_OK : GTK_RESPONSE_CANCEL);
                completed = true;
            });
        });
#endif
        return wxID_NONE;
    }

    bool matched = false;
    bool completed = false;

private:
#ifdef __WXMSW__
    static void CALLBACK DriveNativeDialog(HWND, UINT, UINT_PTR timer, DWORD) {
        if (active_ && active_->timer_ == timer) EnumThreadWindows(GetCurrentThreadId(), FindNativeDialog, 0);
    }

    static BOOL CALLBACK FindNativeDialog(HWND window, LPARAM) {
        constexpr std::array fieldIds{706, 707, 708};
        if (!IsWindowVisible(window) ||
            !std::ranges::all_of(fieldIds, [window](int field) { return GetDlgItem(window, field); }))
            return TRUE;
        auto& choice = *active_;
        KillTimer(nullptr, std::exchange(choice.timer_, 0));
        std::array<UINT, fieldIds.size()> rgb{};
        const std::array channels{choice.wanted_.Red(), choice.wanted_.Green(), choice.wanted_.Blue()};
        for (size_t index = 0; index < fieldIds.size(); ++index) {
            rgb[index] = GetDlgItemInt(window, fieldIds[index], nullptr, FALSE);
            SetDlgItemInt(window, fieldIds[index], channels[index], FALSE);
        }
        choice.matched =
            rgb == std::array<UINT, 3>{choice.original_.Red(), choice.original_.Green(), choice.original_.Blue()};
        if (choice.shift_ && GetKeyboardState(choice.savedKeys_.data())) {
            auto keys = choice.savedKeys_;
            constexpr BYTE pressed = 0x80;
            keys[VK_SHIFT] |= pressed;
            choice.shiftSet_ = SetKeyboardState(keys.data()) != 0;
        }
        SendMessageW(window, WM_COMMAND, choice.cancel_ ? IDCANCEL : IDOK, 0);
        choice.completed = true;
        return FALSE;
    }

    static inline ChooseColor* active_ = nullptr;
    UINT_PTR timer_ = 0;
    std::array<BYTE, 256> savedKeys_{};
    bool shift_ = false;
    bool shiftSet_ = false;
#endif
    wxColour original_;
    wxColour wanted_;
    bool cancel_;
};

enum class ColorChoice : uint8_t { Accept, Cancel, AcceptUppercase };

struct ColorEditCase {
    const char* mode;
    const char* source;
    intptr_t hover;
    wxColour initial;
    const char* expected;
    intptr_t start = 0;
    intptr_t end = 0;
    intptr_t caret = 0;
    ColorChoice choice = ColorChoice::Accept;
};

void CheckColorEdit(EditorFrame* frame, size_t caseIndex, const ColorEditCase& edit) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    NativeSearch::Command(frame, CallTipMenu(frame)->FindItem(edit.mode));
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(edit.source));
    editor->Send(SetSel, 1, 3);
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    NativePaint::Complete(editor);
    DwellEvents events(editor);
#ifdef __WXMSW__
    if (caseIndex == 3 && editor->Send(GetLexer) == SCLEX_NULL && wxGetenv("WXNOTEPAD_CALLTIP_REPLAY_NATIVE_MOTION")) {
        // CI captured a genuine motion here after the synthetic token hover.
        // Leave this native move queued until Hover's normal event-loop wait.
        const wxPoint capturedNativeMotion{414, 409};
        Require(editor->GetClientRect().Contains(capturedNativeMotion), "captured native motion stays in the editor");
        NativeTreeInput::Move(static_cast<HWND>(editor->GetHandle()), editor->ClientToScreen(capturedNativeMotion));
    }
#endif
    const auto hover = Hover(editor, edit.hover);
    const auto point = hover.point;
    const bool preview = WaitFor([editor] { return VisibleCallTip(editor); });
    if (!preview) {
        const auto mouse = editor->ScreenToClient(wxGetMousePosition());
        const auto client = editor->GetClientSize();
        std::fprintf(stderr,
                     "color preview failed: case=%zu lexer=%zd style=%zd mode=%s source=[%s] hover=%td "
                     "point=%d,%d previous=%d,%d hit=%zd "
                     "client=%dx%d dpi=%dx%d calltip=%zd completion=%zd frameActive=%d editorFocus=%d mouse=%d,%d\n",
                     caseIndex, editor->Send(GetLexer), editor->Send(GetStyleIndexAt, edit.hover), edit.mode,
                     edit.source, edit.hover, point.x, point.y, hover.previous.x, hover.previous.y,
                     editor->Send(PositionFromPointClose, point.x, point.y), client.x, client.y, editor->GetDPI().x,
                     editor->GetDPI().y, editor->Send(CallTipActive), editor->Send(AutoCActive), frame->IsActive(),
                     editor->HasFocus(), mouse.x, mouse.y);
        events.Report();
    }
    Require(preview, "original color previews before edit");
    const bool changed = std::string_view(edit.expected) != edit.source;
    const bool cancel = edit.choice == ColorChoice::Cancel;
    ChooseColor chooser(edit.initial, cancel, changed || cancel ? wxColour(205, 171, 239) : edit.initial,
                        edit.choice == ColorChoice::AcceptUppercase);
    ClickCallTip(VisibleCallTip(editor));
    Require(WaitFor([&] { return chooser.completed; }) && chooser.matched,
            "click opens the native chooser with the original computed RGB");
    Require(NativeSearch::Text(editor) == edit.expected && bool(editor->Send(GetModify)) == changed &&
                bool(editor->Send(CanUndo)) == changed && !editor->Send(CanRedo),
            "closing the native chooser applies the original bytes and edit history");
    if (!changed) {
        NativeSearch::Selection(editor, 1, 3, "cancel or choosing the same color keeps the original selection");
        return;
    }
    NativeSearch::Selection(editor, edit.caret, edit.caret,
                            "choosing a color leaves the original caret at the replacement end");
    editor->Send(Undo);
    Require(NativeSearch::Text(editor) == edit.source && !editor->Send(GetModify) && editor->Send(CanRedo),
            "one Undo restores the original bytes and clean save point");
    NativeSearch::Selection(editor, edit.start, edit.end, "Undo restores the original selected digit range");
    editor->Send(Redo);
    Require(NativeSearch::Text(editor) == edit.expected && editor->Send(GetModify),
            "one Redo restores the native color replacement");
    NativeSearch::Selection(editor, edit.caret, edit.caret, "Redo restores the replacement caret");
}

void CheckEdits(EditorFrame* frame) {
    CheckColorEdit(frame, 1, {"ARGB", "pre #1Bd4 and", 5, {187, 221, 68}, "pre #11cdabef and", 5, 9, 13});
    CheckColorEdit(frame, 2, {"RGBA", "pre #aB3 and", 5, {170, 187, 51}, "pre #aB3 and", 0, 0, 0, ColorChoice::Cancel});
    CheckColorEdit(frame, 3, {"RGBA", "pre #aB3 and", 5, {170, 187, 51}, "pre #aB3 and"});
    CheckColorEdit(frame, 4, {"RGBA", "pre #0BD0 and", 6, {0, 187, 221}, "pre #cdabef and", 5, 9, 11});
    CheckColorEdit(frame, 5, {"BGRA", "pre #00a5ef00 and", 7, {239, 165, 0}, "pre #efabcd and", 5, 13, 11});
    CheckColorEdit(frame, 6, {"ABGR", "pre 0X12a5EF74 and", 6, {116, 239, 165}, "pre 0X12efabcdand", 6, 15, 14});
#ifdef __WXMSW__
    CheckColorEdit(
        frame, 7,
        {"RGBA", "pre #aB3 and", 5, {170, 187, 51}, "pre #CDABEF and", 5, 8, 11, ColorChoice::AcceptUppercase});
#endif
    std::puts("PASS: native chooser RGB, cancel/same, all modes, alpha, 0X range and exact Undo/Redo");
}

void CheckCompletionPriority(EditorFrame* frame) {
    using namespace std::chrono_literals;
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    NativeSearch::Command(frame, CallTipMenu(frame)->FindItem("RGBA"));
#ifdef __WXGTK__
    wxString focusFile;
    NativeKeys::FocusEditor(frame, "wxnotepad", focusFile);
#endif
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>("pre #aB3 and"));
    editor->Send(SetSel, 0, 0);
    NativePaint::Complete(editor);
    editor->Send(AutoCShow, 0, reinterpret_cast<intptr_t>("alpha alpine"));
    Require(editor->Send(AutoCActive), "native completion is open before the competing dwell");
    Hover(editor, 5);
    Require(!WaitFor([editor] { return VisibleCallTip(editor) || !editor->Send(AutoCActive); }, 450ms),
            "color dwell neither shows a popup nor cancels active completion");
#ifdef __WXMSW__
    constexpr int escape = VK_ESCAPE;
#else
    constexpr int escape = GDK_KEY_Escape;
#endif
    NativeCompletion::Key(editor, escape);
    Require(!editor->Send(AutoCActive), "Escape first dismisses completion");
    Hover(editor, 6);
    Require(WaitFor([editor] { return VisibleCallTip(editor); }), "a later dwell previews after completion closes");
    NativeCompletion::Key(editor, escape);
    Require(!editor->Send(CallTipActive) && !VisibleCallTip(editor), "Escape dismisses the current color CallTip");
    std::puts("PASS: native completion and Escape priority across competing color dwell");
}

#ifdef __WXMSW__
void CheckImePriority(EditorFrame* frame) {
    using namespace std::chrono_literals;
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const auto window = static_cast<HWND>(editor->GetHandle());
    NativeSearch::Command(frame, CallTipMenu(frame)->FindItem("RGBA"));
    editor->Send(SetIMEInteraction, static_cast<uintptr_t>(Scintilla::IMEInteraction::Inline));
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>("pre #aB3 and"));
    NativePaint::Complete(editor);
    SendMessageW(window, WM_IME_STARTCOMPOSITION, 0, 0);
    Hover(editor, 5);
    const bool deferred = !WaitFor([editor] { return VisibleCallTip(editor); }, 450ms);
    SendMessageW(window, WM_IME_ENDCOMPOSITION, 0, 0);
    Require(deferred, "color dwell waits during a native inline IME composition");
    Hover(editor, 6);
    Require(WaitFor([editor] { return VisibleCallTip(editor); }), "a new dwell previews after composition ends");
    NativeCompletion::Key(editor, VK_ESCAPE);
    Require(!editor->Send(CallTipActive) && NativeSearch::Text(editor) == "pre #aB3 and",
            "Escape closes the preview after IME without modifying the composition line");
    std::puts("PASS: native Windows composition messages and subsequent CallTip Escape");
}
#endif

void CheckTokenBounds(EditorFrame* frame) {
    using namespace std::chrono_literals;
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    NativeSearch::Command(frame, CallTipMenu(frame)->FindItem("RGBA"));
    struct Token {
        const char* text;
        intptr_t hover;
        bool accepted;
    };
    const std::array tokens{
        Token{"pre #aB3 and", 4, true},           Token{"pre #aB3 and", 7, true},
        Token{"pre #12A5EF and", 10, true},       Token{"pre #12a5ef74 and", 12, true},
        Token{"pre 0x12A5Ef and", 4, false},      Token{"pre 0x12A5Ef and", 5, true},
        Token{"pre 0xAB3 and", 6, false},         Token{"pre 1x12A5Ef and", 7, false},
        Token{"pre #0123456789AB and", 9, false}, Token{"pre #12a5e and", 6, false},
        Token{"pre q#123z and", 7, true},         Token{"pre #123", 7, true},
    };
    for (const auto& token : tokens) {
        editor->Send(CallTipCancel);
        editor->Send(SetText, 0, reinterpret_cast<intptr_t>(token.text));
        editor->Send(SetSel, 0, 0);
        NativePaint::Complete(editor);
        Hover(editor, token.hover);
        Require(WaitFor([editor] { return VisibleCallTip(editor); }, token.accepted ? 1500ms : 400ms) == token.accepted,
                token.text);
    }
    editor->Send(CallTipCancel);
    const auto visibleLines = editor->Send(LinesOnScreen);
    const auto input = std::string(static_cast<size_t>(visibleLines - 1), '\n') + "#aB3\n";
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(input.c_str()));
    editor->Send(SetFirstVisibleLine, 0);
    NativePaint::Complete(editor);
    const auto position = visibleLines;
    Hover(editor, position);
    Require(WaitFor([editor] { return VisibleCallTip(editor); }), "the bottom editor line can show a color preview");
    const auto anchorY =
        editor->ClientToScreen(wxPoint(0, static_cast<int>(editor->Send(PointYFromPosition, 0, position)))).y;
    auto* window = VisibleCallTip(editor);
    const auto popup = window->GetScreenRect();
    Require(popup.y < anchorY, "the original CallTip moves above the anchor at the bottom editor edge");
    CapturePreview(window, "bottom");
    editor->Send(CallTipCancel);
    std::printf("PASS: original token bounds and bottom-edge placement (anchor=%d popup=%d)\n", anchorY, popup.y);
}

void CheckPersistence(EditorFrame* frame, const wxString& path) {
    auto* modes = CallTipMenu(frame);
    auto* menus = frame->GetMenuBar();
    const auto saveNow = menus->FindMenuItem("Settings", "Save Settings Now");
    const auto saveOnExit = menus->FindMenuItem("Settings", "Save Settings On Exit");
    Require(saveNow != wxNOT_FOUND && saveOnExit != wxNOT_FOUND, "native settings persistence commands exist");
    NativeSearch::Command(frame, modes->FindItem("ABGR"));
    Require(SettingsStore(path).values.showCallTip == ColorCallTipMode::Off, "choosing a mode does not save it early");
    NativeSearch::Command(frame, saveNow);
    SettingsStore restored(path);
    Require(restored.values.showCallTip == ColorCallTipMode::ABGR, "Save Settings Now writes the selected INI value");
    const auto reopened = std::make_unique<EditorFrame>(&restored);
    auto* reopenedModes = CallTipMenu(reopened.get());
    Require(reopenedModes->IsChecked(reopenedModes->FindItem("ABGR")) &&
                reopened->Editor()->Send(Message::GetMouseDwellTime) == 250,
            "a restarted native editor loads the saved checked mode and dwell setting");
    NativeSearch::Command(frame, saveOnExit);
    NativeSearch::Command(frame, modes->FindItem("BGRA"));
    frame->Editor()->Send(Message::SetSavePoint);
    Require(frame->Close(), "the owned native window closes normally");
    Require(SettingsStore(path).values.showCallTip == ColorCallTipMode::BGRA, "enabled exit persists the changed mode");
    std::puts("PASS: explicit save, restart, and save on exit preserve the native mode");
}

class ColorCallTipApp final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_CALLTIP_DPI96"))
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "create the complementary owned 96-DPI CallTip window");
#endif
        const auto file = TestFiles::TemporaryDirectory() / "color-calltip.ini";
        TestFiles::WriteBytes(file, "[Settings]\nSaveSettings=0\n");
        path_ = TestFiles::Filename(file);
        settings_ = std::make_unique<SettingsStore>(path_);
        auto* frame = new EditorFrame(settings_.get());
        TestRenderer::Select(frame->Editor());
        std::printf("Native CallTip DPI: %d x %d\n", frame->Editor()->GetDPI().x, frame->Editor()->GetDPI().y);
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_CALLTIP_DPI96"))
            Require(frame->Editor()->GetDPI().y == 96, "the complementary native CallTip fixture uses 96 DPI");
#endif
#ifdef __WXMSW__
        frame->ShowWithoutActivating();
#else
        frame->Show();
#endif
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native compositor presents the CallTip test editor");
                CheckModes(frame);
                CheckPreview(frame);
                CheckEdits(frame);
                for (const auto* language :
                     {"CSS Style Sheet", "SCSS Style Sheet", "Less Style Sheet", "HSS Style Sheet", "SQL Query"}) {
                    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", language));
                    CheckPreview(frame, false);
                    CheckEdits(frame);
                }
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Plain Text"));
                CheckCompletionPriority(frame);
#ifdef __WXMSW__
                CheckImePriority(frame);
#endif
                CheckTokenBounds(frame);
                CheckPersistence(frame, path_);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            if (!frame->IsBeingDeleted()) frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    wxString path_;
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(ColorCallTipApp);
