#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "app/localization.h"
#include "app/native_launch.h"
#include "settings/settings_store.h"
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/translation.h>
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/log.h>
#include <wx/display.h>
#ifdef __WXMSW__
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
#ifdef __WXMSW__
struct CommandParts {
    wxString program;
    wxString arguments;
};
CommandParts SplitProgram(wxString command) {
    // Notepad4 89b8b36 Helpers.cpp::ExtractFirstArgument; see licenses/Notepad4.txt.
    // Original trimming and unquoted separation use ASCII spaces, not tabs.
    command = command.Strip(wxString::both);
    if (command.empty()) return {};
    bool quoted = command[0] == '"';
    size_t start = quoted ? 1 : 0;
    if (!quoted && (command[0] == '-' || command[0] == '/')) {
        for (size_t index = 1; index < command.length() && command[index] != ' '; ++index) {
            if (command[index] == '=' && index + 1 < command.length() && command[index + 1] == '"') {
                start = index + 2;
                quoted = true;
                break;
            }
        }
    }
    const auto end = command.find(quoted ? '"' : ' ', start);
    if (end == wxString::npos) return {command.Mid(command[0] == '"' ? 1 : 0).Strip(wxString::both), {}};
    const auto programStart = command[0] == '"' ? 1 : 0;
    return {command.Mid(programStart, end - programStart).Strip(wxString::both),
            command.Mid(end + 1).Strip(wxString::both)};
}
wxString ExpandEnvironment(const wxString& value) {
    const auto length = ExpandEnvironmentStringsW(value.wc_str(), nullptr, 0);
    if (!length) return value;
    std::vector<wchar_t> expanded(length);
    const auto written = ExpandEnvironmentStringsW(value.wc_str(), expanded.data(), length);
    return written && written <= length ? wxString(expanded.data()) : value;
}
#else
struct NativeCommand {
    std::vector<wxString> arguments;
    wxString error;
    size_t programEnd = wxString::npos;
};
NativeCommand ParseNativeCommand(const wxString& command) {
    // Native argv mapping: quoting groups bytes; expansion never performs word
    // splitting, globbing, redirection, pipelines or command substitution.
    NativeCommand result;
    wxString argument;
    wxUniChar quote = 0;
    bool started = false;
    const auto variableCharacter = [](wxUniChar ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
    };
    for (size_t index = 0; index < command.length(); ++index) {
        const auto ch = command[index];
        if (!quote && (ch == ' ' || ch == '\t' || ch == '\n')) {
            if (started) {
                if (result.arguments.empty()) result.programEnd = index;
                result.arguments.push_back(std::exchange(argument, {}));
            }
            started = false;
            continue;
        }
        started = true;
        if (ch == quote) {
            quote = 0;
            continue;
        }
        if (!quote && (ch == '\'' || ch == '"')) {
            quote = ch;
            continue;
        }
        if (ch == '\\' && quote != '\'') {
            if (index + 1 == command.length()) {
                // TRANSLATORS: Linux IDD_RUN parser error; a trailing backslash has no following character.
                result.error = _("The command ends with an unfinished escape.");
                return result;
            }
            const auto next = command[index + 1];
            if (!quote || next == '"' || next == '\\' || next == '$' || next == '\n') {
                ++index;
                if (next != '\n') argument += next;
                continue;
            }
        }
        if (ch == '$' && quote != '\'' && index + 1 < command.length()) {
            size_t end = index + 1;
            const bool braced = command[end] == '{';
            if (braced) ++end;
            const size_t start = end;
            while (end < command.length() && variableCharacter(command[end]))
                ++end;
            if (end != start && (!braced || (end < command.length() && command[end] == '}'))) {
                wxString value;
                wxGetEnv(command.Mid(start, end - start), &value);
                argument += value;
                index = braced ? end : end - 1;
                continue;
            }
        }
        argument += ch;
    }
    if (quote)
        // TRANSLATORS: Linux IDD_RUN parser error; a single or double quote was not closed.
        result.error = _("The command contains an unfinished quote.");
    else if (started)
        result.arguments.push_back(argument);
    return result;
}
#endif
wxString QuoteProgram(wxString program) {
#ifdef __WXMSW__
    return program.Contains(' ') ? "\"" + program + "\"" : program;
#else
    program.Replace("'", "'\\''");
    return "'" + program + "'";
#endif
}
void BrowseCommand(wxDialog& parent, wxTextCtrl& input) {
    wxString program;
    wxString arguments;
#ifdef __WXMSW__
    const auto parts = SplitProgram(input.GetValue());
    program = ExpandEnvironment(parts.program);
    arguments = ExpandEnvironment(parts.arguments);
    // TRANSLATORS: IDD_RUN program browser filter; preserve wildcard patterns and vertical-bar separators.
    const wxString filter = _("Programs (*.exe;*.com;*.bat;*.cmd)|*.exe;*.com;*.bat;*.cmd|All files (*.*)|*.*");
#else
    const auto parsed = ParseNativeCommand(input.GetValue());
    if (!parsed.arguments.empty()) program = parsed.arguments.front();
    if (parsed.programEnd != wxString::npos)
        arguments = input.GetValue().Mid(parsed.programEnd).Strip(wxString::leading);
    // TRANSLATORS: Linux IDD_RUN program browser filter; preserve the wildcard and vertical-bar separator.
    const wxString filter = _("All files (*)|*");
#endif
    const wxFileName initial(program);
    // TRANSLATORS: IDD_RUN Browse action; title of the native executable chooser.
    wxFileDialog browser(&parent, _("Choose program"), initial.GetPath(), initial.GetFullName(), filter,
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (browser.ShowModal() == wxID_OK) {
        auto command = QuoteProgram(browser.GetPath());
        if (!arguments.empty()) command += " " + arguments;
        input.SetValue(command);
    }
    input.SetFocus();
}
bool HasProgram(const wxString& command) {
#ifdef __WXMSW__
    return !SplitProgram(command).program.empty();
#else
    const auto parsed = ParseNativeCommand(command);
    return !parsed.arguments.empty() && !parsed.arguments.front().empty();
#endif
}
bool IsSelfAlias(const wxString& program) {
    return program.CmpNoCase("Notepad4") == 0 || program.CmpNoCase("Notepad4.exe") == 0 ||
           program.CmpNoCase("wxnotepad") == 0 || program.CmpNoCase("wxnotepad.exe") == 0;
}
bool LaunchCommand(wxDialog& dialog, const wxString& command, const wxString& directory, wxString& error) {
#ifdef __WXMSW__
    auto parts = SplitProgram(command);
    parts.arguments = ExpandEnvironment(parts.arguments);
    const bool self = IsSelfAlias(parts.program);
    if (self) parts.program = wxStandardPaths::Get().GetExecutablePath();
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_DOENVSUBST | SEE_MASK_FLAG_NO_UI;
    launch.hwnd = static_cast<HWND>(dialog.GetHandle());
    launch.lpFile = parts.program.wc_str();
    launch.lpParameters = parts.arguments.wc_str();
    launch.lpDirectory = directory.wc_str();
    launch.nShow = SW_SHOWNORMAL;
    if (self) {
        // Original quick-exit branch closes before launching the current binary.
        launch.fMask |= SEE_MASK_NOZONECHECKS;
        dialog.EndModal(wxID_OK);
    }
    const bool launched = ShellExecuteExW(&launch) != FALSE;
    if (!launched) error = wxSysErrorMsg();
    return launched || self;
#else
    auto parsed = ParseNativeCommand(command);
    if (!parsed.error.empty()) {
        error = parsed.error;
        return false;
    }
    if (parsed.arguments.empty() || parsed.arguments.front().empty()) return false;
    const bool self = IsSelfAlias(parsed.arguments.front());
    if (self) {
        parsed.arguments.front() = wxStandardPaths::Get().GetExecutablePath();
        dialog.EndModal(wxID_OK);
    }
    const bool launched = NativeLaunch::SpawnArguments(parsed.arguments, directory, error);
    return launched || self;
#endif
}
} // namespace

void EditorFrame::AppendRunCommand(wxMenu* tools) {
    const int id = wxWindow::NewControlId();
    // TRANSLATORS: IDD_RUN menu command; retain the tab-separated keyboard shortcut.
    tools->Append(id, _("&Run Command...\tCtrl+R"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowRunCommand(); }, id);
}

void EditorFrame::ShowRunCommand() {
    auto& preferences = settings_ ? settings_->values : defaults_;
    // TRANSLATORS: SaveBeforeRunningTools confirmation shared with IDM_FILE_LAUNCH/OPENWITH; %s is the document name.
    if (preferences.saveBeforeRunningTools && !ConfirmDiscardChanges(_("Save changes to %s before running tools?")))
        return;
    // TRANSLATORS: IDD_RUN dialog title.
    wxDialog dialog(this, wxID_ANY, _("Run Command"), wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* contents = new wxBoxSizer(wxVERTICAL);
    // TRANSLATORS: IDD_RUN command input label; the field accepts a program and its arguments.
    contents->Add(new wxStaticText(&dialog, wxID_ANY, _("&Command:")), 0, wxALL, dialog.FromDIP(10));
    const wxString initial = document_.Path().empty() ? wxString{} : QuoteProgram(document_.Path());
    auto* input = new wxTextCtrl(&dialog, wxID_ANY, initial, wxDefaultPosition, dialog.FromDIP(wxSize(430, -1)));
    input->SetName("Command");
    // TRANSLATORS: IDD_RUN command input accessible name, matching its visible label.
    AppLocalization::SetAccessibleName(*input, wxControl::GetLabelText(_("&Command:")));
    auto* commandRow = new wxBoxSizer(wxHORIZONTAL);
    commandRow->Add(input, 1, wxEXPAND);
    // TRANSLATORS: IDD_RUN button opening the native program chooser.
    auto* browse = new wxButton(&dialog, wxID_ANY, _("&Browse..."));
    browse->SetName("Browse programs");
    commandRow->Add(browse, 0, wxLEFT, dialog.FromDIP(8));
    browse->Bind(wxEVT_BUTTON, [&dialog, input](wxCommandEvent&) { BrowseCommand(dialog, *input); });
    contents->Add(commandRow, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    auto* run = wxDynamicCast(dialog.FindWindow(wxID_OK), wxButton);
    // TRANSLATORS: IDD_RUN affirmative button that launches the entered command.
    run->SetLabel(_("&Run"));
    auto* feedback = new wxStaticText(&dialog, wxID_ANY, "");
    feedback->SetName("Run command feedback");
    contents->Insert(2, feedback, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
    const auto update = [input, run] { run->Enable(HasProgram(input->GetValue())); };
    input->Bind(wxEVT_TEXT, [update](wxCommandEvent&) { update(); });
    update();
    dialog.SetSizerAndFit(contents);
    const auto minimum = dialog.GetSize();
    dialog.SetMinSize(minimum);
    dialog.SetMaxSize(wxSize(-1, minimum.y));
    if (preferences.runCommandWidthDIP > 0)
        dialog.SetSize(wxSize(std::max(minimum.x, dialog.FromDIP(preferences.runCommandWidthDIP)), minimum.y));
    else if (settings_) {
        const int displayIndex = wxDisplay::GetFromWindow(this);
        const auto screen =
            wxDisplay(displayIndex == wxNOT_FOUND ? 0 : static_cast<unsigned>(displayIndex)).GetGeometry().GetSize();
        const int original = settings_->OriginalRunCommandWidth(screen.x, screen.y);
        if (original > 0) {
            // ResizeDlg_InitEx/WM_DESTROY stores client width in dialog units
            // (MapDialogRect({4,8})); convert with the native dialog font/DPI.
            const int nativeClient = dialog.ConvertDialogToPixels(wxSize(original, 0)).x;
            dialog.SetClientSize(wxSize(std::max(dialog.GetClientSize().x, nativeClient), dialog.GetClientSize().y));
        }
    }
    input->SetSelection(-1, -1);
    input->SetFocus();
    dialog.CentreOnParent();
    const auto directory = document_.Path().empty() ? launchWorkingDirectory_ : wxFileName(document_.Path()).GetPath();
    dialog.Bind(
        wxEVT_BUTTON,
        [&dialog, input, feedback, directory](wxCommandEvent&) {
            if (!HasProgram(input->GetValue())) return;
            wxString error;
            if (LaunchCommand(dialog, input->GetValue(), directory, error)) {
                if (dialog.IsShown()) dialog.EndModal(wxID_OK);
            } else {
                // TRANSLATORS: IDD_RUN launch failure; %s is the native or command-parser error.
                feedback->SetLabel(wxString::Format(_("Could not run command: %s"), error));
                dialog.GetSizer()->Fit(&dialog);
                input->SetFocus();
            }
        },
        wxID_OK);
    dialog.ShowModal();
    preferences.runCommandWidthDIP = dialog.GetSize().x > minimum.x ? dialog.ToDIP(dialog.GetSize().x) : 0;
    editor_->SetFocus();
}
