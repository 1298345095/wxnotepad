#include "diagnostics/print.h"
#include "editor_test_helpers.h"
#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "editor/platform/clipboard.h"
#include <wx/display.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <print>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw TestEditor::Failure(message);
}
void Choose(EditorFrame& frame, const wxString& label) {
    const int id = frame.GetMenuBar()->FindMenuItem("Settings", label);
    Require(id != wxNOT_FOUND, "original window-position command exists");
    wxCommandEvent command(wxEVT_MENU, id);
    frame.GetEventHandler()->ProcessEvent(command);
    frame.UpdateWindowUI(wxUPDATE_UI_RECURSE);
}
template <typename Ready> void Await(Ready ready, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    do {
        wxYield();
        if (ready()) return;
        wxMilliSleep(10);
    } while (std::chrono::steady_clock::now() < deadline);
    Require(false, message);
}
wxString PositionSection(EditorFrame& frame) {
#ifdef __WXGTK__
    if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) return "wxnotepad Wayland Window";
#endif
    const auto size = wxDisplay(&frame).GetGeometry().GetSize();
    return wxString::Format("Window Position %dx%d", size.x, size.y);
}
void SavedRectangle(const wxString& path, wxSize size, const wxString& section) {
    IniDocument ini(path);
    Require(ini.Load(), "read actual position INI");
    Require(ini.ReadInt("WindowSizeX", 0, section) == size.x && ini.ReadInt("WindowSizeY", 0, section) == size.y,
            "INI stores normal outer dimensions rather than client, minimized or full-screen dimensions");
    Require(ini.ReadString("Marker", {}, "Unrelated") == "retained", "position commands retain unrelated settings");
}
std::string Copy(EditorFrame& frame) {
    NativeKeys::Shortcut(&frame, 'K', true, false, true);
    const auto copied = EditorClipboard::ReadPlainText(frame);
    if (!copied) throw TestEditor::Failure("native clipboard receives position text");
    return *copied;
}
} // namespace
class PositionApp final : public wxApp {
public:
    bool OnInit() override {
        path_ = wxFileName::CreateTempFileName("window-position-");
        wxFFile ini(path_, "w");
        ini.Write("[Settings]\nSaveSettings=0\n[Settings2]\nUILanguage=1033\n"
                  "[Window Position 1x1]\nRunDlgSizeX=999\n[Unrelated]\nMarker=retained\n");
        ini.Close();
        settings_ = std::make_unique<SettingsStore>(path_);
        auto* frame = new EditorFrame(settings_.get());
        frame->SetSize(100, 120, 640, 480);
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "owned window is presented");
                const auto section = PositionSection(*frame);
                const bool coordinates = section != "wxnotepad Wayland Window";
                Await([&] { return frame->GetSize() == wxSize(640, 480); }, "normal rectangle is presented");
                Choose(*frame, "Sticky Window Position");
                IniDocument ini(path_);
                Require(ini.Load() && ini.ReadBool("StickyWindowPosition", false, "Settings2"),
                        "sticky command persists its original flag immediately");
                SavedRectangle(path_, {640, 480}, section);
                const int clear = frame->GetMenuBar()->FindMenuItem("Settings", "Clear Position History");
                Require(clear != wxNOT_FOUND && !frame->GetMenuBar()->IsEnabled(clear),
                        "history clearing is disabled while sticky is active");
                frame->SetSize(160, 180, 700, 510);
                Await([&] { return frame->GetSize() == wxSize(700, 510); }, "moved rectangle is presented");
                Choose(*frame, "Save Settings Now");
                SavedRectangle(path_, {640, 480}, section);
                Choose(*frame, "Sticky Window Position");
                Choose(*frame, "Save Settings Now");
                SavedRectangle(path_, {700, 510}, section);
                TestEditor::Append(frame, "document survives position commands");
                const auto text = TestEditor::Text(frame);
                const int copy = frame->GetMenuBar()->FindMenuItem("Edit", "Window Position");
                Require(copy != wxNOT_FOUND && frame->GetMenuBar()->IsEnabled(copy) == coordinates,
                        "coordinate export accurately reflects the display backend");
                if (coordinates) {
                    const auto copied = Copy(*frame);
#ifdef __WXMSW__
                    Require(copied == "/pos 160,180,700,510,0", "original five-field native clipboard literal");
#else
                    Require(copied == "-pos 160,180,700,510,0", "X11 five-field clipboard literal");
#endif
                    frame->Iconize();
                    Await([&] { return frame->IsIconized(); }, "owned window is minimized");
                    Choose(*frame, "Save Settings Now");
                    SavedRectangle(path_, {700, 510}, section);
                    frame->Iconize(false);
                    Await([&] { return !frame->IsIconized(); }, "owned window is restored");
                    NativeKeys::Shortcut(frame, WXK_F11, false);
                    Choose(*frame, "Save Settings Now");
                    SavedRectangle(path_, {700, 510}, section);
                    Require(Copy(*frame) == copied, "fullscreen exports the ordinary rectangle");
                    NativeKeys::Shortcut(frame, WXK_ESCAPE, false);
                    Await([&] { return frame->GetSize() == wxSize(700, 510); }, "fullscreen restores normal geometry");
                    frame->Maximize();
                    Await([&] { return frame->IsMaximized(); }, "owned window is maximized");
                    Choose(*frame, "Save Settings Now");
                    SavedRectangle(path_, {700, 510}, section);
                    Require(Copy(*frame).ends_with(",700,510,1"), "copy exports normal rectangle with maximized flag");
                    NativeKeys::Shortcut(frame, 'P', true, false, true);
                    const auto work = wxDisplay(frame).GetClientArea();
                    const int height = work.height - 32;
                    const int width = std::min(work.width - 32, height);
                    const wxRect right(work.x + work.width - width - 16, work.y + 16, width, height);
                    Await([&] { return !frame->IsMaximized() && frame->GetRect() == right; },
                          "default shortcut restores original right work-area rectangle from maximized");
                    NativeKeys::Shortcut(frame, 'P', true, false, true);
                    const wxRect left(work.x + 16, work.y + 16, width, height);
                    Await([&] { return frame->GetRect() == left; },
                          "default shortcut toggles to original left rectangle");
                } else {
                    const auto before = frame->GetSize();
                    NativeKeys::Shortcut(frame, 'P', true, false, true);
                    Require(frame->GetSize() == before, "Wayland default positioning leaves actual size intact");
                    Require(frame->GetStatusBar()->GetStatusText().Contains("unavailable on Wayland"),
                            "Wayland shortcut explains the unavailable positioning operation");
                    ini.Load();
                    Require(!ini.FindValue("WindowPosX", section) && !ini.FindValue("WindowPosY", section),
                            "Wayland never saves fabricated global coordinates");
                    Require(ini.ReadInt("RunDlgSizeX", 0, "Window Position 1x1") == 999,
                            "Wayland size saves preserve imported original global-position history");
                }
                Choose(*frame, "Clear Position History");
                Require(ini.Load(), "read cleared INI");
                for (const auto& name : ini.SectionNames())
                    Require(!name.StartsWith("Window Position") && name != "wxnotepad Wayland Window",
                            "clear removes all original resolution histories and Wayland size history");
                Require(TestEditor::Text(frame) == text, "all position commands leave current document usable");
                std::puts("PASS: sticky, save-off/explicit-save, normal/fullscreen/maximized history, clipboard, "
                          "default and clear");
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                status_ = 1;
            }
            frame->Destroy();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return status_;
    }
    int OnExit() override {
        wxRemoveFile(path_);
        return wxApp::OnExit();
    }

private:
    wxString path_;
    std::unique_ptr<SettingsStore> settings_;
    int status_ = 0;
};
wxIMPLEMENT_APP(PositionApp);
