#include "app/dialogs/align_lines_dialog.h"
#include "app/dialogs/auto_completion_settings_dialog.h"
#include "app/dialogs/auto_save_settings_dialog.h"
#include "app/dialogs/column_wrap_dialog.h"
#include "app/dialogs/document_dialogs.h"
#include "app/dialogs/enclose_selection_dialog.h"
#include "app/dialogs/go_to_line_dialog.h"
#include "app/dialogs/insert_tag_dialog.h"
#include "app/dialogs/long_line_settings_dialog.h"
#include "app/dialogs/modify_lines_dialog.h"
#include "app/dialogs/print_page_setup_dialog.h"
#include "app/dialogs/recent_files_dialog.h"
#include "app/dialogs/search_feedback_dialog.h"
#include "app/dialogs/sort_lines_dialog.h"
#include "app/dialogs/tab_settings_dialog.h"
#include "app/dialogs/word_wrap_settings_dialog.h"
#include "app/localization.h"
#include "editor/align_lines.h"
#include "settings/print_options.h"
#include "settings/settings_store.h"
#include "native_search_helpers.h"
#include <wx/control.h>
#include <wx/statbox.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <functional>
#include <memory>
#include <vector>

namespace {
enum class Policy { Fixed, Horizontal, Both };
void Require(bool value, const char* message) {
    if (!value) throw TestEditor::Failure(message);
}

void Controls(wxWindow* parent, std::vector<const wxWindow*>& controls) {
    for (auto* child : parent->GetChildren()) {
        if (!child->IsShownOnScreen() || child->IsTopLevel()) continue;
        if (wxDynamicCast(child, wxControl) && !wxDynamicCast(child, wxStaticBox))
            controls.push_back(child);
        else
            Controls(child, controls);
    }
}

void Geometry(wxDialog& dialog) {
    std::vector<const wxWindow*> controls;
    Controls(&dialog, controls);
    const wxRect client(dialog.ClientToScreen(wxPoint{}), dialog.GetClientSize());
    for (const auto* control : controls) {
        const auto area = control->GetScreenRect();
        if (!client.Contains(area))
            std::cerr << "Outside: " << control->GetName().utf8_str().data() << " area=" << area.x << ',' << area.y
                      << ',' << area.width << ',' << area.height << " client=" << client.x << ',' << client.y << ','
                      << client.width << ',' << client.height << '\n';
        Require(client.Contains(area), "every displayed control fits the actual dialog client area");
        for (const auto* other : controls) {
            if (other == control) break;
            if (other->GetScreenRect().Intersects(area)) {
                std::cerr << "Overlap: " << control->GetName().utf8_str().data() << " / "
                          << other->GetName().utf8_str().data() << '\n';
                Require(false, "translated dialog controls must not overlap");
            }
        }
    }
}

class InspectModal final : public wxModalDialogHook {
public:
    InspectModal() { Register(); }
    Policy expected = Policy::Fixed;
    std::function<void(wxDialog&)> inspect;
    std::string failure;
    int visited = 0;
    int Enter(wxDialog* dialog) override {
        wxTheApp->CallAfter([this, dialog] {
            try {
                ++visited;
                Require(dialog->IsShown(), "inspect a real shown production dialog");
                const bool resizable = (dialog->GetWindowStyleFlag() & wxRESIZE_BORDER) != 0;
                Require(resizable == (expected != Policy::Fixed), "resize axes match the original dialog policy");
#ifdef __WXMSW__
                const auto style = GetWindowLongPtrW(static_cast<HWND>(dialog->GetHandle()), GWL_STYLE);
                Require(((style & WS_THICKFRAME) != 0) == resizable, "native Windows sizing border matches policy");
#elif defined(__WXGTK__)
                Require((gtk_window_get_resizable(GTK_WINDOW(dialog->GetHandle())) != FALSE) == resizable,
                        "native GTK resizable property matches policy");
#endif
                Geometry(*dialog);
                if (inspect) inspect(*dialog);
                if (resizable) {
                    const auto minimum = dialog->GetMinSize();
                    Require(minimum == dialog->GetSize(), "initial translated layout defines minimum track size");
                    const bool horizontal = expected == Policy::Horizontal;
                    Require(dialog->GetMaxSize().y == (horizontal ? minimum.y : -1),
                            "the original horizontal-only policy fixes the fitted height");
                    dialog->SetSize(minimum + dialog->FromDIP(wxSize(120, horizontal ? 0 : 80)));
                    dialog->Layout();
                    wxYield();
                    Geometry(*dialog);
                    dialog->SetSize(minimum);
                    dialog->Layout();
                    Geometry(*dialog);
                }
                std::cout << "PASS dialog: " << dialog->GetTitle().utf8_str().data() << '\n';
            } catch (const std::exception& error) {
                failure = error.what();
            }
            dialog->EndModal(wxID_CANCEL);
        });
        return wxID_NONE;
    }
    void Check(Policy policy, const std::function<void()>& show, std::function<void(wxDialog&)> check = {}) {
        expected = policy;
        inspect = std::move(check);
        const int before = visited;
        show();
        Require(visited == before + 1, "each entry point opens exactly its actual production dialog");
        if (!failure.empty()) throw TestEditor::Failure(failure);
    }
};

void InputGrowth(wxDialog& dialog, const char* firstName, const char* secondName, int firstParts, int secondParts) {
    const auto* first = wxWindow::FindWindowByName(firstName, &dialog);
    const auto* second = wxWindow::FindWindowByName(secondName, &dialog);
    Require(first && second, "original paired text fields are present");
    const auto firstSize = first->GetSize();
    const auto secondSize = second->GetSize();
    const auto initial = dialog.GetSize();
    const int delta = dialog.FromDIP(80);
    dialog.SetSize(initial + wxSize(0, delta));
    dialog.Layout();
    const int firstGrowth = first->GetSize().y - firstSize.y;
    const int secondGrowth = second->GetSize().y - secondSize.y;
    Require(firstGrowth + secondGrowth == delta && firstGrowth * secondParts == secondGrowth * firstParts,
            "vertical growth follows the original 50/50 or 75/25 allocation");
    dialog.SetSize(initial);
    dialog.Layout();
}

void Validate(EditorFrame& frame, OriginalSettings& preferences) {
    InspectModal hook;
    auto* editor = frame.Editor();
    AlignmentMode alignment{};
    hook.Check(Policy::Fixed, [&] { ShowAlignLinesDialog(&frame, alignment); });
    unsigned sort = 0;
    hook.Check(Policy::Fixed, [&] { ShowSortLinesDialog(&frame, sort, false); });
    int column = 80;
    hook.Check(Policy::Fixed, [&] { ShowColumnWrapDialog(&frame, column, 80); });
    int mode = 1;
    hook.Check(Policy::Fixed, [&] { ShowLongLineSettingsDialog(&frame, column, mode); });
    hook.Check(Policy::Fixed, [&] { ShowGoToLineDialog(&frame, *editor); });
    hook.Check(Policy::Fixed, [&] { ShowWordWrapSettingsDialog(&frame, preferences); });
    IndentationSettings indentation(preferences);
    hook.Check(Policy::Fixed,
               [&] { ShowTabSettingsDialog(&frame, preferences, SyntaxLanguage::PlainText, indentation); });
    hook.Check(Policy::Fixed, [&] { ShowAutoCompletionSettingsDialog(&frame, preferences); });
    hook.Check(Policy::Fixed, [&] { ShowAutoSaveSettingsDialog(&frame, preferences.autoSave); });
    EncodingOptions encoding;
    hook.Check(Policy::Fixed, [&] { ChooseDefaultEncoding(&frame, encoding); });
    hook.Check(Policy::Both, [&] { ChooseCurrentEncoding(&frame, EncodingChoice(FileEncoding::Utf8)); });
    hook.Check(Policy::Both, [&] { ChooseReloadEncoding(&frame, FileEncoding::Utf8); });
    hook.Check(Policy::Fixed, [&] { ChooseFileWatchingOptions(&frame, {}); });
    hook.Check(Policy::Fixed, [&] {
        LineEndingOptionsDialog dialog(&frame, {0, true, {}});
        dialog.Choose();
    });
    hook.Check(Policy::Fixed, [&] {
        LineEndingConversionDialog dialog(&frame, {}, Scintilla::EndOfLine::CrLf, true, false);
        dialog.Choose();
    });
    wxString before;
    wxString after;
    bool skip = false;
    hook.Check(
        Policy::Both, [&] { ShowEncloseSelectionDialog(&frame, *editor, before, after); },
        [](wxDialog& dialog) { InputGrowth(dialog, "Insert before selection", "Insert after selection", 1, 1); });
    hook.Check(
        Policy::Both, [&] { ShowModifyLinesDialog(&frame, *editor, before, after, skip); },
        [](wxDialog& dialog) { InputGrowth(dialog, "Prefix text to lines", "Append text to lines", 1, 1); });
    hook.Check(
        Policy::Both, [&] { ShowInsertTagDialog(&frame, *editor); },
        [](wxDialog& dialog) { InputGrowth(dialog, "Opening tag", "Closing tag", 3, 1); });
    hook.Check(Policy::Both, [&] {
        RecentFilesDialog dialog(&frame, preferences, [] {}, [](size_t) {});
        dialog.Choose();
    });
    hook.Check(Policy::Fixed, [&] {
        SearchFeedbackDialog dialog(&frame, "layout", _("Search finished."), true, true);
        dialog.Choose();
    });
    hook.Check(Policy::Fixed, [&] {
        PrintOptions options;
        ShowPrintPageSetupDialog(frame, options, wxPrintData{}, {100, 2, true});
    });
    preferences.openWithDirectory = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    hook.Check(Policy::Both, [&] { frame.OpenDocumentWith(); });
    const auto menu = [&frame](const wxString& top, const wxString& item) {
        const int id = frame.GetMenuBar()->FindMenuItem(top, item);
        Require(id != wxNOT_FOUND, "the localized native menu command exists");
        NativeSearch::Command(&frame, id);
    };
    hook.Check(Policy::Horizontal, [&] { menu(_("&Tools"), _("&Run Command...\tCtrl+R")); });
    hook.Check(Policy::Fixed, [&] { menu(_("&View"), _("Zoom &Level...")); });
    std::cout << "PASS: all " << hook.visited << " production modal layouts\n";
}

class DialogApp final : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        AppLocalization::Initialize(argc > 1 ? static_cast<uint16_t>(wxAtoi(argv[1])) : 1033);
        settings_ = std::make_unique<SettingsStore>();
        auto frame = std::make_unique<EditorFrame>(settings_.get());
        frame->Show();
        SetTopWindow(frame.release());
        CallAfter([this] {
            auto* frame = static_cast<EditorFrame*>(GetTopWindow());
            try {
                Validate(*frame, settings_->values);
                result_ = 0;
            } catch (const std::exception& error) {
                std::cerr << "FAIL dialog layout: " << error.what() << '\n';
            }
            frame->Destroy();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(DialogApp);
