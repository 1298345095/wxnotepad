// PROTOTYPE: expose public editor messages to the common native-window driver.
#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include <wx/wx.h>

namespace {
class ComparisonApp final : public wxApp {
    SettingsStore settings_;

public:
    bool OnInit() override {
        if (argc != 2) return false;
        settings_.values = OriginalSettings{};
        settings_.values.wordWrap = false;
        settings_.values.saveSettings = false;
        settings_.values.fileWatching.mode = FileWatchingMode::None;
        auto* frame = new EditorFrame(&settings_);
        frame->SetTitle("PROTOTYPE - original application comparison");
        frame->Show();
        CallAfter([this, frame] {
            if (frame->Open(wxString(argv[1])).status != OpenStatus::Opened) {
                frame->Destroy();
                ExitMainLoop();
                return;
            }
            auto* editor = frame->Editor();
            ::SetPropW(static_cast<HWND>(editor->GetHandle()), L"wxnotepad.comparison", frame->GetHandle());
        });
        return true;
    }
};
} // namespace
wxIMPLEMENT_APP(ComparisonApp);
