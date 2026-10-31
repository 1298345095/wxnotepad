#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#ifdef __WXMSW__
#include <windows.h>
#elif defined(__WXGTK__)
#include <gtk/gtk.h>
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open(), "saved file must exist");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
wxString Filename(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
std::string Text(EditorControl* editor) {
    const auto length = static_cast<size_t>(editor->Send(Scintilla::Message::GetLength));
    std::string text(length + 1, '\0');
    editor->Send(Scintilla::Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
#ifdef __WXGTK__
// Optional native-widget probe. It drives the real GTK chooser and confirmation
// buttons without relying on WSLg's cross-desktop keyboard/pointer routing.
bool ActivateButton(GtkWidget* widget, const char* label) {
    if (GTK_IS_BUTTON(widget)) {
        const char* text = gtk_button_get_label(GTK_BUTTON(widget));
        if (text && std::string(text).find(label) != std::string::npos) {
            gtk_button_clicked(GTK_BUTTON(widget));
            return true;
        }
    }
    if (!GTK_IS_CONTAINER(widget)) return false;
    GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
    bool activated = false;
    for (GList* child = children; child && !activated; child = child->next)
        activated = ActivateButton(GTK_WIDGET(child->data), label);
    g_list_free(children);
    return activated;
}
struct GtkDialogProbe {
    wxString mode;
    wxString path;
    int stage = 0;
    static gboolean Tick(gpointer data) {
        auto& probe = *static_cast<GtkDialogProbe*>(data);
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget)) continue;
            if (GTK_IS_FILE_CHOOSER(widget)) {
                if (probe.stage == 0) {
                    if (probe.mode.EndsWith("cancel")) {
                        probe.stage = 4;
                        gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_CANCEL);
                    } else {
                        const wxFileName target(probe.path);
                        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(widget), target.GetPath().utf8_str());
                        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(widget), target.GetFullName().utf8_str());
                        probe.stage = 1;
                    }
                } else if (probe.stage == 1) {
                    probe.stage = 2;
                    gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_ACCEPT);
                } else if (probe.stage == 3) {
                    probe.stage = 4;
                    gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_CANCEL);
                }
            } else if (GTK_IS_MESSAGE_DIALOG(widget) && probe.stage == 2) {
                if (probe.mode.EndsWith("no")) {
                    probe.stage = 3;
                    ActivateButton(widget, "Cancel");
                } else {
                    probe.stage = 4;
                    ActivateButton(widget, "Replace");
                }
            }
        }
        g_list_free(windows);
        return G_SOURCE_CONTINUE;
    }
};
#endif
class FileApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        // Separate native dialog probe, driven interactively outside CTest.
        if (argc == 3) {
            const wxString expectedPath = argv[2];
            const wxString mode = argv[1];
            const bool save = mode.EndsWith("save") || mode.EndsWith("yes");
            const std::string text = "Native 中文\r\nsecond\nthird\r";
            frame->Editor()->Send(Scintilla::Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
            frame->Show();
            CallAfter([this, frame, save, expectedPath, mode] {
                try {
#ifdef __WXGTK__
                    GtkDialogProbe probe{mode, expectedPath};
                    const guint timer =
                        mode.StartsWith("--gtk-") ? g_timeout_add(250, GtkDialogProbe::Tick, &probe) : 0;
                    if (timer) g_source_set_can_recurse(g_main_context_find_source_by_id(nullptr, timer), TRUE);
#endif
                    wxCommandEvent command(wxEVT_MENU, wxID_SAVE);
                    frame->GetEventHandler()->ProcessEvent(command);
#ifdef __WXGTK__
                    if (timer) g_source_remove(timer);
#endif
                    Require(frame->DocumentPath() == (save ? expectedPath : wxString()),
                            "native dialog must update identity only on success");
                    Require(bool(frame->Editor()->Send(Scintilla::Message::GetModify)) == !save,
                            "native dialog must clear dirty state only on success");
                    std::puts(save ? "PASS: native Save accepted" : "PASS: native Save cancelled");
                    result_ = 0;
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "FAIL: %s\n", error.what());
                }
                frame->Destroy();
                ExitMainLoop();
            });
            return true;
        }
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-file-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto path = directory / "empty.txt";
                const wxString filename = Filename(path);
                const auto result = frame->SaveFirst(filename);
                Require(result.status == SaveStatus::Saved, "first save must succeed");
                Require(Bytes(path).empty(), "empty UTF-8 file must have no BOM");
                Require(frame->DocumentPath() == filename, "successful save must establish file identity");
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify),
                        "successful save must establish save point");
                Require(!frame->GetTitle().StartsWith("*") && frame->GetTitle().Contains("empty.txt"),
                        "title must show saved identity");
                std::puts("PASS: first empty UTF-8 save and file identity");
                auto* textFrame = new EditorFrame;
                auto* editor = textFrame->Editor();
                const std::string text = "A中文😀\r\nsecond\nthird\rlast";
                editor->Send(Scintilla::Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
                const auto textPath = directory / u8"中文.txt";
                Require(textFrame->SaveFirst(Filename(textPath)).status == SaveStatus::Saved,
                        "UTF-8 text save must succeed");
                Require(Bytes(textPath) == "A\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80\r\nsecond\nthird\rlast",
                        "saved bytes must preserve Unicode and actual mixed EOL with no BOM");
                Require(editor->Send(Scintilla::Message::GetEOLMode) == 0,
                        "new documents must explicitly use original CRLF insertion mode");
                Require(!editor->Send(Scintilla::Message::GetModify), "text save must clear modified state");
                editor->Send(Scintilla::Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                Require(editor->Send(Scintilla::Message::GetModify), "edit after save must be dirty");
                editor->Send(Scintilla::Message::Undo);
                Require(!editor->Send(Scintilla::Message::GetModify), "Undo to saved text must be clean");
                editor->Send(Scintilla::Message::Redo);
                Require(editor->Send(Scintilla::Message::GetModify), "Redo away from save point must be dirty");
                Require(textFrame->Save().status == SaveStatus::Saved, "saving a named document must reuse its path");
                Require(Bytes(textPath) == "A\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80\r\nsecond\nthird\rlast!",
                        "named save must replace complete bytes at the existing path");
                Require(!editor->Send(Scintilla::Message::GetModify), "named save must establish a new save point");
                textFrame->Destroy();
                std::puts("PASS: exact UTF-8/mixed-EOL bytes and savepoint history");
                auto* pending = new EditorFrame;
                pending->Editor()->Send(Scintilla::Message::AddText, text.size(),
                                        reinterpret_cast<intptr_t>(text.data()));
                const auto unchanged = [&] {
                    Require(pending->DocumentPath().empty(), "cancel/failure must retain unnamed identity");
                    Require(Text(pending->Editor()) == text, "cancel/failure must retain document text");
                    Require(pending->Editor()->Send(Scintilla::Message::GetModify),
                            "cancel/failure must retain dirty state");
                    Require(pending->Editor()->Send(Scintilla::Message::CanUndo),
                            "cancel/failure must retain edit history");
                };
                Require(pending->SaveFirst(std::nullopt).status == SaveStatus::Cancelled,
                        "cancelled save must return Cancelled");
                unchanged();
                const auto missing = pending->SaveFirst(Filename(directory / "missing" / "target.txt"));
                Require(missing.status == SaveStatus::Failed && !missing.error.empty(),
                        "temporary-file creation failure must explain the error");
                Require(!std::filesystem::exists(directory / "missing"),
                        "failed save must not create destination parents");
                unchanged();
                const auto targetDirectory = directory / "directory-target";
                std::filesystem::create_directory(targetDirectory);
                {
                    std::ofstream(targetDirectory / "sentinel", std::ios::binary) << "keep";
                }
                const auto replacement = pending->SaveFirst(Filename(targetDirectory));
                Require(replacement.status == SaveStatus::Failed && !replacement.error.empty(),
                        "failed replacement must report failure");
                Require(Bytes(targetDirectory / "sentinel") == "keep",
                        "replacement failure must preserve existing target");
                unchanged();
                const auto protectedDirectory = directory / "protected";
                std::filesystem::create_directory(protectedDirectory);
                const auto protectedPath = protectedDirectory / "sentinel.txt";
                {
                    std::ofstream(protectedPath, std::ios::binary) << "original bytes";
                }
#ifdef __WXMSW__
                HANDLE lock = CreateFileW(protectedPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
                Require(lock != INVALID_HANDLE_VALUE, "must lock sentinel against replacement");
                const auto blocked = pending->SaveFirst(Filename(protectedPath));
                CloseHandle(lock);
#else
                std::filesystem::permissions(protectedDirectory,
                                             std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
                const auto blocked = pending->SaveFirst(Filename(protectedPath));
                std::filesystem::permissions(protectedDirectory, std::filesystem::perms::owner_all);
#endif
                Require(blocked.status == SaveStatus::Failed, "blocked destination must fail saving");
                Require(Bytes(protectedPath) == "original bytes", "failed save must not truncate original file");
                unchanged();
                for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
                    Require(entry.path().filename().string().find(".wxnotepad-") == std::string::npos,
                            "failure must clean sibling temporary files");
                Require(pending->SaveFirst(Filename(protectedPath)).status == SaveStatus::Saved,
                        "approved overwrite must succeed after failure");
                Require(Bytes(protectedPath) == "A\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80\r\nsecond\nthird\rlast",
                        "approved overwrite must write complete new bytes");
                pending->Destroy();
                std::puts(
                    "PASS: cancellation, creation/replacement failures, preserved sentinel and approved overwrite");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            std::filesystem::remove_all(directory);
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnExit() override { return result_; }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(FileApp);
