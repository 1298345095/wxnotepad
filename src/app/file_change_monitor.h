#pragma once

#include "settings/file_watching_options.h"
#include <wx/fswatcher.h>
#include <wx/timer.h>
#include <filesystem>
#include <functional>
#include <memory>

class wxWindow;

// Directory notifications are only a signal to compare the current file's
// last-write/size identity, as in the pinned InstallFileWatching/WatchTimerProc.
class FileChangeMonitor final : public wxEvtHandler {
public:
    FileChangeMonitor(wxWindow& owner, std::function<void()> decide);
    ~FileChangeMonitor() override;
    void Refresh(const wxString& path, const FileWatchingOptions& options);
    void Stop();

private:
    struct Identity {
        bool exists = false;
        std::filesystem::file_time_type lastWrite{};
        uintmax_t size = 0;
        bool operator==(const Identity&) const = default;
    };
    Identity ReadIdentity() const;
    void InstallWatch();
    void Check();
    void Decide();
    wxWindow& owner_;
    std::function<void()> decide_;
    std::unique_ptr<wxFileSystemWatcher> watcher_;
    wxTimer checkTimer_;
    wxTimer reloadTimer_;
    std::filesystem::path path_;
    wxString directory_;
    FileWatchingOptions options_;
    Identity baseline_;
    bool notified_ = false;
};
