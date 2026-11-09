#pragma once

enum class FileWatchingMode { None, ShowMessage, AutoReload };
enum FileWatchingOption {
    FileWatchingOption_None = 0,
    FileWatchingOption_LogFile = 1,
    FileWatchingOption_KeepAtEnd = 2
};

struct FileWatchingOptions {
    FileWatchingMode mode = FileWatchingMode::AutoReload;
    int checkInterval = 1000;
    int autoReloadDelay = 1000;
    int flags = FileWatchingOption_None;
    bool resetOnNewFile = false;
};
