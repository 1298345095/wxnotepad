#pragma once

#include "editor/editor_control.h"
#include <wx/app.h>
#include <wx/dcclient.h>
#include <wx/utils.h>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <vector>
#ifdef __WXGTK__
#include <wx/graphics.h>
#include <cairo.h>
#endif

// Numeric samples from the actual widget paint surface; no image capture.
namespace NativePaint {
inline void Complete(EditorControl* editor) {
    class Completion final : public wxEvtHandler {
    public:
        explicit Completion(EditorControl* editor) : editor_(editor) { editor_->PushEventHandler(this); }
        ~Completion() override { editor_->PopEventHandler(false); }
        bool ProcessEvent(wxEvent& event) override {
            const bool handled = wxEvtHandler::ProcessEvent(event);
            if (event.GetEventType() == wxEVT_PAINT) ++completed;
            return handled;
        }
        unsigned completed = 0;

    private:
        EditorControl* editor_;
    } completion(editor);
    // Styling can abandon the first layout. Observe a second real paint of
    // the settled layout; Update() alone is a no-op on wxGTK/Wayland.
    for (int pass = 0; pass < 2; ++pass) {
        const auto expected = completion.completed + 1;
        editor->Refresh(false);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (completion.completed < expected && std::chrono::steady_clock::now() < deadline) {
            editor->Update();
            wxYield();
            if (completion.completed < expected) wxMilliSleep(1);
        }
        if (completion.completed < expected) throw std::runtime_error("actual native editor paint must complete");
    }
}
inline std::vector<wxColour> Read(EditorControl* editor, const wxRect& area) {
    std::vector<wxColour> pixels;
    const auto sample = [&](wxDC& dc) {
        for (int y = area.y; y < area.GetBottom() + 1; ++y) {
            for (int x = area.x; x < area.GetRight() + 1; ++x) {
                int nativeX = x;
                int nativeY = y;
#ifdef __WXGTK__
                auto* context = static_cast<cairo_t*>(dc.GetGraphicsContext()->GetNativeContext());
                double deviceX = x;
                double deviceY = y;
                cairo_user_to_device(context, &deviceX, &deviceY);
                nativeX = static_cast<int>(deviceX);
                nativeY = static_cast<int>(deviceY);
#endif
                wxColour colour;
                if (!dc.GetPixel(nativeX, nativeY, &colour)) throw std::runtime_error("read actual widget paint pixel");
                pixels.push_back(colour);
            }
        }
    };
#ifdef __WXGTK__
    class Observation final : public wxEvtHandler {
    public:
        Observation(EditorControl* editor, const std::function<void(wxDC&)>& sample)
            : editor_(editor), sample_(sample) {
            editor_->PushEventHandler(this);
        }
        ~Observation() override { editor_->PopEventHandler(false); }
        bool ProcessEvent(wxEvent& event) override {
            const bool handled = wxEvtHandler::ProcessEvent(event);
            if (event.GetEventType() == wxEVT_PAINT && pending) {
                wxPaintDC dc(editor_);
                sample_(dc);
                pending = false;
            }
            return handled;
        }
        bool pending = true;

    private:
        EditorControl* editor_;
        std::function<void(wxDC&)> sample_;
    } observation(editor, sample);
    editor->Refresh(false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (observation.pending && std::chrono::steady_clock::now() < deadline) {
        editor->Update();
        wxYield();
        if (observation.pending) wxMilliSleep(10);
    }
    if (observation.pending) throw std::runtime_error("actual native GTK widget paint completed");
#else
    editor->Refresh(false);
    editor->Update();
    wxYield();
    wxClientDC dc(editor);
    sample(dc);
#endif
    return pixels;
}

class PixelReader {
public:
    explicit PixelReader(EditorControl* editor) : editor_(editor) {}
    wxColour operator()(int x, int y) { return Read(editor_, wxRect(x, y, 1, 1)).front(); }

private:
    EditorControl* editor_;
};
} // namespace NativePaint
