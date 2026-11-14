#pragma once
#include "editor/editor_core.h"
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

// Read-only use of the existing observation friendship; never forces layout.
struct EditorCoreObservation {
    static void Check(EditorControl& editor, int requestedWidth) {
        const auto* core = editor.core_.get();
        const auto client = editor.GetClientSize();
        const bool wrapped =
            editor.Send(Scintilla::Message::GetWrapMode) != static_cast<intptr_t>(Scintilla::Wrap::None);
        const int expected = static_cast<int>(client.x - core->vs.textStart - core->vs.rightMarginWidth);
        if (wrapped && core->wrapWidth != expected)
            throw std::runtime_error("core wrap width does not match the current viewport");
        int drawing = -1, overlay = -1;
#ifdef __WXGTK__
        auto* widget = static_cast<GtkWidget*>(editor.GetHandle());
        auto* child = GTK_IS_BIN(widget) ? gtk_bin_get_child(GTK_BIN(widget)) : nullptr;
        drawing = child ? gtk_widget_get_allocated_width(child) : -1;
        overlay = GTK_IS_SCROLLED_WINDOW(widget)
                      ? gtk_scrolled_window_get_overlay_scrolling(GTK_SCROLLED_WINDOW(widget))
                      : -1;
#endif
        std::printf("{\"metric\":\"geometry\",\"requested\":%d,\"outer\":%d,\"client\":%d,\"drawing\":%d,\"overlay\":%"
                    "d,\"wrapped\":%d,\"wrap_width\":%d,\"text_start\":%g,\"right_margin\":%g}\n",
                    requestedWidth, editor.GetSize().x, client.x, drawing, overlay, wrapped, core->wrapWidth,
                    static_cast<double>(core->vs.textStart), static_cast<double>(core->vs.rightMarginWidth));
    }
};
