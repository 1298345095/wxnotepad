#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include <SciLexer.h>
#include <wx/vlbox.h>
#ifdef __WXMSW__
#include <imm.h>
#endif

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
#ifdef __WXMSW__
constexpr int PopupFontPoints = 12;
constexpr int PopupZoomPercent = 125;
constexpr int ZoomedPopupFontPoints = 15;
wxVListBox* CompletionList(wxWindow* parent) {
    for (auto* child : parent->GetChildren()) {
        if (auto* list = wxDynamicCast(child, wxVListBox)) return list;
        if (auto* list = CompletionList(child)) return list;
    }
    return nullptr;
}
void DocumentAndPopup(EditorFrame* frame, int gdi, int d2d) {
    auto* editor = frame->Editor();
    auto* bar = frame->GetMenuBar();
    NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "C/C++"));
    const std::string text =
        "int value = 42; // styled wrapped text 中文😀 " + std::string(180, 'x') + "\r\n// next line\r\n";
    TestEditor::ResetStream(editor, text.c_str(), 23, 5);
    editor->Send(Message::SetWrapMode, static_cast<uintptr_t>(Scintilla::Wrap::Word));
    editor->Send(Message::Colourise, 0, -1);
    editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("edited"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 23, 5);
    for (const auto& [id, actual] : {std::pair{gdi, 0}, std::pair{d2d, 1}, std::pair{gdi, 0}}) {
        NativeSearch::Command(frame, id);
        editor->Update();
        Require(editor->Send(Message::GetTechnology) == actual && TestEditor::Text(editor) == text &&
                    editor->Send(Message::GetAnchor) == 23 && editor->Send(Message::GetCurrentPos) == 5 &&
                    !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                    editor->Send(Message::CanRedo),
                "switching a populated document retains bytes, reversed selection, save point and history");
        Require(editor->Send(Message::GetLexer) == SCLEX_CPP &&
                    editor->Send(Message::GetStyleIndexAt, 0) == SCE_C_WORD2 && editor->Send(Message::WrapCount, 0) > 1,
                "switching retains current C++ language and styles while rebuilding wrapped layout");
        editor->Send(Message::SetTechnology, static_cast<uintptr_t>(Scintilla::Technology::DirectWriteDC));
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(editor->Send(Message::GetTechnology) == actual && bar->IsChecked(gdi) == (actual == 0) &&
                    bar->IsChecked(d2d) == (actual == 1),
                "unsupported requests retain truthful actual menu radio state");
    }
    editor->Send(Message::Redo);
    const std::string edited = text.substr(0, 5) + "edited" + text.substr(23);
    Require(TestEditor::Text(editor) == edited && editor->Send(Message::GetModify), "Redo survives renderer switches");
    NativeSearch::Command(frame, d2d);
    Require(TestEditor::Text(editor) == edited && editor->Send(Message::GetModify) && editor->Send(Message::CanUndo),
            "switching a modified document retains edits and its Undo history");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == text && !editor->Send(Message::GetModify), "Undo restores loaded save point");

    NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "Plain Text"));
    TestEditor::ResetStream(editor, "al", 2, 2);
    editor->Send(Message::StyleSetSizeFractional, 0, PopupFontPoints * Scintilla::FontSizeMultiplier);
    editor->Send(Message::StyleSetSizeFractional, static_cast<uintptr_t>(Scintilla::StylesCommon::Default),
                 PopupFontPoints * Scintilla::FontSizeMultiplier);
    editor->Send(Message::SetZoom, PopupZoomPercent);
    editor->Send(Message::AutoCSetSeparator, ' ');
    NativeFocus::Activate(static_cast<HWND>(frame->GetHandle()), static_cast<HWND>(editor->GetHandle()));
    wxYield();
    Require(editor->HasFocus(), "owned editor has native focus for IME font observation");
    for (const auto& [before, after] : {std::pair{d2d, gdi}, std::pair{gdi, d2d}}) {
        NativeSearch::Command(frame, before);
        editor->Send(Message::AutoCShow, 2, reinterpret_cast<intptr_t>("alpha alpine"));
        editor->Send(Message::AutoCSelect, 0, reinterpret_cast<intptr_t>("alpine"));
        Require(editor->Send(Message::AutoCActive), "completion is open before switching renderer");
        editor->Send(Message::SetTechnology, after == gdi ? 0 : 1);
        wxYield();
        editor->Update();
        Require(editor->Send(Message::AutoCActive), "renderer switch retains already-open completion");
        auto* list = CompletionList(editor);
        Require(list && list->GetItemCount() == 2 && list->GetSelection() == 1,
                "open completion retains candidate contents and selection");
        const auto font = list->GetFont();
        const auto row = list->GetItemRect(0).height;
        const auto context = ImmGetContext(static_cast<HWND>(editor->GetHandle()));
        LOGFONTW ime{};
        const bool read = context && ImmGetCompositionFontW(context, &ime);
        if (context) ImmReleaseContext(static_cast<HWND>(editor->GetHandle()), context);
        Require(read && ime.lfHeight == -MulDiv(ZoomedPopupFontPoints, editor->GetDPI().y, DisplayUnits::PointsPerInch),
                "native IME font retains 12-point style at 125 percent with active renderer and native DPI");
        editor->Send(Message::AutoCCancel);
        editor->Send(Message::AutoCShow, 2, reinterpret_cast<intptr_t>("alpha alpine"));
        list = CompletionList(editor);
        Require(list && list->GetFont().GetFractionalPointSize() == font.GetFractionalPointSize() &&
                    list->GetFont().GetFaceName() == font.GetFaceName() &&
                    list->GetFont().GetWeight() == font.GetWeight() && list->GetFont().GetStyle() == font.GetStyle() &&
                    list->GetItemRect(0).height == row,
                "already-open completion font and row metrics match a fresh popup on the active renderer");
        Require(list->GetFont().GetFractionalPointSize() == ZoomedPopupFontPoints,
                "completion uses the explicit 12-point style at 125 percent");
        editor->Send(Message::AutoCComplete);
        Require(TestEditor::Text(editor) == "alpha", "completion remains usable after renderer switching");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == "al" && !editor->Send(Message::GetModify),
                "completion Undo retains the save point after switching");
    }
}
#endif
class RenderingApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame(argc > 1 ? &settings_ : nullptr);
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "rendering settings window is presented");
                if (argc > 1) {
                    const int expected = wxAtoi(argv[1]);
                    Require(settings_.values.renderingTechnology == expected, "original technology setting loads");
#ifdef __WXMSW__
                    Require(frame->Editor()->Send(Message::GetTechnology) == (expected == 1 ? 1 : 0),
                            "application applies supported request or preserves usable GDI fallback");
#else
                    Require(frame->Editor()->Send(Message::GetTechnology) == 0, "Linux retains native renderer");
#endif
                    if (argc > 2 && argv[2] != "keep") {
#ifdef __WXMSW__
                        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", argv[2]));
#endif
                    }
                    if (argc > 3 && argv[3] == "explicit")
                        NativeSearch::Command(frame,
                                              frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
                    result_ = 0;
                    frame->Close();
                    return;
                }
#ifdef __WXMSW__
                auto* editor = frame->Editor();
                auto* bar = frame->GetMenuBar();
                Require(editor->Send(Message::GetTechnology) == 1, "Windows application defaults to DirectWrite");
                const int gdi = bar->FindMenuItem("Settings", "Legacy GDI");
                const int d2d = bar->FindMenuItem("Settings", "Direct2D");
                Require(gdi != wxNOT_FOUND && d2d != wxNOT_FOUND, "original rendering choices exist");
                for (const auto& [id, actual] : {std::pair{gdi, 0}, std::pair{d2d, 1}}) {
                    NativeSearch::Command(frame, id);
                    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                    Require(editor->Send(Message::GetTechnology) == actual, "menu switches actual renderer");
                    Require(bar->IsChecked(gdi) == (actual == 0) && bar->IsChecked(d2d) == (actual == 1),
                            "radio state reports actual renderer");
                }
                DocumentAndPopup(frame, gdi, d2d);
#else
                Require(frame->Editor()->Send(Message::GetTechnology) == 0, "Linux retains native rendering");
                Require(frame->GetMenuBar()->FindMenuItem("Settings", "Legacy GDI") == wxNOT_FOUND &&
                            frame->GetMenuBar()->FindMenuItem("Settings", "Direct2D") == wxNOT_FOUND,
                        "Linux does not expose Windows rendering choices");
#endif
                result_ = 0;
                std::puts("PASS: application renderer default and original menu choices");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
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

private:
    SettingsStore settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(RenderingApp);
