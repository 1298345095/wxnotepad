#include "app/file_change_monitor.h"
#include <wx/filename.h>
#include <wx/window.h>
#include <wx/evtloop.h>
#include <algorithm>

FileChangeMonitor::FileChangeMonitor(wxWindow& owner, std::function<void()> decide)
    : owner_(owner), decide_(std::move(decide)), checkTimer_(this), reloadTimer_(this) {
    Bind(wxEVT_FSWATCHER, [this](wxFileSystemWatcherEvent&) { notified_ = true; });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Check(); }, checkTimer_.GetId());
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Decide(); }, reloadTimer_.GetId());
}

FileChangeMonitor::~FileChangeMonitor() {
    Stop();
}

void FileChangeMonitor::Stop() {
    checkTimer_.Stop();
    reloadTimer_.Stop();
    if (watcher_) watcher_->RemoveAll();
    notified_ = false;
}

void FileChangeMonitor::Refresh(const wxString& path, const FileWatchingOptions& options) {
    Stop();
    options_ = options;
    if (path.empty() || options.mode == FileWatchingMode::None) return;
#ifdef __WXMSW__
    path_ = path.ToStdWstring();
#else
    path_ = path.ToStdString(wxConvUTF8);
#endif
    baseline_ = ReadIdentity();
    directory_ = wxFileName(path).GetPath();
    // Startup Open runs in wxApp::OnInit. Inotify needs an active event loop;
    // the first timer check installs it once that loop runs.
    if (wxEventLoopBase::GetActive()) InstallWatch();
    checkTimer_.Start(std::max(options_.checkInterval, 1));
}

void FileChangeMonitor::InstallWatch() {
    if (!watcher_) {
        watcher_ = std::make_unique<wxFileSystemWatcher>();
        watcher_->SetOwner(this);
    }
    watcher_->Add(wxFileName::DirName(directory_), wxFSW_EVENT_CREATE | wxFSW_EVENT_DELETE | wxFSW_EVENT_RENAME |
                                                       wxFSW_EVENT_MODIFY | wxFSW_EVENT_ATTRIB);
    notified_ = true;
}

FileChangeMonitor::Identity FileChangeMonitor::ReadIdentity() const {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path_, error)) return {};
    const auto lastWrite = std::filesystem::last_write_time(path_, error);
    if (error) return {};
    const auto size = std::filesystem::file_size(path_, error);
    return error ? Identity{} : Identity{true, lastWrite, size};
}

void FileChangeMonitor::Check() {
    if (!watcher_) InstallWatch();
    if (!notified_) return;
    notified_ = false;
    if (ReadIdentity() == baseline_) return;
    // One change owns one pending decision. Further writes are read together
    // after the original fixed delay, rather than restarting that delay.
    checkTimer_.Stop();
    watcher_->RemoveAll();
    if (options_.mode == FileWatchingMode::AutoReload)
        reloadTimer_.StartOnce(std::max(options_.autoReloadDelay, 1));
    else
        Decide();
}

void FileChangeMonitor::Decide() {
    if (owner_.IsBeingDeleted()) return;
    // A file dialog/save confirmation already owns this window. Retain the
    // pending change until it returns, rather than nesting another decision.
    if (!owner_.IsEnabled()) {
        reloadTimer_.StartOnce(std::max(options_.checkInterval, 1));
        return;
    }
    decide_();
}
