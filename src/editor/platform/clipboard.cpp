#include "clipboard.h"
#include "core_headers.h"
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/window.h>
#include <cstring>
#include <memory>
#ifdef __WXMSW__
#include <windows.h>
#elif defined(__WXGTK__)
#include <gtk/gtk.h>
#endif

namespace {
#ifdef __WXMSW__
// Original ScintillaWin clipboard ownership and bounded retry rule.
class NativeClipboard {
public:
    explicit NativeClipboard(HWND window) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            if (attempt) ::Sleep(1 << (attempt - 1));
            if (::OpenClipboard(window)) {
                opened_ = true;
                break;
            }
        }
    }
    ~NativeClipboard() {
        if (opened_) ::CloseClipboard();
    }
    explicit operator bool() const { return opened_; }

private:
    bool opened_ = false;
};
#endif
} // namespace

bool EditorClipboard::Write([[maybe_unused]] wxWindow& owner, const Scintilla::Internal::SelectionText& text) {
    if (wxTheClipboard->IsOpened()) return false;
#ifdef __WXMSW__
    const NativeClipboard clipboard(static_cast<HWND>(owner.GetHandle()));
    if (!clipboard) return false;
    const auto value = wxString::FromUTF8(text.Data(), text.Length()).ToStdWstring();
    const auto memory = ::GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    if (!memory) return false;
    auto* bytes = ::GlobalLock(memory);
    if (!bytes) {
        ::GlobalFree(memory);
        return false;
    }
    std::memcpy(bytes, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
    ::GlobalUnlock(memory);
    if (!::EmptyClipboard() || !::SetClipboardData(CF_UNICODETEXT, memory)) {
        ::GlobalFree(memory);
        return false;
    }
    if (text.rectangular) {
        ::SetClipboardData(::RegisterClipboardFormatW(L"MSDEVColumnSelect"), nullptr);
        const auto block = ::GlobalAlloc(GMEM_MOVEABLE, 1);
        if (block) {
            auto* marker = static_cast<unsigned char*>(::GlobalLock(block));
            if (marker) {
                *marker = 2;
                ::GlobalUnlock(block);
                if (!::SetClipboardData(::RegisterClipboardFormatW(L"Borland IDE Block Type"), block))
                    ::GlobalFree(block);
            } else {
                ::GlobalFree(block);
            }
        }
    }
    if (text.lineCopy) {
        ::SetClipboardData(::RegisterClipboardFormatW(L"MSDEVLineSelect"), nullptr);
        ::SetClipboardData(::RegisterClipboardFormatW(L"VisualStudioEditorOperationsLineCutCopyClipboardTag"), nullptr);
    }
    return true;
#else
    wxTheClipboard->UsePrimarySelection(false);
    wxClipboardLocker lock;
    if (!lock) return false;
#ifdef __WXGTK__
    // GTK owns each successful publication until its clear callback. Preserve
    // ScintillaGTK's LF + NUL rectangle marker and its full target length.
    auto value = std::make_unique<std::string>(text.AsView());
    if (text.rectangular) {
        if (value->empty() || value->back() != '\n') value->push_back('\n');
        value->push_back('\0');
    }
    auto* list = gtk_target_list_new(nullptr, 0);
    gtk_target_list_add_text_targets(list, 0);
    if (text.lineCopy) {
        gtk_target_list_add(list, gdk_atom_intern_static_string("MSDEVLineSelect"), 0, 1);
        gtk_target_list_add(list, gdk_atom_intern_static_string("VisualStudioEditorOperationsLineCutCopyClipboardTag"),
                            0, 1);
    }
    gint count = 0;
    auto* targets = gtk_target_table_new_from_list(list, &count);
    const bool copied = gtk_clipboard_set_with_data(
        gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), targets, count,
        [](GtkClipboard*, GtkSelectionData* data, guint info, gpointer owner) {
            const auto* value = static_cast<const std::string*>(owner);
            if (info == 1)
                gtk_selection_data_set(data, gtk_selection_data_get_target(data), 8, nullptr, 0);
            else
                gtk_selection_data_set_text(data, value->c_str(), static_cast<int>(value->size()));
        },
        [](GtkClipboard*, gpointer owner) {
            const std::unique_ptr<std::string> released(static_cast<std::string*>(owner));
        },
        value.get());
    gtk_target_table_free(targets, count);
    gtk_target_list_unref(list);
    if (copied) value.release();
    return copied;
#else
    return wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text.Data(), text.Length())));
#endif
#endif
}

bool EditorClipboard::HasText() noexcept {
#ifdef __WXMSW__
    return ::IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
#else
    // Queries can dispatch UI callbacks; retain the existing reentrancy guard.
    if (wxTheClipboard->IsOpened()) return false;
    wxTheClipboard->UsePrimarySelection(false);
    wxClipboardLocker lock;
    if (!lock) return false;
    return wxTheClipboard->IsSupported(wxDF_UNICODETEXT);
#endif
}

std::optional<EditorClipboard::ReadResult>
EditorClipboard::Read([[maybe_unused]] wxWindow& owner, [[maybe_unused]] const std::function<bool()>& selectionEmpty) {
    if (wxTheClipboard->IsOpened()) return std::nullopt;
    ReadResult result;
#ifdef __WXGTK__
    auto* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    result.lineCopy =
        selectionEmpty() &&
        (gtk_clipboard_wait_is_target_available(clipboard, gdk_atom_intern_static_string("MSDEVLineSelect")) ||
         gtk_clipboard_wait_is_target_available(
             clipboard, gdk_atom_intern_static_string("VisualStudioEditorOperationsLineCutCopyClipboardTag")));
    // Read raw UTF-8: wxTextDataObject discards the rectangle's terminating NUL.
    auto* data = gtk_clipboard_wait_for_contents(clipboard, gdk_atom_intern_static_string("UTF8_STRING"));
    if (!data || gtk_selection_data_get_length(data) < 0) {
        if (data) gtk_selection_data_free(data);
        data = gtk_clipboard_wait_for_contents(clipboard, gdk_atom_intern_static_string("text/plain;charset=utf-8"));
    }
    if (data && gtk_selection_data_get_length(data) >= 0) {
        const int length = gtk_selection_data_get_length(data);
        const auto* bytes = reinterpret_cast<const char*>(gtk_selection_data_get_data(data));
        result.rectangular = length > 2 && bytes[length - 1] == '\0' && bytes[length - 2] == '\n';
        if (length) result.text.assign(bytes, length - (result.rectangular ? 1 : 0));
        gtk_selection_data_free(data);
    } else {
        if (data) gtk_selection_data_free(data);
        auto* value = gtk_clipboard_wait_for_text(clipboard);
        if (!value) return std::nullopt;
        result.text = value;
        g_free(value);
    }
#elif defined(__WXMSW__)
    const NativeClipboard clipboard(static_cast<HWND>(owner.GetHandle()));
    if (!clipboard) return std::nullopt;
    const auto memory = ::GetClipboardData(CF_UNICODETEXT);
    if (!memory) return std::nullopt;
    const auto* bytes = static_cast<const wchar_t*>(::GlobalLock(memory));
    if (!bytes) return std::nullopt;
    const wxString value(bytes);
    const auto buffer = value.ToUTF8();
    result.text.assign(buffer.data(), buffer.length());
    ::GlobalUnlock(memory);
    result.lineCopy =
        selectionEmpty() && (::IsClipboardFormatAvailable(::RegisterClipboardFormatW(L"MSDEVLineSelect")) ||
                             ::IsClipboardFormatAvailable(
                                 ::RegisterClipboardFormatW(L"VisualStudioEditorOperationsLineCutCopyClipboardTag")));
    result.rectangular = ::IsClipboardFormatAvailable(::RegisterClipboardFormatW(L"MSDEVColumnSelect")) != 0;
    if (!result.rectangular) {
        const auto block = ::GetClipboardData(::RegisterClipboardFormatW(L"Borland IDE Block Type"));
        if (block) {
            const auto* marker = static_cast<const unsigned char*>(::GlobalLock(block));
            if (marker) {
                result.rectangular = ::GlobalSize(block) == 1 && *marker == 2;
                ::GlobalUnlock(block);
            }
        }
    }
#else
    wxTextDataObject text;
    {
        wxTheClipboard->UsePrimarySelection(false);
        wxClipboardLocker lock;
        if (!lock || !wxTheClipboard->GetData(text)) return std::nullopt;
    }
    const wxString value = text.GetText();
    const auto buffer = value.ToUTF8();
    result.text.assign(buffer.data(), buffer.length());
#endif
    return result;
}
