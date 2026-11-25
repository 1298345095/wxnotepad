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
#include "app/dialogs/print_page_preview.h"
#include "app/dialogs/recent_files_dialog.h"
#include "app/dialogs/search_feedback_dialog.h"
#include "app/dialogs/sort_lines_dialog.h"
#include "app/dialogs/tab_settings_dialog.h"
#include "app/dialogs/word_wrap_settings_dialog.h"
#include "app/word_wrap_state.h"
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
#include <wx/dcmemory.h>
#include <wx/image.h>
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
        wxTheApp->CallAfter([this, dialog] { Observe(dialog); });
        return wxID_NONE;
    }
    template <class Show> void Check(Policy policy, const Show& show, std::function<void(wxDialog&)> check = {}) {
        expected = policy;
        inspect = std::move(check);
        const int before = visited;
        show();
        Require(visited == before + 1, "each entry point opens exactly its actual production dialog");
        if (!failure.empty()) throw TestEditor::Failure(failure);
    }

private:
    // Close the modal window even when its native observation fails.
    void Observe(wxDialog* dialog) noexcept {
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

wxImage PreviewImage(const wxPrintData& data, const std::array<double, 4>& margins, const wxRect& bounds,
                     const PrintMarginDisplay& units = PrintMarginDisplay{100, 2, true}) {
    wxBitmap bitmap(260, 340);
    wxMemoryDC dc(bitmap);
    dc.SetBackground(wxBrush(wxColour(17, 34, 51)));
    dc.Clear();
    DrawPrintPagePreview(dc, bounds, data, margins, units);
    dc.SelectObject(wxNullBitmap);
    return bitmap.ConvertToImage();
}

bool PixelIs(const wxImage& image, int x, int y, const wxColour& expected) {
    return image.GetRed(x, y) == expected.Red() && image.GetGreen(x, y) == expected.Green() &&
           image.GetBlue(x, y) == expected.Blue();
}

bool PreviewInk(const wxImage& image, int x, int y) {
    // Native memory DCs antialias their one-pixel strokes. Keep the coordinate
    // checks exact while distinguishing dark paint from the untouched canvas.
    constexpr int DarkChannelLimit = 128;
    return !PixelIs(image, x, y, wxColour(17, 34, 51)) && image.GetRed(x, y) < DarkChannelLimit &&
           image.GetGreen(x, y) < DarkChannelLimit && image.GetBlue(x, y) < DarkChannelLimit;
}

bool HasPreviewInk(const wxImage& image, const wxRect& area) {
    for (int y = area.y; y <= area.GetBottom(); ++y)
        for (int x = area.x; x <= area.GetRight(); ++x)
            if (PreviewInk(image, x, y)) return true;
    return false;
}

void PagePreviewPixels() {
    // At this literal A4 size one logical pixel represents one millimeter.
    // Check actual raster output rather than duplicating the layout arithmetic.
    const wxRect a4Bounds(10, 10, 210, 297);
    wxPrintData a4;
    a4.SetPaperId(wxPAPER_A4);
    a4.SetPaperSize(wxDefaultSize);
    a4.SetOrientation(wxPORTRAIT);
    const auto portrait = PreviewImage(a4, {20, 20, 20, 20}, a4Bounds);
    if (wxString path; wxGetEnv("WXNOTEPAD_PAGE_PREVIEW_IMAGE", &path))
        Require(portrait.SaveFile(path, wxBITMAP_TYPE_PNG), "retain the actual native preview raster");
    Require(PreviewInk(portrait, 10, 10) && PreviewInk(portrait, 219, 306),
            "standard A4 paper uses its complete database dimensions");
    Require(PixelIs(portrait, 20, 20, *wxWHITE) && HasPreviewInk(portrait, wxRect(30, 30, 170, 1)),
            "20 mm margins leave white paper outside the dotted printable area");
    Require(PixelIs(portrait, 5, 5, wxColour(17, 34, 51)), "preview preserves the surrounding native background");
    const auto inches = PreviewImage(a4, {1, 1, 1, 1}, a4Bounds, {1000, 3, false});
    Require(HasPreviewInk(inches, wxRect(35, 35, 160, 1)) && !HasPreviewInk(inches, wxRect(20, 20, 190, 1)),
            "one displayed inch uses 25.4 mm, not the stored thousandths-of-an-inch count");
    for (const std::array<double, 4> margins : {std::array<double, 4>{300, 0, 0, 0}, {0, 400, 0, 0}}) {
        const auto oversized = PreviewImage(a4, margins, a4Bounds);
        Require(!HasPreviewInk(oversized, wxRect(20, 20, 190, 270)),
                "margins exceeding either paper dimension omit the printable area");
    }
    wxPrintData custom;
    custom.SetPaperId(wxPAPER_NONE);
    custom.SetPaperSize(wxSize(120, 80));
    custom.SetOrientation(wxLANDSCAPE);
    const auto landscape = PreviewImage(custom, {10, 10, 10, 10}, wxRect(10, 10, 240, 160));
    Require(PixelIs(landscape, 10, 20, wxColour(17, 34, 51)) && PixelIs(landscape, 80, 20, *wxWHITE) &&
                HasPreviewInk(landscape, wxRect(77, 10, 1, 160)),
            "landscape rotates custom dimensions and centers the page at the correct aspect ratio");
    for (const wxSize size : {wxSize(0, 0), wxSize(0, 80), wxSize(120, 0)}) {
        custom.SetPaperSize(size);
        const auto unknown = PreviewImage(custom, {}, a4Bounds);
        Require(!HasPreviewInk(unknown, a4Bounds), "unknown paper dimensions leave the preview background untouched");
    }
}

void Validate(EditorFrame& frame, OriginalSettings& preferences) {
    PagePreviewPixels();
    InspectModal hook;
    auto* editor = frame.Editor();
    AlignmentMode alignment{};
    hook.Check(Policy::Fixed, [&frame, &alignment] { ShowAlignLinesDialog(&frame, alignment); });
    unsigned sort = 0;
    hook.Check(Policy::Fixed, [&frame, &sort] { ShowSortLinesDialog(&frame, sort, false); });
    int column = 80;
    hook.Check(Policy::Fixed, [&frame, &column] { ShowColumnWrapDialog(&frame, column, 80); });
    int mode = 1;
    hook.Check(Policy::Fixed, [&frame, &column, &mode] { ShowLongLineSettingsDialog(&frame, column, mode); });
    hook.Check(Policy::Fixed, [&frame, editor] { ShowGoToLineDialog(&frame, *editor); });
    hook.Check(Policy::Fixed, [&frame] {
        WordWrapOptions wrapOptions;
        ShowWordWrapSettingsDialog(&frame, wrapOptions);
    });
    IndentationSettings indentation(preferences);
    hook.Check(Policy::Fixed, [&frame, &preferences, &indentation] {
        ShowTabSettingsDialog(&frame, preferences, SyntaxLanguage::PlainText, indentation);
    });
    hook.Check(Policy::Fixed, [&frame, &preferences] { ShowAutoCompletionSettingsDialog(&frame, preferences); });
    hook.Check(Policy::Fixed, [&frame, &preferences] { ShowAutoSaveSettingsDialog(&frame, preferences.autoSave); });
    EncodingOptions encoding;
    hook.Check(Policy::Fixed, [&frame, &encoding] { ChooseDefaultEncoding(&frame, encoding); });
    hook.Check(Policy::Both, [&frame] { ChooseCurrentEncoding(&frame, EncodingChoice(FileEncoding::Utf8)); });
    hook.Check(Policy::Both, [&frame] { ChooseReloadEncoding(&frame, FileEncoding::Utf8); });
    hook.Check(Policy::Fixed, [&frame] { ChooseFileWatchingOptions(&frame, {}); });
    hook.Check(Policy::Fixed, [&frame] {
        LineEndingOptionsDialog dialog(&frame, {0, true, {}});
        dialog.Choose();
    });
    hook.Check(Policy::Fixed, [&frame] {
        LineEndingConversionDialog dialog(&frame, {}, Scintilla::EndOfLine::CrLf, true, false);
        dialog.Choose();
    });
    wxString before;
    wxString after;
    bool skip = false;
    hook.Check(
        Policy::Both, [&frame, editor, &before, &after] { ShowEncloseSelectionDialog(&frame, *editor, before, after); },
        [](wxDialog& dialog) { InputGrowth(dialog, "Insert before selection", "Insert after selection", 1, 1); });
    hook.Check(
        Policy::Both,
        [&frame, editor, &before, &after, &skip] { ShowModifyLinesDialog(&frame, *editor, before, after, skip); },
        [](wxDialog& dialog) { InputGrowth(dialog, "Prefix text to lines", "Append text to lines", 1, 1); });
    hook.Check(
        Policy::Both, [&frame, editor] { ShowInsertTagDialog(&frame, *editor); },
        [](wxDialog& dialog) { InputGrowth(dialog, "Opening tag", "Closing tag", 3, 1); });
    int historyChanges = 0;
    hook.Check(Policy::Both, [&frame, &preferences, &historyChanges] {
        RecentFilesDialog dialog(
            &frame, preferences, [&historyChanges] { ++historyChanges; },
            [&historyChanges](size_t) { ++historyChanges; });
        dialog.Choose();
    });
    Require(historyChanges == 0, "Cancel does not clear or remove recent files");
    hook.Check(Policy::Fixed, [&frame] {
        SearchFeedbackDialog dialog(&frame, "layout", _("Search finished."), true, true);
        dialog.Choose();
    });
    hook.Check(Policy::Fixed, [&frame] {
        PrintOptions options;
        options.margins = {2000, 2000, 2000, 2000};
        wxPrintData paper;
        paper.SetPaperId(wxPAPER_A4);
        ShowPrintPageSetupDialog(frame, options, paper, {100, 2, true});
        Require(options.margins == std::array{2000, 2000, 2000, 2000}, "Cancel keeps metric print margins");
    });
    hook.Check(Policy::Fixed, [&frame] {
        PrintOptions options;
        options.margins = {1000, 1000, 1000, 1000};
        wxPrintData paper;
        paper.SetPaperId(wxPAPER_A4);
        paper.SetOrientation(wxLANDSCAPE);
        ShowPrintPageSetupDialog(frame, options, paper, {1000, 3, false});
        Require(options.margins == std::array{1000, 1000, 1000, 1000}, "Cancel keeps inch-display print margins");
    });
    preferences.openWithDirectory = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    hook.Check(Policy::Both, [&frame] { frame.OpenDocumentWith(); });
    const auto menu = [&frame](const wxString& top, const wxString& item) {
        const int id = frame.GetMenuBar()->FindMenuItem(top, item);
        Require(id != wxNOT_FOUND, "the localized native menu command exists");
        NativeSearch::Command(&frame, id);
    };
    hook.Check(Policy::Horizontal, [&menu] { menu(_("&Tools"), _("&Run Command...\tCtrl+R")); });
    hook.Check(Policy::Fixed, [&menu] { menu(_("&View"), _("Zoom &Level...")); });
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
