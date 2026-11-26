#include "app/editor_frame.h"
#include <utility>
#include "app/localization.h"
#include "editor/editor_control.h"
#include "editor/go_to_location.h"
#include "app/process_observer.h"
#include "app/new_window.h"
#include "app/window_routing.h"
#include "settings/settings_store.h"
#include "diagnostics/trace.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <memory>
#include <limits>
#include <array>
#include <cwchar>
#include <algorithm>

namespace {
struct StartupOptions : WindowRequest {
    wxString observation;
    std::optional<wxString> windowPosition;
    wxString schemeName;
    std::optional<unsigned> unsupportedSourceCodePage;
    wxString missingParameter;
    bool help = false;
    std::optional<std::pair<bool, bool>> routing;

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

    bool ParseInitialLocation(const wxString& value, bool displayColumns) {
        // Pinned Helpers.cpp::ParseCommaList64 accepts decimal prefixes and up
        // to three numbers, saturating overflows through the CRT conversion.
        const auto text = value.ToStdWstring();
        const wchar_t* current = text.c_str();
        std::array<intptr_t, 3> values{};
        size_t count = 0;
        for (; count < values.size(); ++count) {
            wchar_t* end = nullptr;
            values[count] = static_cast<intptr_t>(std::wcstoll(current, &end, 10));
            if (end == current) break;
            current = *end == L',' ? end + 1 : end;
        }
        if (!count) return false;
        constexpr unsigned MaximumJumpTabWidth = 256; // Pinned EditLexer.h::TAB_WIDTH_MAX.
        constexpr unsigned JumpTabWidthShift = 4;
        const auto type = displayColumns
                              ? std::min(static_cast<unsigned>(values[2]), MaximumJumpTabWidth) << JumpTabWidthShift
                              : static_cast<unsigned>(std::to_underlying(Scintilla::ColumnType::Character)) +
                                    static_cast<unsigned>(values[2] & 1);
        initialLocation = InitialLocation{values[0], values[1], type};
        return true;
    }

    bool Parse(int argc, const wxCmdLineArgsArray& argv) {
        int i = 1;
        while (i < argc) {
            const wxString argument(argv[i++]);
            wxString option;
            if (argument.StartsWith("--"))
                option = argument.Mid(2);
            else if (argument.StartsWith("-"))
                option = argument.Mid(1);
#ifdef __WXMSW__
            else if (argument.StartsWith("/"))
                option = argument.Mid(1);
#endif
            if ((option.CmpNoCase("e") == 0 || option.CmpNoCase("s") == 0 || option == "g" || option == "G") &&
                i == argc) {
                missingParameter = argument;
                return false;
            }
            if (argument == "-" || argument == "--") {
                if (i == argc || !startup.empty()) return false;
                startup = argv[i++];
                ordinaryFile = true;
                while (i < argc)
                    startup += " " + wxString(argv[i++]);
            } else if (argument == "--observe" && i < argc)
                observation = argv[i++];
            else if (argument == "--startup-file" && i < argc)
                startup = argv[i++];
            else if (argument == "--window-position" && i < argc)
                windowPosition = argv[i++];
            else if (argument == "--read-only")
                readOnly = true;
            else if (argument == "--startup-encoding" && i < argc) {
                if (!ParseEncoding(argv[i++])) return false;
            } else if (argument == "--startup-scheme" && i < argc) {
                if (!ParseScheme(argv[i++])) return false;
            } else if (argument == "--startup-caret" && i < argc) {
                if (!ParseCaret(argv[i++])) return false;
            } else if (option.CmpNoCase("e") == 0 && i < argc) {
                const auto detected = MatchSourceEncoding(wxString(argv[i++]).utf8_string());
                unsupportedSourceCodePage = detected.unsupportedCodePage;
                if (unsupportedSourceCodePage) return false;
                source = detected.systemDefault ? DocumentLoad::SourceDecoding(DocumentLoad::NativeAnsiSource{})
                         : detected.encoding    ? DocumentLoad::SourceDecoding(EncodingChoice(
                                                      *detected.encoding, detected.bom, false, detected.systemOem))
                                                : DocumentLoad::SourceDecoding{};
            } else if (option.CmpNoCase("s") == 0 && i < argc) {
                schemeName = argv[i++];
            } else if (option.CmpNoCase("n") == 0 || option.CmpNoCase("ns") == 0 || option.CmpNoCase("r") == 0 ||
                       option.CmpNoCase("rs") == 0) {
                routing = std::pair{option.Lower().StartsWith("r"), option.length() == 2};
            } else if (option.CmpNoCase("ro") == 0) {
                readOnly = true;
            } else if (option == "?") {
                help = true;
            } else if ((option == "g" || option == "G") && i < argc) {
                if (ParseInitialLocation(argv[i], option == "G")) {
                    ++i;
                } else {
                    // Original nonnumeric g/G operands fall back to the whole
                    // remaining pathname, including the option's prefix.
                    startup = argument;
                    ordinaryFile = true;
                    while (i < argc)
                        startup += " " + wxString(argv[i++]);
                }
            } else {
                if (!startup.empty() || argument.StartsWith("-")) return false;
                startup = argument;
                ordinaryFile = true;
                // The original single-file mode treats the remaining command
                // line as one pathname, including unquoted spaces.
                while (i < argc)
                    startup += " " + wxString(argv[i++]);
            }
        }
        return true;
    }
};

std::optional<SyntaxLanguage> StartupScheme(const wxString& name, const SyntaxExtensions& extensions) {
    // Original Style_SetLexerFromName: extension first, then a scheme-name
    // prefix of at least three characters. An unmatched name keeps detection.
    for (const auto& definition : SyntaxCatalog()) {
        if (definition.language != SyntaxSettingsLanguage(definition.language)) continue;
        for (const auto& extension : SyntaxExtensionList(definition.language, extensions)) {
            if (extension.CmpNoCase(name) == 0) return definition.language;
        }
    }
    if (name.length() >= 3) {
        for (const auto& definition : SyntaxCatalog()) {
            if (wxString::FromUTF8(SyntaxSettingsSection(definition.language)).Lower().StartsWith(name.Lower()))
                return SyntaxSettingsLanguage(definition.language);
        }
    }
    return std::nullopt;
}

wxString StartupHelp() {
    // TRANSLATORS: New wxnotepad command-line help. Keep literal switches -e, -s, -g, -G, -ro, -? and
    // encoding aliases ANSI/OEM unchanged; bracketed operands describe user-supplied values.
    wxString text = _("Usage: wxnotepad [options] [file]\n\n"
                      "-e encoding  Source encoding (original aliases, including ANSI and OEM).\n"
                      "-s scheme  Syntax scheme extension or name.\n"
                      "-g line[,character[,bytes]]  Character position; an odd third value counts bytes.\n"
                      "-G line[,column[,tab-width]]  Display column; optional tab width (maximum 256).\n"
                      "-ro  Read-only editing mode; disk permissions stay unchanged.\n"
                      "-?  Show this help.\n\n"
                      "Positions are one-based. A negative line jumps to the end; a negative column counts characters "
                      "without expanding tabs.");
    // TRANSLATORS: New wxnotepad help for original LoadFlags; keep -n, -ns, -r and -rs unchanged.
    text += "\n\n" + _("-n  Always open a new window.\n"
                       "-ns  Match an existing window only for the same file.\n"
                       "-r  Reuse an available window.\n"
                       "-rs  Reuse without a file; otherwise match only the same file.");
#ifdef __WXMSW__
    // TRANSLATORS: New wxnotepad help; /option, -option and --option show literal option-prefix spellings.
    text += "\n\n" + _("Windows: /option, -option and --option.");
#else
    // TRANSLATORS: New wxnotepad help; -option and --option are option spellings, / is the literal path prefix.
    text += "\n\n" + _("Linux: -option and --option; / starts an absolute file path.");
#endif
    // TRANSLATORS: New wxnotepad help; preserve the literal option terminators - and --.
    text += "\n" + _("Use - or -- to end options and open the remaining single file path.");
    return text;
}
} // namespace

class NotepadApp final : public wxApp {
    std::unique_ptr<SettingsStore> settings_;
    std::unique_ptr<ProcessObserver> observer_;
    std::unique_ptr<WindowRouting> routing_;
    std::optional<int> startupExitCode_;
    wxString desktopStartupId_;
    wxString waylandActivationToken_;

public:
    bool Initialize(int& count, wxChar** arguments) override {
#ifdef __WXGTK__
        // GDK stashes DESKTOP_STARTUP_ID and unsets it during initialization.
        wxGetEnv("DESKTOP_STARTUP_ID", &desktopStartupId_);
        wxGetEnv("XDG_ACTIVATION_TOKEN", &waylandActivationToken_);
        wxUnsetEnv("XDG_ACTIVATION_TOKEN");
#endif
        return wxApp::Initialize(count, arguments);
    }
    bool OnInit() override {
        WXNOTE_TRACE("process.start");
        StartupOptions options;
        const bool parsed = options.Parse(argc, argv);
        if (!parsed && !options.unsupportedSourceCodePage && options.missingParameter.empty()) return false;
        settings_ = std::make_unique<SettingsStore>();
        AppLocalization::Initialize(settings_->values.uiLanguage);
        if (!options.missingParameter.empty()) {
            // TRANSLATORS: New wxnotepad missing-operand error; %s is the literal option token, such as /s or -e.
            wxMessageBox(wxString::Format(_("Option \"%s\" requires a parameter."), options.missingParameter),
                         // TRANSLATORS: New wxnotepad title for an invalid command-line option operand.
                         _("Command Line Error"), wxOK | wxICON_ERROR);
            startupExitCode_ = 2;
            return true;
        }
        if (options.unsupportedSourceCodePage) {
            wxMessageBox(
                wxString::Format(
                    // TRANSLATORS: New wxnotepad unsupported-source-encoding error; %u is its numeric code page.
                    _("The selected encoding (code page %u) is not migrated. The document was not changed."),
                    *options.unsupportedSourceCodePage),
                // TRANSLATORS: wxnotepad title for failure to load the requested file.
                _("Could not open file"), wxOK | wxICON_ERROR);
            startupExitCode_ = 2;
            return true;
        }
        if (options.help) {
            // TRANSLATORS: New wxnotepad title for the delivered command-line help.
            wxMessageBox(StartupHelp(), _("Command Line Help"), wxOK | wxICON_INFORMATION);
            startupExitCode_ = 0;
            return true;
        }
        SetAppearance(Appearance::System);
        options.startupId = WindowRouting::StartupId(desktopStartupId_, waylandActivationToken_);
        WindowRouting::SetStartupId(options.startupId);
        desktopStartupId_.clear();
        waylandActivationToken_.clear();
        if (!options.startup.empty()) {
            wxFileName path(options.startup);
            path.Normalize(wxPATH_NORM_ENV_VARS | wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG |
                           wxPATH_NORM_SHORTCUT);
            options.startup = path.GetFullPath();
        }
        if (!options.schemeName.empty())
            options.scheme = StartupScheme(options.schemeName, settings_->values.schemeExtensions);
        const auto [reuse, single] =
            options.routing.value_or(std::pair{settings_->values.reuseWindow, settings_->values.singleFileInstance});
        const auto routed = WindowRouting::Route(options, reuse, single);
        if (routed != WindowRouting::Result::NewWindow) {
            startupExitCode_ = routed == WindowRouting::Result::Handled ? 0 : 2;
            return true;
        }
        auto* frame = new EditorFrame(settings_.get());
        if (options.windowPosition && !ApplyInitialWindowPosition(*frame, *options.windowPosition)) return false;
        bool skipLoad = options.startup.empty();
        if (options.ordinaryFile) {
            // As in the original, present the owned editor before a startup
            // file confirmation. Internal relaunch failures keep their contract.
            frame->Show();
            wxFileName path(options.startup);
            path.Normalize(wxPATH_NORM_ENV_VARS | wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG |
                           wxPATH_NORM_SHORTCUT);
            if (!path.FileExists() && !wxFileName::DirExists(path.GetFullPath())) {
                const int choice = wxMessageBox(
                    // TRANSLATORS: Original IDS_ASK_CREATE; %s is the literal missing-file pathname.
                    wxString::Format(_("\"%s\" not found.\nWould you like to create this file?"), path.GetFullPath()),
                    // TRANSLATORS: New wxnotepad title for the original IDS_ASK_CREATE confirmation.
                    _("Create file?"), wxYES_NO | wxCANCEL | wxICON_QUESTION, frame);
                if (choice == wxCANCEL) {
                    frame->Destroy();
                    startupExitCode_ = 0;
                    return true;
                }
                skipLoad = choice != wxYES;
                if (!skipLoad) {
                    bool created;
                    {
                        const wxLogNull quiet;
                        wxFile file;
                        created = file.Create(path.GetFullPath(), false) && file.Close();
                    }
                    if (!created) {
                        // TRANSLATORS: New wxnotepad file-creation failure in the original IDS_ASK_CREATE workflow.
                        wxMessageBox(_("Could not create the file."),
                                     // TRANSLATORS: wxnotepad title for failure to load or create the requested file.
                                     _("Could not open file"), wxOK | wxICON_ERROR, frame);
                        skipLoad = true;
                    } else if (std::holds_alternative<DocumentLoad::NativeAnsiSource>(options.source)) {
                        // Original FileLoad's Create branch initializes ANSI
                        // directly; an existing empty file goes through recoding.
                        if (const auto native = NativeEncodingChoice()) options.source = *native;
                    }
                }
            }
        }
        bool opened = false;
        if (!skipLoad) {
            const auto result =
                frame->OpenRestartDocument(options.startup, options.encoding, options.scheme, options.readOnly,
                                           options.line, options.column, options.source);
            opened = result.status == OpenStatus::Opened;
            if (!opened) {
                if (!options.ordinaryFile) return false;
                if (result.status == OpenStatus::Failed)
                    // TRANSLATORS: wxnotepad title for failure to load the requested file.
                    wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, frame);
            }
        }
        if (!opened && !options.schemeName.empty() && options.scheme)
            frame->OpenRestartDocument({}, std::nullopt, options.scheme, false, 0, 0);
        if (options.initialLocation && opened) GoToInitialLocation(*frame->Editor(), *options.initialLocation);
        frame->Show();
        frame->Editor()->SetFocus();
        routing_ = std::make_unique<WindowRouting>(*frame);
        if (!routing_->IsOk()) return false;
        // An external acceptance fixture can observe independently relaunched
        // processes through the same opt-in shown-application seam.
        if (wxString observationRoot; options.observation.empty() &&
                                      wxGetEnv("WXNOTEPAD_OBSERVE_ROOT", &observationRoot) &&
                                      !observationRoot.empty()) {
            options.observation = wxFileName(observationRoot, wxString::Format("%lu", wxGetProcessId())).GetFullPath();
            if (!wxFileName::Mkdir(options.observation, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) return false;
        }
        if (!options.observation.empty()) observer_ = std::make_unique<ProcessObserver>(*frame, options.observation);
        return true;
    }
    int OnRun() override { return startupExitCode_ ? *startupExitCode_ : wxApp::OnRun(); }
#ifdef WXNOTEPAD_DIAGNOSTICS
    int OnExit() override {
        const int result = wxApp::OnExit();
        WXNOTE_TRACE("application.exit", "\tstatus=%d", result);
        return result;
    }
#endif
};

wxIMPLEMENT_APP(NotepadApp);
