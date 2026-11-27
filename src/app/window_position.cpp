#include "app/editor_frame.h"
#include "app/new_window.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/translation.h>
#include <algorithm>
#include <utility>
#include <limits>
#ifdef __WXMSW__
#include <windows.h>
#elif defined(__WXGTK__)
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#endif

namespace {
// Notepad4 89b8b36, InitInstance and SnapToDefaultPos; licenses/Notepad4.txt.
// Original coordinates and inset are pixels, independent of the editor's DPI.
constexpr int DefaultInset = 16;
wxRect DefaultRectangle(const wxRect& work) {
    const int height = std::max(1, work.height - 2 * DefaultInset);
    const int width = std::max(1, std::min(work.width - 2 * DefaultInset, height));
    return {work.GetRight() + 1 - width - DefaultInset, work.y + DefaultInset, width, height};
}
wxRect FitRectangle(wxRect normal, const wxRect& work) {
    normal.width = std::min(normal.width, work.width);
    normal.height = std::min(normal.height, work.height);
    normal.x = std::clamp(normal.x, work.x, work.x + work.width - normal.width);
    normal.y = std::clamp(normal.y, work.y, work.y + work.height - normal.height);
    return normal;
}
wxDisplay PositionDisplay(const wxPoint& point) {
    int display = wxDisplay::GetFromPoint(point);
#ifdef __WXMSW__
    const POINT nativePoint{point.x, point.y};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (::GetMonitorInfoW(::MonitorFromPoint(nativePoint, MONITOR_DEFAULTTONEAREST), &monitor))
        display = wxDisplay::GetFromPoint(wxPoint(monitor.rcMonitor.left, monitor.rcMonitor.top));
#else
    if (display == wxNOT_FOUND) {
        double shortestDistance = std::numeric_limits<double>::max();
        for (unsigned index = 0; index < wxDisplay::GetCount(); ++index) {
            const auto bounds = wxDisplay(index).GetGeometry();
            const double dx = static_cast<double>(point.x) - std::clamp(point.x, bounds.x, bounds.GetRight());
            const double dy = static_cast<double>(point.y) - std::clamp(point.y, bounds.y, bounds.GetBottom());
            const double distance = dx * dx + dy * dy;
            if (distance < shortestDistance) {
                shortestDistance = distance;
                display = static_cast<int>(index);
            }
        }
    }
#endif
    return wxDisplay(display == wxNOT_FOUND ? 0 : static_cast<unsigned>(display));
}
wxDisplay RectangleDisplay(const wxRect& rectangle) {
#ifdef __WXMSW__
    const RECT rect{rectangle.x, rectangle.y, rectangle.x + rectangle.width, rectangle.y + rectangle.height};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (::GetMonitorInfoW(::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor))
        return PositionDisplay(wxPoint(monitor.rcMonitor.left, monitor.rcMonitor.top));
#else
    int greatestArea = 0;
    unsigned selected = 0;
    for (unsigned index = 0; index < wxDisplay::GetCount(); ++index) {
        const auto overlap = wxDisplay(index).GetGeometry().Intersect(rectangle);
        const int area = overlap.width * overlap.height;
        if (area > greatestArea) {
            greatestArea = area;
            selected = index;
        }
    }
    if (greatestArea > 0) return wxDisplay(selected);
#endif
    return PositionDisplay(rectangle.GetPosition());
}
void PositionError(wxWindow* window, SettingsStore* settings) {
    if (!settings) return;
    wxMessageBox(wxString::Format(_("Could not write %s"), settings->Path()), _("Could not save settings"),
                 wxOK | wxICON_ERROR, window);
}
} // namespace

bool HasGlobalWindowCoordinates() {
#ifdef __WXGTK__
    return GDK_IS_X11_DISPLAY(gdk_display_get_default());
#else
    return true;
#endif
}

bool ApplyPublicWindowPosition(EditorFrame& window, const wxString& argument) {
    const auto position = ParseWindowPosition(argument);
    if (!position) return false;
    if (!HasGlobalWindowCoordinates()) {
        window.SetSize(position->normal.GetSize());
        // Before first map GTK can defer size notifications until after the
        // compositor maximizes. Capture the requested normal size first.
        window.SendSizeEvent();
        window.RememberInitialWindowPosition(position->maximized);
        window.Maximize(position->maximized);
        return true;
    }
    auto display = RectangleDisplay(position->normal);
    wxRect normal = position->normal;
#ifdef __WXMSW__
    // WINDOWPLACEMENT exports workspace coordinates. CreateWindow/SetSize take
    // screen coordinates: add the monitor's taskbar work-area offset exactly once.
    const auto offset = display.GetClientArea().GetPosition() - display.GetGeometry().GetPosition();
    normal.Offset(offset);
#endif
    window.SetSize(FitRectangle(normal, display.GetClientArea()), wxSIZE_ALLOW_MINUS_ONE);
    window.SendSizeEvent();
    window.RememberInitialWindowPosition(position->maximized);
    window.Maximize(position->maximized);
    return true;
}

void EditorFrame::RememberInitialWindowPosition(bool maximized) {
    normalWindowPosition_.normal = GetRect();
    normalWindowPosition_.maximized = maximized;
    normalWindowSize_ = GetSize();
    pendingInitialMaximize_ = maximized;
}

void EditorFrame::CaptureNormalWindowPosition() {
    if (pendingInitialMaximize_) {
        if (!IsMaximized()) return;
        pendingInitialMaximize_ = false;
    }
    if (FullScreenActive() || IsIconized()) return;
    normalWindowPosition_.coordinates = HasGlobalWindowCoordinates();
    normalWindowPosition_.screen = wxDisplay(this).GetGeometry().GetSize();
#ifdef __WXMSW__
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    const auto window = static_cast<HWND>(GetHandle());
    if (!::GetWindowPlacement(window, &placement)) return;
    const auto& rect = placement.rcNormalPosition;
    normalWindowPosition_.normal = wxRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    normalWindowPosition_.maximized = ::IsZoomed(window) || (placement.flags & WPF_RESTORETOMAXIMIZED);
#else
    normalWindowPosition_.maximized = IsMaximized();
    if (!IsMaximized()) normalWindowPosition_.normal = GetRect();
#endif
    if (normalWindowPosition_.normal.width <= 0 || normalWindowPosition_.normal.height <= 0)
        normalWindowPosition_.normal = wxRect(GetPosition(), normalWindowSize_);
}

void EditorFrame::RestoreWindowPosition() {
    normalWindowPosition_.normal = GetRect();
    normalWindowPosition_.coordinates = HasGlobalWindowCoordinates();
    auto display = PositionDisplay(settings_ ? settings_->LastWindowPosition() : wxPoint{});
    normalWindowPosition_.screen = display.GetGeometry().GetSize();
    if (!settings_) return;
    const auto saved = settings_->LoadWindowPosition(normalWindowPosition_.screen, normalWindowPosition_.coordinates);
    if (saved) {
        const auto& rect = saved->normal;
        ApplyPublicWindowPosition(
            *this, wxString::Format("%d,%d,%d,%d,%d", rect.x, rect.y, rect.width, rect.height, saved->maximized));
        normalWindowPosition_ = *saved;
    } else if (normalWindowPosition_.coordinates) {
        SetSize(DefaultRectangle(display.GetClientArea()));
        CaptureNormalWindowPosition();
    }
    normalWindowSize_ = normalWindowPosition_.normal.GetSize();
}

void EditorFrame::DefaultWindowPosition() {
    if (!HasGlobalWindowCoordinates()) {
        // TRANSLATORS: Wayland limitation: no compositor global-position protocol is available.
        SetStatusText(_("Window coordinates and default positioning are unavailable on Wayland."));
        return;
    }
    const bool restoring = FullScreenActive() || IsMaximized() || IsIconized();
    const auto work = wxDisplay(this).GetClientArea();
    auto target = DefaultRectangle(work);
    if (target == GetRect()) target.x = work.x + DefaultInset;
    if (restoring) pendingDefaultPosition_ = target;
    if (FullScreenActive()) SetFullScreen(false);
    if (IsIconized()) Iconize(false);
    Maximize(false);
    if (restoring) {
        ApplyPendingWindowPosition();
        return;
    }
    SetSize(target, wxSIZE_ALLOW_MINUS_ONE);
    CaptureNormalWindowPosition();
}

void EditorFrame::ApplyPendingWindowPosition() {
    if (!pendingDefaultPosition_ || IsMaximized() || IsIconized() || FullScreenActive()) return;
#ifdef __WXGTK__
    auto* window = gtk_widget_get_window(GTK_WIDGET(GetHandle()));
    if (window && (gdk_window_get_state(window) &
                   (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN | GDK_WINDOW_STATE_ICONIFIED)))
        return;
#endif
    const auto target = pendingDefaultPosition_.value();
    pendingDefaultPosition_.reset();
    CallAfter([this, target] {
        SetSize(target, wxSIZE_ALLOW_MINUS_ONE);
        CaptureNormalWindowPosition();
    });
}

void EditorFrame::CopyWindowPosition() {
    if (!HasGlobalWindowCoordinates()) {
        SetStatusText(_("Window coordinates and default positioning are unavailable on Wayland."));
        return;
    }
    CaptureNormalWindowPosition();
    const auto& rect = normalWindowPosition_.normal;
#ifdef __WXMSW__
    constexpr auto prefix = "/pos";
#else
    constexpr auto prefix = "-pos";
#endif
    const auto bytes = wxString::Format("%s %d,%d,%d,%d,%d", prefix, rect.x, rect.y, rect.width, rect.height,
                                        normalWindowPosition_.maximized)
                           .utf8_string();
    editor_->Send(Scintilla::Message::CopyText, bytes.size(), reinterpret_cast<intptr_t>(bytes.c_str()));
}

void EditorFrame::AppendWindowPositionCommands(wxMenu* menu) {
    const int sticky = wxWindow::NewControlId();
    // TRANSLATORS: Original IDM_VIEW_STICKY_WINDOW_POSITION under Settings > Window Options.
    menu->AppendCheckItem(sticky, _("&Sticky Window Position"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& values = settings_ ? settings_->values : defaults_;
            if (settings_) {
                if ((!values.stickyWindowPosition && !SaveSettings(true)) ||
                    !settings_->SaveStickyWindowPosition(!values.stickyWindowPosition))
                    PositionError(this, settings_);
            } else
                values.stickyWindowPosition = !values.stickyWindowPosition;
            UpdateWindowUI(wxUPDATE_UI_RECURSE);
        },
        sticky);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Check((settings_ ? settings_->values : defaults_).stickyWindowPosition);
        },
        sticky);
    const int clear = wxWindow::NewControlId();
    // TRANSLATORS: Original IDM_VIEW_CLEARWINPOS under Settings > Window Options.
    menu->Append(clear, _("Clear Position &History"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            if (settings_ && !settings_->values.stickyWindowPosition && !settings_->ClearWindowPositionHistory())
                PositionError(this, settings_);
        },
        clear);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Enable(!(settings_ ? settings_->values : defaults_).stickyWindowPosition);
        },
        clear);
    menu->AppendSeparator();
}

void EditorFrame::AppendCopyWindowPosition(wxMenu* menu) {
    const int copy = wxWindow::NewControlId();
    menu->AppendSeparator();
    // TRANSLATORS: Original CMD_COPYWINPOS, Edit > Copy to Clipboard.
    menu->Append(copy, _("&Window Position\tCtrl+Shift+K"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CopyWindowPosition(); }, copy);
    Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event) { event.Enable(HasGlobalWindowCoordinates()); }, copy);
}
