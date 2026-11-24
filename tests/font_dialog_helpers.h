#pragma once

#include "native_key_helpers.h"
#include <print>
#include <wx/fontdlg.h>
#include <wx/modalhook.h>
#include <chrono>
#ifdef __WXMSW__
#include <commdlg.h>
#include <dlgs.h>
#include <imm.h>
#endif

namespace NativeFonts {
// Drive the platform font chooser itself, including its initial native values.
class Chooser final : public wxModalDialogHook, public wxTimer {
public:
    Chooser(EditorFrame* frame, int expectedPoints, wxString family, bool cancel, wxColour colour = {}, bool styleChooser = false)
        : frame_(frame), expectedPoints_(expectedPoints), family_(std::move(family)), cancel_(cancel), colour_(colour), styleChooser_(styleChooser) {
        Register();
    }
    ~Chooser() override { Unregister(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Select Scheme") {
            error_ = "native font shortcut must open the font chooser rather than the syntax scheme chooser";
            wxTheApp->CallAfter([dialog] { NativeSearch::Click(dialog, wxID_CANCEL); });
            return wxID_NONE;
        }
        if (auto* font = wxDynamicCast(dialog, wxFontDialog)) {
            dialog_ = font;
            if (colour_.IsOk() && dialog_->GetFontData().GetColour() != colour_) {
                error_ = "font chooser starts with the selected default foreground, which font confirmation preserves";
                return wxID_CANCEL;
            }
            std::println(stderr, "native chooser initial: {}",
                         dialog_->GetFontData().GetInitialFont().GetNativeFontInfoDesc().utf8_str().data());
#ifdef __WXGTK__
            // wx's native GTK chooser bypasses wxWindow's key event handlers.
            g_signal_connect(dialog_->GetHandle(), "key-release-event",
                             G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
                                 if (event->keyval == GDK_KEY_F12) static_cast<Chooser*>(data)->released_ = true;
                                 return FALSE;
                             }),
                             this);
#endif
            deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            Start(20);
        }
        return wxID_NONE;
    }
    void Verify() const {
        if (!error_.empty()) throw std::runtime_error(error_);
        if (!done_) throw std::runtime_error("native font chooser must be shown and answered");
    }
    void OpenWithShortcut() {
#ifdef __WXMSW__
        NativeKeys::Shortcut(frame_, WXK_F12, false, true);
#else
        wxString focusFile;
        NativeKeys::FocusEditor(frame_, "wxnotepad", focusFile);
        bool received = false;
        NativeKeys::KeyRelease observer(WXK_F12, received, nullptr);
        {
            wxFFile request(focusFile, "wb");
            if (!request.Write(NativeTrace::Request("key:alt+F12")))
                throw std::runtime_error("write native font shortcut request");
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (!done_ && error_.empty() && std::chrono::steady_clock::now() < deadline) {
            wxYield();
            wxMilliSleep(10);
        }
        Verify();
        if (!received && !released_)
            throw std::runtime_error("real font shortcut releases in the owned editor or native chooser");
#endif
    }
    void OpenWithMenu(int id) {
#ifdef __WXGTK__
        // A real menu is invoked from the active application.
        wxString focusFile;
        NativeKeys::FocusEditor(frame_, "wxnotepad", focusFile);
#endif
        NativeSearch::Command(frame_, id);
        Verify();
    }
    void Notify() override {
        try {
#ifdef __WXMSW__
            struct Target { HWND native = nullptr; bool style; } target{nullptr, styleChooser_};
            EnumThreadWindows(
                GetCurrentThreadId(),
                [](HWND window, LPARAM data) -> BOOL {
                    wchar_t title[128]{};
                    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
                    auto& target = *reinterpret_cast<Target*>(data);
                    if (target.style) {
                        if (!GetDlgItem(window, cmb1) || !GetDlgItem(window, cmb2) || !GetDlgItem(window, cmb3)) return TRUE;
                    } else if (wxString(title) != "Default Code Font" && wxString(title) != "Default Text Font") return TRUE;
                    target.native = window;
                    return FALSE;
                },
                reinterpret_cast<LPARAM>(&target));
            const auto native = target.native;
            if (!native) {
                if (std::chrono::steady_clock::now() >= deadline_)
                    throw std::runtime_error("owned native font chooser did not appear");
                return;
            }
            LOGFONTW initial{};
            SendMessageW(native, WM_CHOOSEFONT_GETLOGFONT, 0, reinterpret_cast<LPARAM>(&initial));
            const int points = MulDiv(-initial.lfHeight, 72, GetDpiForSystem());
            if (!selected_) {
                if (points != expectedPoints_)
                    throw std::runtime_error("native chooser starts at the selected default point size");
                // Family/style/size are the native common-dialog controls.
                auto* family = GetDlgItem(native, cmb1);
                const auto familyIndex = SendMessageW(family, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                                      reinterpret_cast<LPARAM>(family_.wc_str()));
                if (familyIndex == CB_ERR) throw std::runtime_error("requested native font family must be installed");
                SendMessageW(family, CB_SETCURSEL, familyIndex, 0);
                SendMessageW(native, WM_COMMAND, MAKEWPARAM(cmb1, CBN_SELCHANGE), reinterpret_cast<LPARAM>(family));
                auto* style = GetDlgItem(native, cmb2);
                const auto count = SendMessageW(style, CB_GETCOUNT, 0, 0);
                bool found = false;
                for (LRESULT index = 0; index < count; ++index) {
                    SendMessageW(style, CB_SETCURSEL, index, 0);
                    SendMessageW(native, WM_COMMAND, MAKEWPARAM(cmb2, CBN_SELCHANGE), reinterpret_cast<LPARAM>(style));
                    LOGFONTW chosen{};
                    SendMessageW(native, WM_CHOOSEFONT_GETLOGFONT, 0, reinterpret_cast<LPARAM>(&chosen));
                    if (chosen.lfWeight == FW_BOLD && chosen.lfItalic) {
                        found = true;
                        break;
                    }
                }
                if (!found) throw std::runtime_error("native font fixture needs an installed bold italic face");
                SetDlgItemTextW(native, cmb3, L"15");
                CheckDlgButton(native, chx1, BST_CHECKED);
                CheckDlgButton(native, chx2, BST_CHECKED);
                selected_ = true;
                return;
            }
            Stop();
            done_ = true;
            PostMessageW(native, WM_COMMAND, cancel_ ? IDCANCEL : IDOK, 0);
#elif defined(__WXGTK__)
            auto* native = GTK_WIDGET(dialog_->GetHandle());
            if (!native || !gtk_widget_get_mapped(native)) {
                if (std::chrono::steady_clock::now() >= deadline_)
                    throw std::runtime_error("native font chooser must become mapped");
                return;
            }
            auto* description = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(native));
            if (!description) {
                if (std::chrono::steady_clock::now() >= deadline_) {
                    throw std::runtime_error(selected_ ? "native chooser must select the requested installed face"
                                                       : "native font chooser must expose an initial font selection");
                }
                return;
            }
            const auto points = static_cast<double>(pango_font_description_get_size(description)) / PANGO_SCALE;
            pango_font_description_free(description);
            if (!selected_) {
                if (points != expectedPoints_)
                    throw std::runtime_error("native chooser starts at the selected default point size");
                const auto selection = family_ + " Bold Italic 15";
                auto* requested = pango_font_description_from_string(selection.utf8_str());
                auto* loaded = pango_context_load_font(gtk_widget_get_pango_context(native), requested);
                pango_font_description_free(requested);
                if (!loaded)
                    throw std::runtime_error("native font fixture requires an installed bold italic/oblique face");
                auto* available = pango_font_describe(loaded);
                pango_font_description_set_size(available, 15 * PANGO_SCALE);
                gtk_font_chooser_set_font_desc(GTK_FONT_CHOOSER(native), available);
                pango_font_description_free(available);
                g_object_unref(loaded);
                selected_ = true;
                return;
            }
            if (points != 15) {
                if (std::chrono::steady_clock::now() >= deadline_)
                    throw std::runtime_error("native font controls must accept selected point size");
                return;
            }
            Stop();
            done_ = true;
            NativeDialogs::Respond(native, cancel_ ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_OK);
#endif
        } catch (const std::exception& error) {
            Stop();
            error_ = error.what();
#ifdef __WXMSW__
            EnumThreadWindows(
                GetCurrentThreadId(),
                [](HWND window, LPARAM) -> BOOL {
                    if (GetClassLongPtrW(window, GCW_ATOM) == 32770) PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
                    return TRUE;
                },
                0);
#elif defined(__WXGTK__)
            NativeDialogs::Respond(GTK_WIDGET(dialog_->GetHandle()), GTK_RESPONSE_CANCEL);
#endif
        }
    }

private:
    EditorFrame* frame_;
    wxFontDialog* dialog_ = nullptr;
    int expectedPoints_;
    wxString family_;
    bool cancel_;
    wxColour colour_;
    [[maybe_unused]] bool styleChooser_;
    bool selected_ = false;
    bool done_ = false;
    bool released_ = false;
    std::string error_;
    std::chrono::steady_clock::time_point deadline_;
};
} // namespace NativeFonts
