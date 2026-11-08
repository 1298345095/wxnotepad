#pragma once

enum class FileWatchingMode { None, ShowMessage, AutoReload };

struct FileWatchingOptions {
    FileWatchingMode mode = FileWatchingMode::AutoReload;
    int checkInterval = 1000;
    int autoReloadDelay = 1000;
};
