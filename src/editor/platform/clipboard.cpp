#include "clipboard.h"
#include "core_headers.h"
#include "diagnostics/trace.h"
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/window.h>
#include <cstring>
#include <memory>
#include <utility>
#ifdef __WXMSW__
#include <windows.h>
#elif defined(__WXGTK__)
#include <gtk/gtk.h>
#endif

#ifdef __WXMSW__
namespace {
// Original ScintillaWin clipboard ownership and bounded retry rule.
class NativeClipboard {
public:
    explicit NativeClipboard(HWND window) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            if (attempt) ::Sleep(1 << (attempt - 1));
            if (::OpenClipboard(window)) {
                opened_ = true;
                WXNOTE_TRACE("clipboard.native-open", "\ttarget=%p", static_cast<void*>(window));
                break;
            }
        }
    }
    ~NativeClipboard() {
        if (opened_) {
            ::CloseClipboard();
            WXNOTE_TRACE("clipboard.native-close");
        }
    }
    explicit operator bool() const { return opened_; }

private:
    bool opened_ = false;
};
} // namespace
#endif

#ifdef __WXGTK__
namespace {
constexpr char PlainTextFormat[] = "application/x-wxnotepad-plain-text";
struct GtkClipboardText {
    std::string bytes;
    size_t plainLength = 0;
};
bool GtkHasData() {
    GdkAtom* targets = nullptr;
    gint count = 0;
    const bool any =
        gtk_clipboard_wait_for_targets(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), &targets, &count) && count > 0;
    g_free(static_cast<void*>(targets));
    return any;
}
} // namespace
#endif

bool EditorClipboard::HasData([[maybe_unused]] wxWindow& owner) {
    if (wxTheClipboard->IsOpened()) return false;
#ifdef __WXMSW__
    const NativeClipboard clipboard(static_cast<HWND>(owner.GetHandle()));
    return clipboard && ::CountClipboardFormats() != 0;
#else
    wxTheClipboard->UsePrimarySelection(false);
    wxClipboardLocker lock;
    if (!lock) return false;
#ifdef __WXGTK__
    return GtkHasData();
#else
    return wxTheClipboard->IsSupported(wxDF_UNICODETEXT);
#endif
#endif
}

bool EditorClipboard::Clear([[maybe_unused]] wxWindow& owner) {
    WXNOTE_TRACE("clipboard.clear", "\ttarget=%p", static_cast<void*>(&owner));
    if (wxTheClipboard->IsOpened()) return false;
#ifdef __WXMSW__
    const NativeClipboard clipboard(static_cast<HWND>(owner.GetHandle()));
    return clipboard && ::CountClipboardFormats() != 0 && ::EmptyClipboard();
#else
    wxTheClipboard->UsePrimarySelection(false);
    wxClipboardLocker lock;
    if (!lock) return false;
#ifdef __WXGTK__
    if (!GtkHasData()) return false;
    // GTK clear releases only our ownership. Take over before releasing it so
    // foreign offers are cleared too. An offer with zero targets also discards
    // a clipboard manager's saved formats before the owned source is destroyed.
    // This handles Weston versions which ignore bare null-source requests.
    auto* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    const GtkTargetEntry noTargets{};
    if (!gtk_clipboard_set_with_data(
            clipboard, &noTargets, 0,
            [](GtkClipboard*, GtkSelectionData* data, guint, gpointer) {
                gtk_selection_data_set(data, gtk_selection_data_get_target(data), 8, nullptr, 0);
            },
            nullptr, nullptr))
        return false;
    WXNOTE_TRACE("clipboard.clear-owned", "\ttarget=%p", static_cast<void*>(&owner));
    gtk_clipboard_clear(clipboard);
    WXNOTE_TRACE("clipboard.clear-released", "\ttarget=%p", static_cast<void*>(&owner));
    return true;
#else
    wxTheClipboard->Clear();
    return true;
#endif
#endif
}

bool EditorClipboard::Write([[maybe_unused]] wxWindow& owner, const Scintilla::Internal::SelectionText& text) {
    WXNOTE_TRACE("clipboard.write", "\ttarget=%p\tlength=%llu\trectangular=%d\tline=%d", static_cast<void*>(&owner),
                 static_cast<unsigned long long>(text.Length()), text.rectangular, text.lineCopy);
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
    WXNOTE_TRACE("clipboard.owned", "\ttarget=%p\tformat=CF_UNICODETEXT\tlength=%llu", static_cast<void*>(&owner),
                 static_cast<unsigned long long>(text.Length()));
    return true;
#else
    wxTheClipboard->UsePrimarySelection(false);
    wxClipboardLocker lock;
    if (!lock) return false;
#ifdef __WXGTK__
    // GTK owns each successful publication until its clear callback. Preserve
    // ScintillaGTK's LF + NUL rectangle marker and its full target length.
    auto value = std::make_unique<GtkClipboardText>();
    value->bytes = text.AsView();
    value->plainLength = value->bytes.size();
    if (text.rectangular) {
        if (value->bytes.empty() || value->bytes.back() != '\n') value->bytes.push_back('\n');
        value->bytes.push_back('\0');
    }
    auto* list = gtk_target_list_new(nullptr, 0);
    gtk_target_list_add_text_targets(list, 0);
    // The LF+NUL native shape convention cannot distinguish an added carrier
    // LF from a genuine empty final rectangle row. Retain original plain bytes
    // for supporting peers while leaving the native Paste targets unchanged.
    if (text.rectangular) gtk_target_list_add(list, gdk_atom_intern_static_string(PlainTextFormat), 0, 2);
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
            const auto* value = static_cast<const GtkClipboardText*>(owner);
            WXNOTE_TRACE("clipboard.transfer", "\towner=%p\tformat=%s\tlength=%llu", owner,
                         info == 1 ? "line-marker" : "GTK-text",
                         static_cast<unsigned long long>(info == 1 ? 0 : value->bytes.size()));
            if (info == 1)
                gtk_selection_data_set(data, gtk_selection_data_get_target(data), 8, nullptr, 0);
            else if (info == 2)
                gtk_selection_data_set(data, gtk_selection_data_get_target(data), 8,
                                       reinterpret_cast<const guchar*>(value->bytes.data()),
                                       static_cast<int>(value->plainLength));
            else
                gtk_selection_data_set_text(data, value->bytes.c_str(), static_cast<int>(value->bytes.size()));
        },
        [](GtkClipboard*, gpointer owner) {
            const std::unique_ptr<GtkClipboardText> released(static_cast<GtkClipboardText*>(owner));
            WXNOTE_TRACE("clipboard.cleared", "\towner=%p\tlength=%llu", owner,
                         static_cast<unsigned long long>(released->bytes.size()));
        },
        value.get());
    gtk_target_table_free(targets, count);
    gtk_target_list_unref(list);
    if (copied) {
        WXNOTE_TRACE("clipboard.owned", "\ttarget=%p\towner=%p\tformat=GTK-text\tlength=%llu",
                     static_cast<void*>(&owner), static_cast<void*>(value.get()),
                     static_cast<unsigned long long>(value->bytes.size()));
        // GTK now owns the pointer already passed to set_with_data.
        value.release(); // NOLINT(bugprone-unused-return-value)
    }
    if (!copied) WXNOTE_TRACE("clipboard.write-failed", "\ttarget=%p", static_cast<void*>(&owner));
    return copied;
#else
    const bool copied = wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text.Data(), text.Length())));
    WXNOTE_TRACE(copied ? "clipboard.owned" : "clipboard.write-failed",
                 "\ttarget=%p\tformat=wxUNICODETEXT\tlength=%llu", static_cast<void*>(&owner),
                 static_cast<unsigned long long>(text.Length()));
    return copied;
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

std::optional<std::string> EditorClipboard::ReadPlainText(wxWindow& owner) {
    if (wxTheClipboard->IsOpened()) return std::nullopt;
    std::optional<std::string> result;
#ifdef __WXGTK__
    auto* data = gtk_clipboard_wait_for_contents(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                                                 gdk_atom_intern_static_string(PlainTextFormat));
    if (data) {
        const int length = gtk_selection_data_get_length(data);
        if (length >= 0) {
            result.emplace();
            if (length) result->assign(reinterpret_cast<const char*>(gtk_selection_data_get_data(data)), length);
        }
        gtk_selection_data_free(data);
    }
#endif
    if (!result) {
        auto text = Read(owner, [] { return false; });
        if (!text) return std::nullopt;
        result = std::move(text->text);
    }
    const auto terminator = result->find('\0');
    if (terminator != std::string::npos) result->resize(terminator);
    return result;
}

std::optional<EditorClipboard::ReadResult>
EditorClipboard::Read([[maybe_unused]] wxWindow& owner, [[maybe_unused]] const std::function<bool()>& selectionEmpty) {
    WXNOTE_TRACE("clipboard.read", "\ttarget=%p", static_cast<void*>(&owner));
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
    [[maybe_unused]] const char* receivedFormat = "UTF8_STRING";
    auto* data = gtk_clipboard_wait_for_contents(clipboard, gdk_atom_intern_static_string("UTF8_STRING"));
    if (!data || gtk_selection_data_get_length(data) < 0) {
        if (data) gtk_selection_data_free(data);
        receivedFormat = "text/plain;charset=utf-8";
        data = gtk_clipboard_wait_for_contents(clipboard, gdk_atom_intern_static_string("text/plain;charset=utf-8"));
    }
    if (data && gtk_selection_data_get_length(data) >= 0) {
        const int length = gtk_selection_data_get_length(data);
        const auto* bytes = reinterpret_cast<const char*>(gtk_selection_data_get_data(data));
        result.rectangular = length > 2 && bytes[length - 1] == '\0' && bytes[length - 2] == '\n';
        if (length) result.text.assign(bytes, length - (result.rectangular ? 1 : 0));
        WXNOTE_TRACE("clipboard.received", "\ttarget=%p\tformat=%s\tlength=%d\trectangular=%d\tline=%d",
                     static_cast<void*>(&owner), receivedFormat, length, result.rectangular, result.lineCopy);
        gtk_selection_data_free(data);
    } else {
        if (data) gtk_selection_data_free(data);
        auto* value = gtk_clipboard_wait_for_text(clipboard);
        if (!value) return std::nullopt;
        result.text = value;
        WXNOTE_TRACE("clipboard.received", "\ttarget=%p\tformat=GTK-text\tlength=%llu\tline=%d",
                     static_cast<void*>(&owner), static_cast<unsigned long long>(result.text.size()), result.lineCopy);
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
    WXNOTE_TRACE("clipboard.received", "\ttarget=%p\tformat=CF_UNICODETEXT\tlength=%llu\trectangular=%d\tline=%d",
                 static_cast<void*>(&owner), static_cast<unsigned long long>(result.text.size()), result.rectangular,
                 result.lineCopy);
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
    WXNOTE_TRACE("clipboard.received", "\ttarget=%p\tformat=wxUNICODETEXT\tlength=%llu", static_cast<void*>(&owner),
                 static_cast<unsigned long long>(result.text.size()));
#endif
    return result;
}
