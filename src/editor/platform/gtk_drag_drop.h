#pragma once

#include <wx/defs.h>

#ifdef __WXGTK__
#include <wx/dnd.h>
#include <functional>
#include <string_view>

class wxWindow;

class GtkTextDragDrop final {
public:
    struct Callbacks {
        std::function<bool()> readOnly;
        std::function<void(int, int)> showFeedback;
        std::function<void()> clearFeedback;
        std::function<void(int, int, std::string_view, bool)> dropText;
        std::function<void(const wxString&)> dropFile;
    };

    GtkTextDragDrop(wxWindow& window, Callbacks callbacks);
    ~GtkTextDragDrop();
    GtkTextDragDrop(const GtkTextDragDrop&) = delete;
    GtkTextDragDrop& operator=(const GtkTextDragDrop&) = delete;
    wxDragResult Start(std::string_view text);

private:
    wxWindow& window_;
    Callbacks callbacks_;
};
#endif
