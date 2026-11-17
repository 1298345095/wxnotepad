#include "app/editor_frame.h"
#include <utility>
#include "app/localization.h"
#include "editor/editor_control.h"
#include "app/process_observer.h"
#include "app/new_window.h"
#include "settings/settings_store.h"
#include "diagnostics/trace.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <memory>
#include <limits>

namespace {
struct StartupOptions {
    wxString startup;
    wxString observation;
    std::optional<wxString> windowPosition;
    std::optional<EncodingChoice> encoding;
    std::optional<SyntaxLanguage> scheme;
    bool readOnly = false;
    Scintilla::Position line = 0;
    Scintilla::Position column = 0;

    bool ParseEncoding(const wxString& value) {
        long setting = 0;
        if (!value.ToLong(&setting) || setting < std::numeric_limits<int>::min() ||
            setting > std::numeric_limits<int>::max())
            return false;
        encoding = DefaultEncodingChoice(static_cast<int>(setting));
        if (!encoding) return false;
        return true;
    }

    bool ParseScheme(const wxString& value) {
        long setting = 0;
        if (!value.ToLong(&setting) || setting < 0 ||
            setting >= static_cast<int>(std::to_underlying(SyntaxLanguage::Count)))
            return false;
        scheme = static_cast<SyntaxLanguage>(setting);
        return true;
    }

    bool ParseCaret(const wxString& value) {
        const auto parts = wxSplit(value, ',');
        wxLongLong_t parsedLine = 0;
        wxLongLong_t parsedColumn = 0;
        if (parts.size() != 2 || !parts[0].ToLongLong(&parsedLine) || !parts[1].ToLongLong(&parsedColumn) ||
            parsedLine <= 0 || parsedColumn <= 0 || parsedLine > std::numeric_limits<Scintilla::Position>::max() ||
            parsedColumn > std::numeric_limits<Scintilla::Position>::max())
            return false;
        line = static_cast<Scintilla::Position>(parsedLine);
        column = static_cast<Scintilla::Position>(parsedColumn);
        return true;
    }

    bool Parse(int argc, const wxCmdLineArgsArray& argv) {
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
                if (!ParseEncoding(argv[++i])) return false;
            } else if (argument == "--startup-scheme" && i + 1 < argc) {
                if (!ParseScheme(argv[++i])) return false;
            } else if (argument == "--startup-caret" && i + 1 < argc) {
                if (!ParseCaret(argv[++i])) return false;
            } else
                return false;
        }
        return true;
    }
};
} // namespace

class NotepadApp final : public wxApp {
    std::unique_ptr<SettingsStore> settings_;
    std::unique_ptr<ProcessObserver> observer_;

public:
    bool OnInit() override {
        WXNOTE_TRACE("process.start");
        StartupOptions options;
        if (!options.Parse(argc, argv)) return false;
        settings_ = std::make_unique<SettingsStore>();
        AppLocalization::Initialize(settings_->values.uiLanguage);
        auto* frame = new EditorFrame(settings_.get());
        if (options.windowPosition && !ApplyInitialWindowPosition(*frame, *options.windowPosition)) return false;
        if (!options.startup.empty() && frame->OpenRestartDocument(options.startup, options.encoding, options.scheme,
                                                                   options.readOnly, options.line, options.column)
                                                .status != OpenStatus::Opened)
            return false;
        frame->Show();
        frame->Editor()->SetFocus();
        // An external acceptance fixture can observe independently relaunched
        // processes through the same opt-in shown-application seam.
        wxString observationRoot;
        if (options.observation.empty() && wxGetEnv("WXNOTEPAD_OBSERVE_ROOT", &observationRoot) &&
            !observationRoot.empty()) {
            options.observation = wxFileName(observationRoot, wxString::Format("%lu", wxGetProcessId())).GetFullPath();
            if (!wxFileName::Mkdir(options.observation, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) return false;
        }
        if (!options.observation.empty()) observer_ = std::make_unique<ProcessObserver>(*frame, options.observation);
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
