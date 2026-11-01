#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "app/process_observer.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <memory>

class NotepadApp final : public wxApp {
    std::unique_ptr<SettingsStore> settings_;
    std::unique_ptr<ProcessObserver> observer_;

public:
    bool OnInit() override {
        wxString startup;
        wxString observation;
        for (int i = 1; i < argc; ++i) {
            const wxString argument(argv[i]);
            if (argument == "--observe" && i + 1 < argc)
                observation = argv[++i];
            else if (argument == "--startup-file" && i + 1 < argc)
                startup = argv[++i];
            else
                return false;
        }
        settings_ = std::make_unique<SettingsStore>();
        auto* frame = new EditorFrame(settings_.get());
        if (!startup.empty() && frame->Open(startup).status != OpenStatus::Opened) return false;
        frame->Show();
        frame->Editor()->SetFocus();
        if (!observation.empty()) observer_ = std::make_unique<ProcessObserver>(*frame, observation);
        return true;
    }
};

wxIMPLEMENT_APP(NotepadApp);
