#include "app/editor_frame.h"
#include "app/localization.h"
#include "editor/editor_control.h"
#include "app/process_observer.h"
#include "app/new_window.h"
#include "settings/settings_store.h"
#include "diagnostics/trace.h"
#include <wx/wx.h>
#include <memory>
#include <limits>

class NotepadApp final : public wxApp {
    std::unique_ptr<SettingsStore> settings_;
    std::unique_ptr<ProcessObserver> observer_;

public:
    bool OnInit() override {
        WXNOTE_TRACE("process.start");
        wxString startup;
        wxString observation;
        std::optional<wxString> windowPosition;
        std::optional<EncodingChoice> startupEncoding;
        std::optional<SyntaxLanguage> startupScheme;
        bool readOnly = false;
        Scintilla::Position line = 0;
        Scintilla::Position column = 0;
        for (int i = 1; i < argc; ++i) {
            const wxString argument(argv[i]);
            if (argument == "--observe" && i + 1 < argc)
                observation = argv[++i];
            else if (argument == "--startup-file" && i + 1 < argc)
                startup = argv[++i];
            else if (argument == "--window-position" && i + 1 < argc)
                windowPosition = argv[++i];
            else if (argument == "--read-only")
                readOnly = true;
            else if (argument == "--startup-encoding" && i + 1 < argc) {
                long setting = 0;
                if (!wxString(argv[++i]).ToLong(&setting) || setting < std::numeric_limits<int>::min() ||
                    setting > std::numeric_limits<int>::max())
                    return false;
                startupEncoding = DefaultEncodingChoice(static_cast<int>(setting));
                if (!startupEncoding) return false;
            } else if (argument == "--startup-scheme" && i + 1 < argc) {
                long scheme = 0;
                if (!wxString(argv[++i]).ToLong(&scheme) || scheme < 0 ||
                    scheme >= static_cast<int>(SyntaxLanguage::Count))
                    return false;
                startupScheme = static_cast<SyntaxLanguage>(scheme);
            } else if (argument == "--startup-caret" && i + 1 < argc) {
                const auto parts = wxSplit(argv[++i], ',');
                wxLongLong_t parsedLine = 0;
                wxLongLong_t parsedColumn = 0;
                if (parts.size() != 2 || !parts[0].ToLongLong(&parsedLine) || !parts[1].ToLongLong(&parsedColumn) ||
                    parsedLine <= 0 || parsedColumn <= 0 ||
                    parsedLine > std::numeric_limits<Scintilla::Position>::max() ||
                    parsedColumn > std::numeric_limits<Scintilla::Position>::max())
                    return false;
                line = static_cast<Scintilla::Position>(parsedLine);
                column = static_cast<Scintilla::Position>(parsedColumn);
            } else
                return false;
        }
        settings_ = std::make_unique<SettingsStore>();
        AppLocalization::Initialize(settings_->values.uiLanguage);
        auto* frame = new EditorFrame(settings_.get());
        if (windowPosition && !ApplyInitialWindowPosition(*frame, *windowPosition)) return false;
        if (!startup.empty() &&
            frame->OpenRestartDocument(startup, startupEncoding, startupScheme, readOnly, line, column).status !=
                OpenStatus::Opened)
            return false;
        frame->Show();
        frame->Editor()->SetFocus();
        if (!observation.empty()) observer_ = std::make_unique<ProcessObserver>(*frame, observation);
        return true;
    }
#ifdef WXNOTEPAD_DIAGNOSTICS
    int OnExit() override {
        const int result = wxApp::OnExit();
        WXNOTE_TRACE("application.exit", "\tstatus=%d", result);
        return result;
    }
#endif
};

wxIMPLEMENT_APP(NotepadApp);
