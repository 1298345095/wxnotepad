#include "app/editor_frame.h"
#include "app/localization.h"
#include "app/status_fields.h"
#include "native_paint_helpers.h"
#include "native_search_helpers.h"
#include "settings/settings_store.h"
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void ReplacementLayout(EditorFrame* frame) {
    for (int command : {wxID_FIND, wxID_REPLACE, wxID_FIND, wxID_REPLACE}) {
        NativeSearch::Command(frame, command);
        auto* input = wxWindow::FindWindowByName("Replacement text", frame);
        auto* wrap = wxWindow::FindWindowByName("Wrap around", frame);
        Require(input && wrap, "actual search dialog exposes replacement and wrap controls");
        auto* dialog = input->GetParent();
        wxWindow* label = nullptr;
        for (auto* child : dialog->GetChildren()) {
            if (wxDynamicCast(child, wxStaticText) && child->GetLabel() == _("Replace &with:")) label = child;
        }
        Require(label, "actual search dialog has its translated replacement label");
        const bool replace = command == wxID_REPLACE;
        Require(input->IsShown() == replace && label->IsShown() == replace,
                "replacement controls follow the current Find/Replace mode");
        if (!replace) continue;
        const auto labelRect = label->GetRect();
        const auto inputRect = input->GetRect();
        const auto wrapRect = wrap->GetRect();
        std::printf("Replace layout: label=%d..%d input=%d..%d wrap=%d..%d\n", labelRect.y, labelRect.GetBottom(),
                    inputRect.y, inputRect.GetBottom(), wrapRect.y, wrapRect.GetBottom());
        Require(labelRect.GetBottom() < inputRect.y && inputRect.GetBottom() < wrapRect.y,
                "Replace with label, replacement input and Wrap around must occupy separate ordered rows");
        Require(dialog->GetClientRect().Contains(labelRect) && dialog->GetClientRect().Contains(inputRect) &&
                    dialog->GetClientRect().Contains(wrapRect),
                "all replacement rows remain inside the shown dialog");
    }
}
#ifdef __WXGTK__
class StatusCapture final {
public:
    explicit StatusCapture(wxStatusBar* status) : status_(status) {
        handler_ = g_signal_connect_after(status_->GetConnectWidget(), "draw", G_CALLBACK(Draw), this);
    }
    ~StatusCapture() { g_signal_handler_disconnect(status_->GetConnectWidget(), handler_); }
    wxImage Read() {
        status_->Refresh();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!image_.IsOk() && std::chrono::steady_clock::now() < deadline) {
            status_->Update();
            wxYield();
            if (!image_.IsOk()) wxMilliSleep(1);
        }
        Require(image_.IsOk(), "capture actual status pixels after the native GTK draw");
        return image_;
    }
    double scaleX = 1;
    double scaleY = 1;

private:
    static gboolean Draw(GtkWidget*, cairo_t* context, gpointer data) {
        static_cast<StatusCapture*>(data)->Sample(context);
        return FALSE;
    }
    void Sample(cairo_t* context) {
        auto* surface = cairo_get_group_target(context);
        cairo_surface_flush(surface);
        double deviceX, deviceY, offsetX, offsetY;
        cairo_surface_get_device_scale(surface, &deviceX, &deviceY);
        cairo_surface_get_device_offset(surface, &offsetX, &offsetY);
        cairo_matrix_t matrix;
        cairo_get_matrix(context, &matrix);
        scaleX = deviceX * matrix.xx;
        scaleY = deviceY * matrix.yy;
        double x = 0, y = 0;
        cairo_user_to_device(context, &x, &y);
        const auto size = status_->GetClientSize();
        const cairo_rectangle_int_t area{
            static_cast<int>(std::lround(x * deviceX + offsetX)), static_cast<int>(std::lround(y * deviceY + offsetY)),
            static_cast<int>(std::lround(size.x * scaleX)), static_cast<int>(std::lround(size.y * scaleY))};
        auto* mapped = cairo_surface_map_to_image(surface, &area);
        if (cairo_surface_status(mapped) == CAIRO_STATUS_SUCCESS) {
            image_.Create(area.width, area.height);
            const auto* pixels = cairo_image_surface_get_data(mapped);
            const int stride = cairo_image_surface_get_stride(mapped);
            for (int row = 0; row < area.height; ++row) {
                for (int column = 0; column < area.width; ++column) {
                    const auto pixel = reinterpret_cast<const uint32_t*>(pixels + row * stride)[column];
                    image_.SetRGB(column, row, (pixel >> 16U) & 0xffU, (pixel >> 8U) & 0xffU, pixel & 0xffU);
                }
            }
        }
        cairo_surface_unmap_image(surface, mapped);
    }
    wxStatusBar* status_;
    gulong handler_;
    wxImage image_;
};
void StatusBaselines(EditorFrame* frame) {
    auto* status = frame->GetStatusBar();
    NativePaint::Complete(frame->Editor());
    // The common final glyph isolates baseline displacement from differences in
    // CJK/Latin ink bounds. Keep the actual translated captions in every pane.
    const std::array fields{StatusField::Line, StatusField::Encoding, StatusField::Eol, StatusField::Overtype,
                            StatusField::Zoom};
    std::array<int, StatusField::Count> widths{};
    for (int field = 0; field < StatusField::Count; ++field) {
        if (std::ranges::find(fields, field) != fields.end())
            status->SetStatusText(status->GetStatusText(field) + " H", field);
        widths[field] = status->GetTextExtent(status->GetStatusText(field)).x + status->FromDIP(20);
    }
    widths[StatusField::Feedback] = -1;
    status->SetStatusWidths(StatusField::Count, widths.data());
    StatusCapture capture(status);
    const auto image = capture.Read();
    wxString evidence;
    if (wxGetEnv("WXNOTEPAD_UI_LAYOUT_IMAGE", &evidence))
        Require(image.SaveFile(evidence, wxBITMAP_TYPE_PNG), "retain actual status rendering");
    int expectedBottom = -1;
    const bool darkText = status->GetForegroundColour().GetLuminance() < status->GetBackgroundColour().GetLuminance();
    const double inkThreshold =
        (status->GetForegroundColour().GetLuminance() + status->GetBackgroundColour().GetLuminance()) / 2;
    for (int field : fields) {
        wxRect rectangle;
        Require(status->GetFieldRect(field, rectangle), "status field has a native rectangle");
        const auto text = status->GetStatusText(field);
        Require(text.EndsWith(" H"), "baseline probe preserves each caption through the actual paint");
        // wxGTK status text starts one pixel after the generic two-pixel inset.
        constexpr int GtkTextInset = 3;
        const int left = static_cast<int>(
            std::lround((rectangle.x + GtkTextInset + status->GetTextExtent(text.BeforeLast('H')).x) * capture.scaleX));
        const int right = static_cast<int>(
            std::lround((rectangle.x + GtkTextInset + status->GetTextExtent(text).x) * capture.scaleX));
        int bottom = -1;
        for (int y = static_cast<int>(std::ceil(rectangle.y * capture.scaleY));
             y < static_cast<int>(std::floor((rectangle.GetBottom() + 1) * capture.scaleY)); ++y) {
            for (int x = left; x < right; ++x) {
                Require(x >= 0 && x < image.GetWidth() && y >= 0 && y < image.GetHeight(),
                        "baseline glyph lies within the actual status pixels");
                const wxColour pixel(image.GetRed(x, y), image.GetGreen(x, y), image.GetBlue(x, y));
                if (darkText ? pixel.GetLuminance() < inkThreshold : pixel.GetLuminance() > inkThreshold) bottom = y;
            }
        }
        std::printf("Status baseline: field=%d glyph-bottom=%d text=%s\n", field, bottom, text.utf8_str().data());
        Require(bottom >= 0, "common baseline glyph is present in actual status pixels");
        if (expectedBottom < 0) expectedBottom = bottom;
        Require(bottom == expectedBottom, "translated and Latin status captions must share one painted baseline");
    }
}
#endif
class LayoutApp final : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        const auto language = argc > 2 ? wxAtoi(argv[2]) : 1033;
        AppLocalization::Initialize(static_cast<uint16_t>(language));
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.selectOption = 11;
        auto* frame = new EditorFrame(settings_.get());
        frame->SetClientSize(wxSize(1500, 650));
        frame->Show();
        CallAfter([this, frame, language] {
            try {
                Require(
                    frame->GetStatusBar()->GetStatusText(StatusField::Line).StartsWith(language == 1033 ? "Ln" : "行"),
                    "load the actual requested English or Chinese UI catalog");
                if (argc > 1 && argv[1] == "status") {
#ifdef __WXGTK__
                    StatusBaselines(frame);
#endif
                } else {
                    ReplacementLayout(frame);
                }
                std::puts("PASS: actual native UI layout");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Destroy();
            ExitMainLoop();
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
wxIMPLEMENT_APP(LayoutApp);
