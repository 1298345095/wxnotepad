// Independent stock-widget application: it does not link the editor or core.
#include <wx/wx.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/timer.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <cstring>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

class PeerApp final : public wxApp {
public:
    bool OnInit() override {
        wxLog::SetActiveTarget(new wxLogStderr);
        if (argc != 2) return false;
        directory_ = argv[1];
        frame_ = new wxFrame(nullptr, wxID_ANY, "wxnotepad clipboard test peer");
        text_ = new wxTextCtrl(frame_, wxID_ANY, {}, wxDefaultPosition, wxSize(400, 180), wxTE_MULTILINE);
        text_->Bind(wxEVT_KEY_UP, [this](wxKeyEvent& event) {
            if (event.GetKeyCode() == WXK_SHIFT) gestureReceived_ = true;
            event.Skip();
        });
        frame_->Show();
        timer_.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Poll(); });
        timer_.Start(20);
        started_ = wxGetUTCTimeMillis();
        return true;
    }

private:
    wxString directory_;
    wxFrame* frame_ = nullptr;
    wxTextCtrl* text_ = nullptr;
    wxTimer timer_;
    wxLongLong started_;
    bool pendingPaste_ = false;
    wxString pendingCommand_;
    wxLongLong activationStarted_;
    bool gestureRequested_ = false;
    bool gestureReceived_ = false;
    void Respond(const wxString& value) {
        {
            wxFFile file(directory_ + "/response.tmp", "wb");
            file.Write(value, wxConvUTF8);
        }
        wxRenameFile(directory_ + "/response.tmp", directory_ + "/response", true);
    }
    void Poll() {
        if (wxGetUTCTimeMillis() - started_ > 90000) {
            frame_->Destroy();
            ExitMainLoop();
            return;
        }
        if (!pendingCommand_.empty()) {
            if (!frame_->IsActive() || !text_->HasFocus()) {
                if (wxGetUTCTimeMillis() - activationStarted_ > 3000) {
                    pendingCommand_.clear();
                    Respond("native focus unavailable");
                }
                return;
            }
            wxString focusFile;
            if (wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile)) {
                // Focus can arrive before the compositor delivers a key serial.
                // Match the editor fixture: receive a native gesture before
                // changing Wayland clipboard ownership in this process too.
                if (!gestureRequested_) {
                    gestureRequested_ = true;
                    activationStarted_ = wxGetUTCTimeMillis();
                    wxFFile file(focusFile, "wb");
                    file.Write("gesture");
                    return;
                }
                if (!gestureReceived_) {
                    if (wxGetUTCTimeMillis() - activationStarted_ > 3000) {
                        pendingCommand_.clear();
                        Respond("native gesture unavailable");
                    }
                    return;
                }
                wxFFile file(focusFile, "wb");
            }
            const auto command = pendingCommand_;
            pendingCommand_.clear();
            frame_->SetTitle("wxnotepad clipboard test peer");
            Perform(command);
            return;
        }
        if (pendingPaste_) {
            // GTK delivers the native clipboard request asynchronously.
            if (!text_->GetValue().empty()) {
                pendingPaste_ = false;
                Respond(text_->GetValue());
            }
            return;
        }
        const auto request = directory_ + "/request";
        if (!wxFileExists(request)) return;
        wxString command;
        {
            wxFFile file(request, "rb");
            file.ReadAll(&command, wxConvUTF8);
        }
        wxRemoveFile(request);
        if (command == "copy" || command == "wxcopy" || command == "nontext" || command == "paste" ||
            command.StartsWith("shape")) {
            frame_->SetTitle(wxString::Format("wxnotepad clipboard focus: peer [%lu]", wxGetProcessId()));
            wxString focusFile;
            if (wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile)) {
                wxFFile file(focusFile, "wb");
                file.Write("peer");
            }
            frame_->Raise();
            text_->SetFocus();
            activationStarted_ = wxGetUTCTimeMillis();
            gestureRequested_ = false;
            gestureReceived_ = false;
            pendingCommand_ = command;
            return;
        }
        Perform(command);
    }
    void Perform(const wxString& command) {
        if (command == "shape-read") {
            wxString shape = "stream";
            wxString text;
#ifdef __WXMSW__
            if (!::OpenClipboard(static_cast<HWND>(frame_->GetHandle()))) {
                Respond("failed");
                return;
            }
            // Observe the native payload before wxTextDataObject normalizes EOLs.
            const auto memory = ::GetClipboardData(CF_UNICODETEXT);
            const auto* bytes = static_cast<const wchar_t*>(::GlobalLock(memory));
            if (!bytes) {
                ::CloseClipboard();
                Respond("failed");
                return;
            }
            const auto capacity = ::GlobalSize(memory) / sizeof(wchar_t);
            size_t length = 0;
            while (length < capacity && bytes[length] != L'\0')
                ++length;
            text = wxString(bytes, length);
            ::GlobalUnlock(memory);
            if (::IsClipboardFormatAvailable(::RegisterClipboardFormatW(L"MSDEVLineSelect")) &&
                ::IsClipboardFormatAvailable(
                    ::RegisterClipboardFormatW(L"VisualStudioEditorOperationsLineCutCopyClipboardTag")))
                shape = "line";
            if (::IsClipboardFormatAvailable(::RegisterClipboardFormatW(L"MSDEVColumnSelect"))) {
                const auto memory = ::GetClipboardData(::RegisterClipboardFormatW(L"Borland IDE Block Type"));
                const auto* bytes = static_cast<const unsigned char*>(::GlobalLock(memory));
                if (bytes && ::GlobalSize(memory) == 1 && bytes[0] == 2) shape = "rectangle";
                if (bytes) ::GlobalUnlock(memory);
            }
            ::CloseClipboard();
#elif defined(__WXGTK__)
            auto* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
            auto* data = gtk_clipboard_wait_for_contents(clipboard, gdk_atom_intern_static_string("UTF8_STRING"));
            if (!data) {
                Respond("failed");
                return;
            }
            int length = gtk_selection_data_get_length(data);
            const auto* bytes = reinterpret_cast<const char*>(gtk_selection_data_get_data(data));
            std::fprintf(stderr, "Native peer UTF8_STRING length=%d tail=%d,%d\n", length,
                         length >= 2 ? bytes[length - 2] : -1, length >= 1 ? bytes[length - 1] : -1);
            if (length >= 2 && bytes[length - 1] == '\0' && bytes[length - 2] == '\n') {
                shape = "rectangle";
                --length;
            }
            text = wxString::FromUTF8(bytes, length);
            gtk_selection_data_free(data);
            if (shape != "rectangle" &&
                gtk_clipboard_wait_is_target_available(clipboard, gdk_atom_intern_static_string("MSDEVLineSelect")) &&
                gtk_clipboard_wait_is_target_available(
                    clipboard, gdk_atom_intern_static_string("VisualStudioEditorOperationsLineCutCopyClipboardTag")))
                shape = "line";
#endif
            wxFFile file(directory_ + "/received.txt", "wb");
            if (!file.Write(text, wxConvUTF8)) {
                Respond("failed");
                return;
            }
            file.Close();
            Respond(shape);
        } else if (command.StartsWith("shape-")) {
            const bool rectangle = command == "shape-rectangle" || command == "shape-both";
            const bool line = command == "shape-line" || command == "shape-both";
            const bool vsLine = command == "shape-vsline";
            const auto text = wxString::FromUTF8(rectangle ? "中文\tX\n日本\tY\n" : "中文\tX\n日本\tY");
#ifdef __WXMSW__
            if (!::OpenClipboard(static_cast<HWND>(frame_->GetHandle()))) {
                Respond("failed");
                return;
            }
            const auto value = text.ToStdWstring();
            auto memory = ::GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
            auto* bytes = ::GlobalLock(memory);
            if (!bytes) {
                if (memory) ::GlobalFree(memory);
                ::CloseClipboard();
                Respond("failed");
                return;
            }
            std::memcpy(bytes, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
            ::GlobalUnlock(memory);
            if (!::EmptyClipboard() || !::SetClipboardData(CF_UNICODETEXT, memory)) {
                ::GlobalFree(memory);
                ::CloseClipboard();
                Respond("failed");
                return;
            }
            const auto tag = [](const wchar_t* name) { ::SetClipboardData(::RegisterClipboardFormatW(name), nullptr); };
            if (rectangle) tag(L"MSDEVColumnSelect");
            if (line) tag(L"MSDEVLineSelect");
            if (vsLine) tag(L"VisualStudioEditorOperationsLineCutCopyClipboardTag");
            if (command == "shape-borland" || command == "shape-malformed") {
                memory = ::GlobalAlloc(GMEM_MOVEABLE, 1);
                auto* marker = static_cast<unsigned char*>(::GlobalLock(memory));
                if (marker) {
                    *marker = command == "shape-borland" ? 2 : 1;
                    ::GlobalUnlock(memory);
                    if (!::SetClipboardData(::RegisterClipboardFormatW(L"Borland IDE Block Type"), memory))
                        ::GlobalFree(memory);
                } else if (memory) {
                    ::GlobalFree(memory);
                }
            }
            ::CloseClipboard();
#elif defined(__WXGTK__)
            auto* value = new std::string(text.ToUTF8().data());
            if (rectangle) value->push_back('\0');
            auto* list = gtk_target_list_new(nullptr, 0);
            gtk_target_list_add_text_targets(list, 0);
            if (line) gtk_target_list_add(list, gdk_atom_intern_static_string("MSDEVLineSelect"), 0, 1);
            if (vsLine)
                gtk_target_list_add(
                    list, gdk_atom_intern_static_string("VisualStudioEditorOperationsLineCutCopyClipboardTag"), 0, 1);
            gint count = 0;
            auto* targets = gtk_target_table_new_from_list(list, &count);
            const bool copied = gtk_clipboard_set_with_data(
                gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), targets, count,
                [](GtkClipboard*, GtkSelectionData* data, guint info, gpointer owner) {
                    const auto* value = static_cast<const std::string*>(owner);
                    if (info)
                        gtk_selection_data_set(data, gtk_selection_data_get_target(data), 8, nullptr, 0);
                    else
                        gtk_selection_data_set_text(data, value->data(), static_cast<int>(value->size()));
                },
                [](GtkClipboard*, gpointer owner) { delete static_cast<std::string*>(owner); }, value);
            gtk_target_table_free(targets, count);
            gtk_target_list_unref(list);
            if (!copied) {
                delete value;
                Respond("failed");
                return;
            }
#endif
            Respond("ready");
        } else if (command == "ready") {
            Respond("ready");
        } else if (command == "wxcopy") {
            wxClipboardLocker lock;
            Respond(!(!lock) && wxTheClipboard->SetData(
                                    new wxTextDataObject(wxString::FromUTF8("peer English 中文 😀\n第二行\nlast")))
                        ? "ready"
                        : "failed");
        } else if (command == "nontext") {
            wxClipboardLocker lock;
            auto* object = new wxCustomDataObject(wxDataFormat("application/x-wxnotepad-test"));
            object->SetData(5, "owned");
            Respond(!(!lock) && wxTheClipboard->SetData(object) ? "ready" : "failed");
        } else if (command == "copy") {
            text_->SetValue(wxString::FromUTF8("peer English 中文 😀\n第二行\nlast"));
            text_->SelectAll();
            text_->Copy();
            Respond("ready");
        } else if (command == "paste") {
            text_->Clear();
            pendingPaste_ = true;
            text_->Paste();
#ifdef __WXMSW__
        } else if (command == "hold") {
            Respond(::OpenClipboard(static_cast<HWND>(frame_->GetHandle())) ? "held" : "failed");
        } else if (command == "release") {
            ::CloseClipboard();
            Respond("released");
#endif
        } else if (command == "quit") {
            Respond("bye");
            frame_->Destroy();
            ExitMainLoop();
        }
    }
};
wxIMPLEMENT_APP(PeerApp);
