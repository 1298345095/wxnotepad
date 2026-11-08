#include "editor/platform/gtk_drag_drop.h"

#ifdef __WXGTK__
#include <wx/window.h>
#include <wx/utils.h>
#include <gtk/gtk.h>
#include <utility>

namespace {
GdkAtom FileTarget() {
    return gdk_atom_intern_static_string("text/uri-list");
}
bool OffersFiles(GdkDragContext* context) {
    return g_list_find(gdk_drag_context_list_targets(context), FileTarget()) != nullptr;
}
GdkAtom Target(GtkWidget* widget, GdkDragContext* context) {
    return OffersFiles(context) ? FileTarget() : gtk_drag_dest_find_target(widget, context, nullptr);
}
GdkDragAction DragAction(GdkDragContext* context, bool readOnly) {
    const auto allowed = gdk_drag_context_get_actions(context);
    if (OffersFiles(context)) return static_cast<GdkDragAction>(allowed & GDK_ACTION_COPY);
    if (readOnly) return static_cast<GdkDragAction>(0);
    if (wxGetKeyState(WXK_CONTROL) && (allowed & GDK_ACTION_COPY)) return GDK_ACTION_COPY;
    if (allowed & GDK_ACTION_MOVE) return GDK_ACTION_MOVE;
    return static_cast<GdkDragAction>(allowed & GDK_ACTION_COPY);
}
} // namespace

GtkTextDragDrop::GtkTextDragDrop(wxWindow& window, Callbacks callbacks)
    : window_(window), callbacks_(std::move(callbacks)) {
    auto* widget = window_.GetConnectWidget();
    gtk_drag_dest_set(widget, static_cast<GtkDestDefaults>(0), nullptr, 0,
                      static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    auto* targets = gtk_target_list_new(nullptr, 0);
    gtk_target_list_add_uri_targets(targets, 1);
    gtk_target_list_add_text_targets(targets, 0);
    gtk_drag_dest_set_target_list(widget, targets);
    gtk_target_list_unref(targets);
    g_signal_connect(widget, "drag-motion",
                     G_CALLBACK(+[](GtkWidget* widget, GdkDragContext* context, gint x, gint y, guint time,
                                    gpointer value) -> gboolean {
                         auto& editor = *static_cast<GtkTextDragDrop*>(value);
                         auto action = DragAction(context, editor.callbacks_.readOnly());
                         if (Target(widget, context) == GDK_NONE) action = static_cast<GdkDragAction>(0);
                         gdk_drag_status(context, action, time);
                         if (action && !OffersFiles(context))
                             editor.callbacks_.showFeedback(x, y);
                         else
                             editor.callbacks_.clearFeedback();
                         return TRUE;
                     }),
                     this);
    g_signal_connect(widget, "drag-leave", G_CALLBACK(+[](GtkWidget*, GdkDragContext*, guint, gpointer value) {
                         static_cast<GtkTextDragDrop*>(value)->callbacks_.clearFeedback();
                     }),
                     this);
    g_signal_connect(
        widget, "drag-drop",
        G_CALLBACK(+[](GtkWidget* widget, GdkDragContext* context, gint, gint, guint time, gpointer value) -> gboolean {
            auto& editor = *static_cast<GtkTextDragDrop*>(value);
            const auto target = Target(widget, context);
            if (!DragAction(context, editor.callbacks_.readOnly()) || target == GDK_NONE) {
                editor.callbacks_.clearFeedback();
                gtk_drag_finish(context, FALSE, FALSE, time);
                return TRUE;
            }
            gtk_drag_get_data(widget, context, target, time);
            return TRUE;
        }),
        this);
    g_signal_connect(
        widget, "drag-data-received",
        G_CALLBACK(+[](GtkWidget*, GdkDragContext* context, gint x, gint y, GtkSelectionData* data, guint, guint time,
                       gpointer value) {
            auto& editor = *static_cast<GtkTextDragDrop*>(value);
            editor.callbacks_.clearFeedback();
            if (gtk_selection_data_get_target(data) == FileTarget()) {
                auto** uris = gtk_selection_data_get_uris(data);
                auto* filename = uris && uris[0] ? g_filename_from_uri(uris[0], nullptr, nullptr) : nullptr;
                const bool accepted = filename != nullptr;
                if (accepted) editor.callbacks_.dropFile(wxConvFileName->cMB2WX(filename));
                g_free(filename);
                g_strfreev(uris);
                gtk_drag_finish(context, accepted, FALSE, time);
                return;
            }
            auto* text = gtk_selection_data_get_text(data);
            const auto action = gdk_drag_context_get_selected_action(context);
            const bool accepted = text && *text && !editor.callbacks_.readOnly() &&
                                  (action == GDK_ACTION_COPY || action == GDK_ACTION_MOVE);
            if (accepted) {
                editor.callbacks_.dropText(x, y, reinterpret_cast<const char*>(text), action == GDK_ACTION_MOVE);
            }
            g_free(text);
            // Native GTK peers rely on the delete request after a successful
            // move; the original core handles deletion for our own source.
            gtk_drag_finish(context, accepted, accepted && action == GDK_ACTION_MOVE, time);
        }),
        this);
}

GtkTextDragDrop::~GtkTextDragDrop() {
    g_signal_handlers_disconnect_by_data(window_.GetConnectWidget(), this);
    gtk_drag_dest_unset(window_.GetConnectWidget());
}

wxDragResult GtkTextDragDrop::Start(std::string_view text) {
    struct Transfer {
        std::string_view text;
        bool finished = false;
        bool failed = false;
        GdkDragAction operation = static_cast<GdkDragAction>(0);
        wxDragResult result = wxDragNone;
    } transfer{text};
    auto* widget = window_.GetConnectWidget();
    const auto dataHandler = g_signal_connect(
        widget, "drag-data-get",
        G_CALLBACK(+[](GtkWidget*, GdkDragContext*, GtkSelectionData* data, guint, guint, gpointer value) {
            const auto& transfer = *static_cast<Transfer*>(value);
            gtk_selection_data_set_text(data, transfer.text.data(), static_cast<gint>(transfer.text.size()));
        }),
        &transfer);
    const auto endHandler =
        g_signal_connect(widget, "drag-end", G_CALLBACK(+[](GtkWidget*, GdkDragContext* context, gpointer value) {
                             auto& transfer = *static_cast<Transfer*>(value);
                             if (!transfer.failed) {
                                 const auto action = transfer.operation ? transfer.operation
                                                                        : gdk_drag_context_get_selected_action(context);
                                 if (action == GDK_ACTION_MOVE)
                                     transfer.result = wxDragMove;
                                 else if (action == GDK_ACTION_COPY)
                                     transfer.result = wxDragCopy;
                             }
                             transfer.finished = true;
                         }),
                         &transfer);
    const auto failedHandler = g_signal_connect(
        widget, "drag-failed", G_CALLBACK(+[](GtkWidget*, GdkDragContext*, GtkDragResult, gpointer value) -> gboolean {
            static_cast<Transfer*>(value)->failed = true;
            return FALSE;
        }),
        &transfer);
    auto* targets = gtk_target_list_new(nullptr, 0);
    gtk_target_list_add_text_targets(targets, 0);
    auto* event = gtk_get_current_event();
    const auto context = gtk_drag_begin_with_coordinates(
        widget, targets, static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE), 1, event, -1, -1);
    if (event) gdk_event_free(event);
    gtk_target_list_unref(targets);
    if (context) {
        g_object_ref(context);
        const auto dropHandler =
            g_signal_connect(context, "drop-performed", G_CALLBACK(+[](GdkDragContext* context, guint, gpointer value) {
                                 static_cast<Transfer*>(value)->operation =
                                     gdk_drag_context_get_selected_action(context);
                             }),
                             &transfer);
        while (!transfer.finished)
            gtk_main_iteration();
        g_signal_handler_disconnect(context, dropHandler);
        g_object_unref(context);
    }
    g_signal_handler_disconnect(widget, dataHandler);
    g_signal_handler_disconnect(widget, endHandler);
    g_signal_handler_disconnect(widget, failedHandler);
    return transfer.result;
}
#endif
