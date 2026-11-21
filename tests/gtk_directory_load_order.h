#pragma once

#include <gio/gio.h>
#include <memory>

extern "C" void __wrap_g_file_enumerator_next_files_async(GFileEnumerator*, int, int, GCancellable*,
                                                          GAsyncReadyCallback, gpointer);
extern "C" GList* __wrap_g_file_enumerator_next_files_finish(GFileEnumerator*, GAsyncResult*, GError**);
extern "C" void __wrap_g_file_query_info_async(GFile*, const char*, GFileQueryInfoFlags, int, GCancellable*,
                                               GAsyncReadyCallback, gpointer);

namespace NativeDialogs {
// Owned by the controlled fixture's wxApp. GLib's deferred calls retain their
// context until destroy-notify, independently of application teardown.
class DirectoryLoadOrder final {
public:
    DirectoryLoadOrder();
    DirectoryLoadOrder(const DirectoryLoadOrder&) = delete;
    DirectoryLoadOrder& operator=(const DirectoryLoadOrder&) = delete;
    bool Completed() const;

private:
    struct State;
    std::shared_ptr<State> state_;
    friend void ::__wrap_g_file_enumerator_next_files_async(GFileEnumerator*, int, int, GCancellable*,
                                                            GAsyncReadyCallback, gpointer);
    friend GList* ::__wrap_g_file_enumerator_next_files_finish(GFileEnumerator*, GAsyncResult*, GError**);
    friend void ::__wrap_g_file_query_info_async(GFile*, const char*, GFileQueryInfoFlags, int, GCancellable*,
                                                 GAsyncReadyCallback, gpointer);
};

// The owning wxApp supplies this borrowed lookup for the fixed C linker ABI.
DirectoryLoadOrder* CurrentDirectoryLoadOrder();
} // namespace NativeDialogs
