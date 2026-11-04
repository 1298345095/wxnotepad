#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <cstdio>
#include <stdexcept>
#include <memory>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class SettingsApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                const wxString mode = argc > 1 ? wxString(argv[1]) : wxString("change");
                const bool restored = mode.StartsWith("restored");
                const bool disabled = mode == "disabled";
                frame->Editor()->Send(Scintilla::Message::SetText, 0, reinterpret_cast<intptr_t>("next stored"));
                frame->Editor()->Send(Scintilla::Message::EmptyUndoBuffer);
                frame->Editor()->Send(Scintilla::Message::SetSavePoint);
                frame->Editor()->Send(Scintilla::Message::SetSel, 0, 4);
                NativeSearch::Command(frame, wxID_REPLACE);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                auto* matchCase = wxDynamicCast(wxWindow::FindWindowByName("Match case", frame), wxCheckBox);
                auto* wholeWord = wxDynamicCast(wxWindow::FindWindowByName("Whole word", frame), wxCheckBox);
                auto* transform = wxDynamicCast(wxWindow::FindWindowByName("Transform backslashes", frame), wxCheckBox);
                auto* wrap = wxDynamicCast(wxWindow::FindWindowByName("Wrap around", frame), wxCheckBox);
                Require(query && query->GetValue() == (disabled   ? ""
                                                       : restored ? "next"
                                                                  : "stored"),
                        "original Recent Find fallback and SelectOption must be restored in the real dialog");
                Require(replacement->GetValue() == (disabled || mode == "restored-empty" ? ""
                                                    : restored                           ? "replacement"
                                                                                         : "previous"),
                        "original Recent Replace fallback must be restored in the real dialog");
                Require(matchCase->GetValue() == !disabled && transform->GetValue() == !disabled && !wrap->GetValue() &&
                            wholeWord->GetValue() == (!disabled && (!restored || mode == "restored-history")),
                        "packed original find flags and behavior options must be restored");
                if (!restored) {
                    query->SetValue(mode == "controls" ? "next\nstored" : "next");
                    replacement->SetValue(mode == "empty"      ? ""
                                          : mode == "controls" ? "replacement\nline"
                                                               : "replacement");
                    NativeSearch::Toggle(wholeWord, false);
                    auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
                    NativeSearch::Click(dialog, wxID_FORWARD);
                    query->SetValue("discarded draft");
                    NativeSearch::Toggle(matchCase, false);
                    NativeSearch::Click(dialog, wxID_CANCEL);
                    if (mode == "change") {
                        const int save = frame->GetMenuBar()->FindMenuItem("View", "Save Settings Now");
                        NativeSearch::Command(frame, save);
                    }
                }
                std::puts("PASS: original search settings through native controls and independent process");
                result_ = 0;
                frame->Close();
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                frame->Destroy();
            }
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SettingsApp);
