#include "app/editor_frame.h"
#include "app/dialogs/dialog_layout.h"
#include "app/desktop_launch.h"
#include "app/native_launch.h"
#include "settings/settings_store.h"
#include "settings/recent_files.h"
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/stdpaths.h>
#include <wx/log.h>
#include <wx/utils.h>
#include <algorithm>
#ifdef __WXMSW__
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <gio/gdesktopappinfo.h>
#include <unistd.h>
#endif

namespace {
wxString DefaultProgramDirectory() {
#ifdef __WXMSW__
    PWSTR directory = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &directory))) {
        const wxString result(directory);
        CoTaskMemFree(directory);
        return result;
    }
#else
    const auto desktop = wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Desktop);
    if (!desktop.empty()) return desktop;
#endif
    return wxGetHomeDir();
}

class OpenWithDialog final : public wxDialog {
public:
    OpenWithDialog(wxWindow* parent, wxString& directory)
        // TRANSLATORS: IDD_OPENWITH title; choose a program to open the current document.
        : wxDialog(parent, wxID_ANY, _("Open Document With"), wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          directory_(directory) {
        if (directory_.empty()) directory_ = DefaultProgramDirectory();
        auto* layout = new wxBoxSizer(wxVERTICAL);
        path_ = new wxTextCtrl(this, wxID_ANY, directory_, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
        path_->SetName("open-with-directory");
        programs_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(360, 225)));
        programs_->SetName("open-with-programs");
        layout->Add(programs_, 1, wxEXPAND | wxALL, FromDIP(8));
        // TRANSLATORS: IDD_OPENWITH button selecting the directory containing available programs.
        auto* browse = new wxButton(this, wxID_ANY, _("&Browse Directory..."));
        browse->SetName("open-with-browse");
        auto directoryRow = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
        directoryRow->Add(browse, 0, wxRIGHT, FromDIP(8));
        directoryRow->Add(path_, 1, wxALIGN_CENTER_VERTICAL);
        layout->Add(directoryRow.release(), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
        layout->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(8));
        DialogLayout::Fit(*this, layout, DialogLayout::Resize::Both);
        Fill();
        browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            // TRANSLATORS: IDD_OPENWITH directory browser title.
            wxDirDialog chooser(this, _("Open Document With"), directory_, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
            if (chooser.ShowModal() == wxID_OK) {
                // The original updates OpenWithDir immediately, including on later Cancel.
                directory_ = chooser.GetPath();
                Fill();
            }
        });
        programs_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { UpdateSelection(); });
        programs_->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) {
            if (programs_->GetSelection() != wxNOT_FOUND) EndModal(wxID_OK);
        });
        Bind(
            wxEVT_BUTTON,
            [this](wxCommandEvent&) {
                if (programs_->GetSelection() != wxNOT_FOUND) EndModal(wxID_OK);
            },
            wxID_OK);
        CentreOnParent();
    }
    wxString SelectedProgram() const {
        const auto selected = programs_->GetSelection();
        return selected == wxNOT_FOUND ? wxString{} : paths_[static_cast<size_t>(selected)];
    }

private:
    void UpdateSelection() { FindWindow(wxID_OK)->Enable(programs_->GetSelection() != wxNOT_FOUND); }
    void Fill() {
        path_->ChangeValue(directory_);
        path_->SetToolTip(directory_);
        programs_->Clear();
        paths_.clear();
        wxDir directory(directory_);
        if (!directory.IsOpened()) {
            // TRANSLATORS: IDD_OPENWITH failure reading the selected program directory; no parameters.
            wxMessageBox(_("Could not read the program directory."), _("Open Document With"), wxOK | wxICON_ERROR,
                         this);
            UpdateSelection();
            return;
        }
        wxArrayString names;
        wxString name;
#ifdef __WXMSW__
        constexpr int ProgramEntries = wxDIR_FILES | wxDIR_DIRS;
#else
        constexpr int ProgramEntries = wxDIR_FILES;
#endif
        for (bool found = directory.GetFirst(&name, {}, ProgramEntries); found; found = directory.GetNext(&name)) {
#ifndef __WXMSW__
            const wxFileName file(directory_, name);
            if (file.GetExt() != "desktop" && access(file.GetFullPath().utf8_str(), X_OK) != 0) continue;
            const auto extension = file.GetExt().Lower();
            if (extension == "exe" || extension == "lnk" || extension == "bat" || extension == "cmd") continue;
#endif
            names.push_back(name);
        }
        names.Sort();
        for (const auto& item : names) {
            programs_->Append(item);
            paths_.push_back(wxFileName(directory_, item).GetFullPath());
        }
        UpdateSelection();
        Layout();
    }
    wxString& directory_;
    wxTextCtrl* path_;
    wxListBox* programs_;
    std::vector<wxString> paths_;
};

#ifdef __WXMSW__
wxString DocumentArgument(wxString path) {
    // OpenWithDlg resolves a document shortcut before handing it to the program.
    if (wxFileName(path).GetExt().CmpNoCase("lnk") != 0) return path;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return path;
    IPersistFile* persistent = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&persistent)))) {
        if (SUCCEEDED(persistent->Load(path.wc_str(), STGM_READ))) {
            wchar_t target[32768]{};
            if (SUCCEEDED(link->GetPath(target, static_cast<int>(std::size(target)), nullptr, SLGP_RAWPATH)) &&
                target[0])
                path = target;
        }
        persistent->Release();
    }
    link->Release();
    return path;
}
wxString QuoteArgument(const wxString& argument) {
    wxString result = "\"";
    size_t slashes = 0;
    for (const auto character : argument) {
        if (character == '\\') {
            ++slashes;
            continue;
        }
        result += wxString('\\', slashes * (character == '"' ? 2 : 1));
        slashes = 0;
        if (character == '"') result += '\\';
        result += character;
    }
    result += wxString('\\', slashes * 2);
    return result + "\"";
}
#endif
bool LaunchProgram(wxWindow* parent, const wxString& program, const wxString& document, const wxString& directory,
                   wxString& error) {
#ifdef __WXMSW__
    const auto argument = document.empty() ? wxString{} : QuoteArgument(DocumentArgument(document));
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_FLAG_NO_UI;
    launch.hwnd = static_cast<HWND>(parent->GetHandle());
    launch.lpFile = program.wc_str();
    launch.lpParameters = argument.wc_str();
    launch.lpDirectory = directory.empty() ? nullptr : directory.wc_str();
    launch.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&launch)) return true;
    error = wxSysErrorMsg();
    return false;
#else
    (void)parent;
    if (wxFileName(program).GetExt() == "desktop") {
        GDesktopAppInfo* application = g_desktop_app_info_new_from_filename(program.utf8_str());
        if (!application) {
            // TRANSLATORS: Linux IDM_FILE_OPENWITH error loading a native .desktop program entry; no parameters.
            error = _("Could not load the desktop application entry.");
            return false;
        }
        const bool launched = LaunchDesktopApplication(G_APP_INFO(application), document, directory, error);
        g_object_unref(application);
        return launched;
    }
    std::vector<wxString> arguments{program};
    if (!document.empty()) arguments.push_back(document);
    return NativeLaunch::SpawnArguments(arguments, directory, error);
#endif
}
} // namespace

void EditorFrame::AppendDocumentTools(wxMenu* tools) {
    const int save = wxWindow::NewControlId();
    // TRANSLATORS: SaveBeforeRunningTools preference; ask about unsaved changes before external tool commands.
    tools->AppendCheckItem(save, _("&Save Before Running Tools"));
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Check((settings_ ? settings_->values : defaults_).saveBeforeRunningTools);
        },
        save);
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& preference = (settings_ ? settings_->values : defaults_).saveBeforeRunningTools;
            preference = !preference;
        },
        save);
    tools->AppendSeparator();
    const int restart = wxWindow::NewControlId();
    tools->Append(restart, _("&Restart"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Restart(); }, restart);
    const int openInNewWindow = wxWindow::NewControlId();
    // TRANSLATORS: IDM_FILE_NEWWINDOW2 opens the current disk document in another process.
    tools->Append(openInNewWindow, _("Open &in New Window\tAlt+0"));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenInNewWindow(); }, openInNewWindow);
    tools->AppendSeparator();
    const int execute = wxWindow::NewControlId();
    // TRANSLATORS: IDM_FILE_LAUNCH menu command; open the current disk file with its native association.
    tools->Append(execute, _("Execute &Document\tCtrl+L"));
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(!document_.Path().empty()); }, execute);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ExecuteDocument(); }, execute);
    const int openWith = wxWindow::NewControlId();
    // TRANSLATORS: IDM_FILE_OPENWITH menu command; choose a program for the current document.
    tools->Append(openWith, _("O&pen Document With..."));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenDocumentWith(); }, openWith);
}

bool EditorFrame::ExecuteDocument() {
    if (document_.Path().empty()) return false;
    const auto& preferences = settings_ ? settings_->values : defaults_;
    // TRANSLATORS: IDM_FILE_LAUNCH save confirmation; %s is the document name.
    if (preferences.saveBeforeRunningTools && !ConfirmDiscardChanges(_("Save changes to %s before running tools?")))
        return false;
    wxString error;
    if (LaunchDesktopTarget(this, document_.Path(), wxFileName(document_.Path()).GetPath(), error)) return true;
    // TRANSLATORS: IDM_FILE_LAUNCH error title; the message body contains the native launch error.
    wxMessageBox(error, _("Could not execute document"), wxOK | wxICON_ERROR, this);
    return false;
}

bool EditorFrame::OpenDocumentWith() {
    auto& preferences = settings_ ? settings_->values : defaults_;
    // TRANSLATORS: IDM_FILE_OPENWITH save confirmation; %s is the document name.
    if (preferences.saveBeforeRunningTools && !ConfirmDiscardChanges(_("Save changes to %s before running tools?")))
        return false;
    if (preferences.openWithDirectory.empty()) preferences.openWithDirectory = DefaultProgramDirectory();
    OpenWithDialog chooser(this, preferences.openWithDirectory);
    if (chooser.ShowModal() != wxID_OK) return false;
    const auto directory = document_.Path().empty() ? wxString{} : wxFileName(document_.Path()).GetPath();
    wxString error;
    if (LaunchProgram(this, chooser.SelectedProgram(), document_.Path(), directory, error)) return true;
    // TRANSLATORS: IDM_FILE_OPENWITH error title; the message body contains the native launch error.
    wxMessageBox(error, _("Could not open document with program"), wxOK | wxICON_ERROR, this);
    return false;
}
