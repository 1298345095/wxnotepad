#include "gtk_directory_load_order.h"
#include "native_dialog_helpers.h"
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <functional>
#include <memory>
#include <string>

namespace {
constexpr unsigned InitialEnumerationDelayMilliseconds = 200;
std::string initialDirectory;
std::string requestedParent;
bool enumerationQueued = false;
bool enumerationDelivered = false;
bool enumerationFinished = false;
bool parentQueued = false;
bool parentDelivered = false;
std::function<void()> deferredParent;
std::shared_ptr<GFileEnumerator> controlledEnumerator;

template <typename T> std::shared_ptr<T> Retain(T* object) {
    return {object ? static_cast<T*>(g_object_ref(object)) : nullptr, [](T* value) {
                if (value) g_object_unref(value);
            }};
}
void Defer(unsigned milliseconds, std::function<void()> action) {
    auto pending = std::make_unique<std::function<void()>>(std::move(action));
    g_timeout_add_full(
        G_PRIORITY_DEFAULT, milliseconds,
        [](gpointer data) -> gboolean {
            (*static_cast<std::function<void()>*>(data))();
            return G_SOURCE_REMOVE;
        },
        pending.release(), [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
}
bool Matches(GFile* file, const std::string& expected) {
    if (expected.empty()) return false;
    char* path = g_file_get_path(file);
    const bool matches = path && path == expected;
    g_free(path);
    return matches;
}
GtkWidget* ChooserWidget(GtkWidget* widget) {
    if (GTK_IS_FILE_CHOOSER_WIDGET(widget)) return widget;
    if (!GTK_IS_CONTAINER(widget)) return nullptr;
    GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget* chooser = nullptr;
    for (GList* child = children; child && !chooser; child = child->next)
        chooser = ChooserWidget(GTK_WIDGET(child->data));
    g_list_free(children);
    return chooser;
}
bool VisibleChooserModel(gpointer model) {
    // In pinned GTK3, gtk_file_system_model_got_enumerator passes its model as
    // the next-files callback data. Its finished-loading handler identifies the
    // owning chooser. wxDirDialog also builds a hidden fallback: do not delay it.
    GList* windows = gtk_window_list_toplevels();
    bool visible = false;
    for (GList* item = windows; item; item = item->next) {
        auto* window = GTK_WIDGET(item->data);
        if (!gtk_widget_get_visible(window)) continue;
        auto* chooser = ChooserWidget(window);
        if (!chooser) continue;
        if (g_signal_handler_find(model, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, chooser)) {
            visible = true;
            break;
        }
    }
    g_list_free(windows);
    return visible;
}
} // namespace

namespace NativeDialogs {
void EnableDirectoryLoadOrder() {
    initialDirectory = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath().utf8_string();
    wxString root;
    if (!wxGetEnv("WXNOTEPAD_DOCUMENT_TOOLS_ROOT", &root))
        throw std::runtime_error("directory load-order regression requires its owned document-tools root");
    requestedParent = root.utf8_string();
}
bool DirectoryLoadOrderCompleted() {
    std::fprintf(stderr, "directory load-order gates: enumeration=%d/%d/%d parent=%d/%d\n", enumerationQueued,
                 enumerationDelivered, enumerationFinished, parentQueued, parentDelivered);
    return enumerationQueued && enumerationDelivered && enumerationFinished && parentQueued && parentDelivered;
}
} // namespace NativeDialogs

extern "C" void __real_g_file_enumerator_next_files_async(GFileEnumerator*, int, int, GCancellable*,
                                                          GAsyncReadyCallback, gpointer);
extern "C" void __wrap_g_file_enumerator_next_files_async(GFileEnumerator* enumerator, int count, int priority,
                                                          GCancellable* cancel, GAsyncReadyCallback callback,
                                                          gpointer data) {
    if (enumerationQueued || !Matches(g_file_enumerator_get_container(enumerator), initialDirectory) ||
        !VisibleChooserModel(data)) {
        __real_g_file_enumerator_next_files_async(enumerator, count, priority, cancel, callback, data);
        return;
    }
    enumerationQueued = true;
    controlledEnumerator = Retain(enumerator);
    std::fprintf(stderr, "directory load-order: deferring initial enumeration\n");
    Defer(InitialEnumerationDelayMilliseconds, [enumerator = Retain(enumerator), count, priority,
                                                cancel = Retain(cancel), callback, data] {
        enumerationDelivered = true;
        __real_g_file_enumerator_next_files_async(enumerator.get(), count, priority, cancel.get(), callback, data);
    });
}

extern "C" GList* __real_g_file_enumerator_next_files_finish(GFileEnumerator*, GAsyncResult*, GError**);
extern "C" GList* __wrap_g_file_enumerator_next_files_finish(GFileEnumerator* enumerator, GAsyncResult* result,
                                                             GError** error) {
    auto* files = __real_g_file_enumerator_next_files_finish(enumerator, result, error);
    if (!files && (!error || !*error) && enumerator == controlledEnumerator.get()) {
        enumerationFinished = true;
        std::fprintf(stderr, "directory load-order: initial enumeration finished\n");
        // Let GTK consume this EOF and finish the old model before delivering
        // the new parent info. Both are ordinary valid asynchronous completions.
        if (deferredParent) Defer(0, std::move(deferredParent));
    }
    return files;
}

extern "C" void __real_g_file_query_info_async(GFile*, const char*, GFileQueryInfoFlags, int, GCancellable*,
                                               GAsyncReadyCallback, gpointer);
extern "C" void __wrap_g_file_query_info_async(GFile* file, const char* attributes, GFileQueryInfoFlags flags,
                                               int priority, GCancellable* cancel, GAsyncReadyCallback callback,
                                               gpointer data) {
    if (!enumerationQueued || parentQueued || !Matches(file, requestedParent) ||
        std::string(attributes) != "standard::type") {
        __real_g_file_query_info_async(file, attributes, flags, priority, cancel, callback, data);
        return;
    }
    parentQueued = true;
    if (enumerationFinished) {
        parentDelivered = true;
        std::fprintf(stderr, "directory load-order: requested parent after initial load\n");
        __real_g_file_query_info_async(file, attributes, flags, priority, cancel, callback, data);
        return;
    }
    std::fprintf(stderr, "directory load-order: deferring requested parent until initial load completes\n");
    deferredParent = [file = Retain(file), attributes = std::string(attributes), flags, priority,
                      cancel = Retain(cancel), callback, data] {
        parentDelivered = true;
        std::fprintf(stderr, "directory load-order: delivering requested parent after initial load\n");
        __real_g_file_query_info_async(file.get(), attributes.c_str(), flags, priority, cancel.get(), callback, data);
    };
}
