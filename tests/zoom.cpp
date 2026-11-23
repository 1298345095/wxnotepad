#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/menu.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/dcclient.h>
#include <cstdio>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <array>
#ifdef __WXMSW__
#include "editor/platform/directwrite_surface.h"
#include "PlatWin.h"
#endif

namespace {
using Scintilla::Message;
#ifdef __WXMSW__
long OriginalDirectWriteWidth(EditorControl* editor, const char* face, int points, std::string_view text) {
    namespace core = Scintilla::Internal;
    auto surface = core::OriginalDirectWriteSurface();
    surface->Init(editor->GetHandle());
    const core::FontWin font(
        core::FontParameters(face, points * editor->GetDPI().y / static_cast<double>(core::pointsPerInch),
                             Scintilla::FontWeight::Normal, Scintilla::FontStretch::Normal, false,
                             Scintilla::FontQuality::QualityDefault, Scintilla::Technology::DirectWrite));
    // Pinned Editor::TextWidth rounds the original surface width at the message boundary.
    return std::lround(surface->WidthTextUTF8(&font, text));
}
#endif
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Key(EditorControl* editor, int key) {
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = key;
    event.m_controlDown = true;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Menu(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("View", label);
    Require(id != wxNOT_FOUND, "original zoom menu command exists");
    wxCommandEvent event(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(event), "zoom menu command is handled");
}
void Wheel(EditorControl* editor, int rotation, bool control = false, bool shift = false,
           wxMouseWheelAxis axis = wxMOUSE_WHEEL_VERTICAL, int lines = 3) {
    wxMouseEvent event(wxEVT_MOUSEWHEEL);
    event.m_wheelRotation = rotation;
    event.m_wheelDelta = 120;
    event.m_linesPerAction = lines;
    event.m_columnsPerAction = 3;
    event.m_wheelAxis = axis;
    event.m_controlDown = control;
    event.m_shiftDown = shift;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Level(EditorFrame* frame, const wxString& value, int button, bool applyFirst = false) {
    bool observed = false;
    wxTheApp->CallAfter([&observed, value, button, applyFirst] {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || dialog->GetTitle() != "Zoom Level") continue;
            auto* input = wxDynamicCast(wxWindow::FindWindowByName("Zoom level", dialog), wxComboBox);
            observed =
                input && input->GetCount() == 14 && input->GetString(0) == "500%" && input->GetString(13) == "25%";
            if (input) input->SetValue(value);
            if (applyFirst) {
                wxCommandEvent apply(wxEVT_BUTTON, wxID_APPLY);
                dialog->GetEventHandler()->ProcessEvent(apply);
            }
            wxCommandEvent command(wxEVT_BUTTON, button);
            dialog->GetEventHandler()->ProcessEvent(command);
            return;
        }
    });
    Menu(frame, "Zoom Level...");
    Require(observed, "Zoom Level uses original editable preset list");
}
std::string Text(EditorControl* editor) {
    std::string text(static_cast<size_t>(editor->Send(Message::GetLength)) + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
void NativeKey(EditorControl* editor, int key) {
#ifdef __WXMSW__
    BYTE previous[256];
    Require(GetKeyboardState(previous), "read owned thread key state");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    modified[VK_CONTROL] = 0x80;
    modified[VK_SHIFT] = 0;
    Require(SetKeyboardState(modified), "set owned thread Ctrl modifier");
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYDOWN, key, 0);
    SetKeyboardState(previous);
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
    event->key.send_event = TRUE;
    event->key.keyval = key;
    event->key.state = GDK_CONTROL_MASK;
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
void NativeWheel(EditorControl* editor) {
#ifdef __WXMSW__
    const auto point = editor->ClientToScreen(wxPoint(100, 100));
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, 120),
                 MAKELPARAM(point.x, point.y));
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    auto* event = gdk_event_new(GDK_SCROLL);
    event->scroll.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
    event->scroll.send_event = TRUE;
    event->scroll.direction = GDK_SCROLL_UP;
    event->scroll.state = GDK_CONTROL_MASK;
    event->scroll.x = 100;
    event->scroll.y = 100;
    gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget))));
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
void CheckZoomedGeometry(EditorControl* editor, std::string_view bytes, uint64_t revision) {
    using enum Scintilla::Message;
    std::array<char, 100> font{};
    editor->Send(StyleGetFont, static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::Default)),
                 reinterpret_cast<intptr_t>(font.data()));
#ifdef __WXMSW__
    constexpr std::array technologies{Scintilla::Technology::DirectWrite, Scintilla::Technology::Default};
#else
    constexpr std::array technologies{Scintilla::Technology::Default};
#endif
    for (const auto technology : technologies) {
#ifdef __WXMSW__
        editor->Send(SetTechnology, static_cast<uintptr_t>(technology));
        Require(editor->Send(GetTechnology) == static_cast<intptr_t>(technology),
                "zoom geometry uses the requested native renderer");
#endif
        intptr_t normalHeight = 0;
        for (const auto [zoom, points] : {std::pair{100, 12}, {200, 24}, {50, 6}, {175, 21}, {100, 12}}) {
            editor->Send(SetZoom, zoom);
            editor->Refresh();
            editor->Update();
            wxYield();
            wxClientDC native(editor);
            native.SetFont(wxFont(wxFontInfo(points).FaceName(wxString::FromUTF8(font.data()))));
#ifdef __WXMSW__
            if (technology == Scintilla::Technology::DirectWrite) {
                Require(editor->Send(TextWidth, static_cast<uintptr_t>(Scintilla::StylesCommon::Default),
                                     reinterpret_cast<intptr_t>("WWWWWW")) ==
                            OriginalDirectWriteWidth(editor, font.data(), points, "WWWWWW"),
                        "zoomed DirectWrite text matches the pinned original at the same point size and DPI");
                Require(editor->Send(GetMarginWidthN, 0) ==
                            OriginalDirectWriteWidth(editor, font.data(), points, "999") + editor->FromDIP(8),
                        "DirectWrite line-number margin matches original native glyph metrics and DPI padding");
            } else
#endif
            {
                Require(editor->Send(TextWidth, 32, reinterpret_cast<intptr_t>("WWWWWW")) ==
                            native.GetTextExtent("WWWWWW").x,
                        "zoomed text uses native point size at the window DPI");
                Require(editor->Send(GetMarginWidthN, 0) == native.GetTextExtent("999").x + editor->FromDIP(8),
                        "line-number margin uses zoomed native glyph metrics and DPI padding");
            }
            const auto height = editor->Send(TextHeight, 0);
            if (zoom == 100) normalHeight = height;
            if (zoom == 200) Require(height > normalHeight, "zoom grows text line height");
            for (intptr_t position : {3, 9, 203}) {
                const auto x = editor->Send(PointXFromPosition, 0, position);
                const auto y = editor->Send(PointYFromPosition, 0, position);
                Require(editor->Send(PositionFromPointClose, x + 1, y + height / 2) == position,
                        "hit testing follows zoomed text coordinates and margin offset");
            }
            Require(editor->Send(StyleGetSize,
                                 static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::Default))) == 12 &&
                        Text(editor) == bytes && editor->DocumentRevision() == revision &&
                        editor->Send(GetAnchor) == 3 && editor->Send(GetCurrentPos) == 9 && editor->Send(CanUndo) &&
                        editor->Send(CanRedo) && editor->Send(GetModify),
                    "zoom preserves base point size, bytes, selection, save point and undo/redo state");
            std::println(
                "PASS: technology={:d} zoom={:d} native_points={:d} dpi={:d} height={:d} margin={:d} text/hit geometry",
                std::to_underlying(technology), zoom, points, editor->GetDPI().y, static_cast<long long>(height),
                static_cast<long long>(editor->Send(GetMarginWidthN, 0)));
        }
    }
}
void CheckZoom(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
#ifdef __WXMSW__
    const auto initialTechnology = static_cast<Scintilla::Technology>(editor->Send(GetTechnology));
    Require(initialTechnology == Scintilla::Technology::DirectWrite,
            "Windows zoom commands exercise the application default DirectWrite renderer");
#endif
    Require(editor->Send(GetZoom) == 100, "original default zoom is 100 percent");
    Key(editor, '+');
    Require(editor->Send(GetZoom) == 110, "Ctrl+plus zooms in by original ten percent step");
    Menu(frame, "Zoom Out");
    Require(editor->Send(GetZoom) == 100, "Zoom Out shares core command");
    editor->Send(SetZoom, 200);
    Key(editor, WXK_NUMPAD_ADD);
    Require(editor->Send(GetZoom) == 225, "original step above 200 is 25 percent");
    Key(editor, WXK_NUMPAD_SUBTRACT);
    Require(editor->Send(GetZoom) == 200, "keypad minus shares zoom out");
    Key(editor, '\\');
    Require(editor->Send(GetZoom) == 100, "Ctrl+backslash resets to 100 percent");
    editor->Send(SetZoom, 499);
    Menu(frame, "Zoom In");
    Menu(frame, "Zoom In");
    Require(editor->Send(GetZoom) == 500, "zoom in clamps at 500 percent");
    editor->Send(SetZoom, 11);
    Key(editor, '-');
    Key(editor, '-');
    Require(editor->Send(GetZoom) == 10, "zoom out clamps at 10 percent");
    Menu(frame, "Reset Zoom");
    Require(editor->Send(GetZoom) == 100, "reset command restores original default");
    std::println("PASS: original zoom commands, shortcuts, reset, steps and limits; dpi={:d}", editor->GetDPI().y);
    std::string text;
    for (int line = 0; line < 200; ++line)
        text += std::string(200, 'W') + "\r\n";
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    editor->Send(SetSel, 3, 9);
    editor->Send(SetScrollWidth, 4000);
    Wheel(editor, 60, true);
    Require(editor->Send(GetZoom) == 100, "fractional wheel accumulates before zoom");
    Wheel(editor, 60, true);
    Require(editor->Send(GetZoom) == 110, "Ctrl+wheel zooms instead of scrolling");
    Wheel(editor, 360, true);
    Require(editor->Send(GetZoom) == 120, "multiple notches in one event zoom only once");
    Wheel(editor, -120, true);
    Require(editor->Send(GetZoom) == 110, "negative Ctrl+wheel zooms out");
    Wheel(editor, 120, true, false, wxMOUSE_WHEEL_VERTICAL, 0);
    Require(editor->Send(GetZoom) == 110, "disabled vertical wheel does not zoom");
    editor->Send(SetXOffset, 300);
    Wheel(editor, 120, true, true);
    Require(editor->Send(GetZoom) == 110 && editor->Send(GetXOffset) < 300,
            "Shift precedes Ctrl and scrolls horizontally");
    editor->Send(SetXOffset, 300);
    Wheel(editor, 120, true, true, wxMOUSE_WHEEL_HORIZONTAL);
    Require(editor->Send(GetZoom) == 110 && editor->Send(GetXOffset) > 300,
            "physical horizontal wheel precedes modifiers and reverses direction");
    editor->Send(SetWrapMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::Wrap::Word)));
    editor->Send(SetXOffset, 0);
    Wheel(editor, 120, true, true);
    Require(editor->Send(GetXOffset) == 0 && editor->Send(GetZoom) == 110,
            "wrapped horizontal wheel neither scrolls nor zooms");
    editor->Send(SetWrapMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::Wrap::None)));
    editor->Send(SetFirstVisibleLine, 30);
    Wheel(editor, -120);
    Require(editor->Send(GetFirstVisibleLine) == 33, "plain wheel retains vertical scrolling");
    std::puts("PASS: original wheel precedence, accumulation, disabled wheel and plain scrolling");
    Wheel(editor, 60, false, true);
    Wheel(editor, 60, true);
    Require(editor->Send(GetZoom) == 120, "Shift and Ctrl share the physical vertical accumulator");
    Require(frame->GetMenuBar()->FindMenuItem("View", "Zoom Level...") != wxNOT_FOUND,
            "original Zoom Level dialog command exists");
    Level(frame, "175%", wxID_OK);
    Require(editor->Send(GetZoom) == 175, "Zoom Level accepts original numeric prefix");
    Level(frame, "501", wxID_OK);
    Require(editor->Send(GetZoom) == 175, "out-of-range OK closes without changing zoom");
    Level(frame, "invalid", wxID_OK);
    Require(editor->Send(GetZoom) == 175, "nonnumeric OK keeps the current screen zoom");
    Level(frame, "50", wxID_CANCEL);
    Require(editor->Send(GetZoom) == 175, "Cancel leaves unapplied value unchanged");
    Level(frame, "125%", wxID_CANCEL, true);
    Require(editor->Send(GetZoom) == 125, "Cancel retains already applied zoom");
    std::puts("PASS: Zoom Level presets, original parsing, invalid OK, Apply and Cancel");
    for (const char* suffix : {"A", "B"}) {
        editor->Send(BeginUndoAction);
        editor->Send(AppendText, 1, reinterpret_cast<intptr_t>(suffix));
        editor->Send(EndUndoAction);
    }
    editor->Send(Undo);
    const auto bytes = Text(editor);
    const auto revision = editor->DocumentRevision();
    editor->Send(SetSel, 3, 9);
    editor->Send(SetFirstVisibleLine, 0);
    editor->Send(SetXOffset, 0);
    CheckZoomedGeometry(editor, bytes, revision);
#ifdef __WXMSW__
    editor->Send(SetTechnology, static_cast<uintptr_t>(initialTechnology));
#endif
    editor->Send(Undo);
    Require(Text(editor) == text && !editor->Send(GetModify), "Undo still reaches the save point");
    editor->Send(Redo);
    editor->Send(Redo);
    Require(Text(editor) == text + "AB", "both original edits remain redoable after zoom");
    std::puts("PASS: bytes, selection, base point size and complete history survive zoom");
#ifdef __WXMSW__
    NativeKey(editor, VK_OEM_PLUS);
    Require(editor->Send(GetZoom) == 110, "native Ctrl+equals reaches Zoom In");
    NativeKey(editor, VK_SUBTRACT);
    Require(editor->Send(GetZoom) == 100, "native keypad subtract reaches Zoom Out");
    NativeKey(editor, VK_OEM_5);
#elif defined(__WXGTK__)
    NativeKey(editor, GDK_KEY_equal);
    Require(editor->Send(GetZoom) == 110, "native Ctrl+equals reaches Zoom In");
    NativeKey(editor, GDK_KEY_KP_Subtract);
    Require(editor->Send(GetZoom) == 100, "native keypad subtract reaches Zoom Out");
    NativeKey(editor, GDK_KEY_backslash);
#endif
    Require(editor->Send(GetZoom) == 100, "native Ctrl+backslash resets zoom");
    NativeWheel(editor);
    Require(editor->Send(GetZoom) == 110, "native Ctrl+wheel reaches zoom operation");
    std::puts("PASS: owned native key and wheel events reach original zoom commands");
}
class ZoomApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        TestEditor::UseFixedWidthFont(frame->Editor());
        frame->Show();
        CallAfter([this, frame] {
            try {
                CheckZoom(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::println(stderr, "FAIL: {}", error.what());
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
wxIMPLEMENT_APP(ZoomApp);
