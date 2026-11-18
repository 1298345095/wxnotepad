#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "font_dialog_helpers.h"
#include <wx/filename.h>
#include <wx/vlbox.h>
#include <cmath>

namespace {
using Scintilla::Message;
constexpr auto DefaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::string FontFace(EditorControl* editor, uintptr_t style = DefaultStyle) {
    std::string face(static_cast<size_t>(editor->Send(Message::StyleGetFont, style)) + 1, '\0');
    editor->Send(Message::StyleGetFont, style, reinterpret_cast<intptr_t>(face.data()));
    face.pop_back();
    return face;
}
wxVListBox* CompletionList(wxWindow* parent) {
    for (auto* child : parent->GetChildren()) {
        if (auto* list = wxDynamicCast(child, wxVListBox)) return list;
        if (auto* list = CompletionList(child)) return list;
    }
    return nullptr;
}
class FontsApp final : public wxApp {
public:
    bool OnInit() override {
        if (argc > 1) {
            settings_ = std::make_unique<SettingsStore>();
        } else {
            path_ = wxFileName::CreateTempFileName("wxnotepad-default-fonts");
            {
                wxFFile file(path_, "wb");
                file.Write("[Global Styles]\nDefault Code Style=font:$(Code); size:14; weight:600; italic\n"
                           "Default Text Style=font:$(Text); size:18; underline\n"
                           "[C/C++ Source]\nDefault=font:$(Text); size:+2; strike\n");
            }
            settings_ = std::make_unique<SettingsStore>(path_);
        }
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "font settings window is presented");
                if (argc > 1) {
                    Persistence(frame);
                    result_ = 0;
                    frame->Close();
                    return;
                }
                auto* bar = frame->GetMenuBar();
#ifdef __WXMSW__
                wxString legacy;
                const bool gdi = wxGetEnv("WXNOTEPAD_DEFAULT_FONTS_GDI", &legacy);
                if (gdi) NativeSearch::Command(frame, bar->FindMenuItem("Settings", "Legacy GDI"));
                Require(frame->Editor()->Send(Message::GetTechnology) ==
                            static_cast<intptr_t>(gdi ? Scintilla::Technology::Default
                                                      : Scintilla::Technology::DirectWrite),
                        "font chooser and layout checks use the requested real native renderer");
#endif
                const int code = bar->FindMenuItem("View", "Default Code Font...");
                const int text = bar->FindMenuItem("View", "Default Text Font...");
                const int inheritance = bar->FindMenuItem("Scheme", "Use Default Code Style");
                Require(code != wxNOT_FOUND && text != wxNOT_FOUND && inheritance != wxNOT_FOUND,
                        "original default code/text font and inheritance menu commands exist");
                auto* editor = frame->Editor();
                Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1800 &&
                            editor->Send(Message::StyleGetUnderline, DefaultStyle),
                        "plain text imports the separate text default");
                const auto textFace = FontFace(editor);
                NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "C/C++"));
                std::printf("C++ face=%s textFace=%s size=%lld weight=%lld italic=%lld strike=%lld\n",
                            FontFace(editor).c_str(), textFace.c_str(),
                            static_cast<long long>(editor->Send(Message::StyleGetSizeFractional, DefaultStyle)),
                            static_cast<long long>(editor->Send(Message::StyleGetWeight, DefaultStyle)),
                            static_cast<long long>(editor->Send(Message::StyleGetItalic, DefaultStyle)),
                            static_cast<long long>(editor->Send(Message::StyleGetStrike, DefaultStyle)));
                Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1600 &&
                            editor->Send(Message::StyleGetWeight, DefaultStyle) == 600 &&
                            editor->Send(Message::StyleGetItalic, DefaultStyle) &&
                            editor->Send(Message::StyleGetStrike, DefaultStyle) && FontFace(editor) == textFace,
                        "scheme default overlays code inheritance, named text font and relative point size");
                Require(bar->IsChecked(inheritance), "code scheme uses code default initially");
                NativeSearch::Command(frame, inheritance);
                Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 2000 &&
                            editor->Send(Message::StyleGetUnderline, DefaultStyle) && !bar->IsChecked(inheritance),
                        "inheritance selector immediately uses text base plus original scheme overlay");
                NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "Python"));
                Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1400,
                        "other code schemes keep their independent code inheritance");
                NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "C/C++"));
                Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 2000,
                        "returning to scheme restores its inheritance override");
                NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "Python"));
                TestEditor::ResetStream(editor, "alpha beta gamma\r\nsecond line\r\n", 12, 3);
                editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("edited"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 12, 3);
                editor->Send(Message::SetZoom, 125);
                const auto originalText = TestEditor::Text(editor);
                const auto originalEncoding = frame->DocumentEncoding();
                const auto codeFace = FontFace(editor);
                const auto originalStyle = settings_->values.fonts.codeStyle;
                {
                    NativeFonts::Chooser chooser(frame, 14, wxString::FromUTF8(textFace), true);
                    chooser.OpenWithShortcut();
                    chooser.Verify();
                }
                Require(FontFace(editor) == codeFace &&
                            editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1400 &&
                            settings_->values.fonts.codeStyle == originalStyle,
                        "Alt+F12 opens code chooser and cancelling retains exact preference and rendering");
                {
                    NativeFonts::Chooser chooser(frame, 14, wxString::FromUTF8(textFace), false);
                    chooser.OpenWithMenu(code);
                    chooser.Verify();
                }
                Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1500 &&
                            editor->Send(Message::StyleGetWeight, DefaultStyle) == 700 &&
                            editor->Send(Message::StyleGetItalic, DefaultStyle),
                        "native font confirmation applies point size, numeric weight and italic");
#ifdef __WXMSW__
                Require(editor->Send(Message::StyleGetUnderline, DefaultStyle) &&
                            editor->Send(Message::StyleGetStrike, DefaultStyle),
                        "native Windows font effects apply underline and strike independently of weight/italic");
#endif
                Require(TestEditor::Text(editor) == originalText && frame->DocumentEncoding() == originalEncoding &&
                            editor->Send(Message::GetAnchor) == 12 && editor->Send(Message::GetCurrentPos) == 3 &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo) && editor->Send(Message::GetZoom) == 125 &&
                            editor->Send(Message::TextHeight, 0) > 0,
                        "font confirmation preserves bytes, encoding, reversed selection, save point, Redo and zoom");
                NativeKeys::Shortcut(frame, WXK_F12, false, false, true);
                Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1800 &&
                            !bar->IsChecked(inheritance),
                        "Shift+F12 invokes selected scheme inheritance without collapsing text/code defaults");
                NativeKeys::Shortcut(frame, WXK_F12, false, false, true);
                Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1500,
                        "Shift+F12 restores separately chosen code default");
                editor->Send(Message::Redo);
                Require(TestEditor::Text(editor) == "alpeditedamma\r\nsecond line\r\n" &&
                            editor->Send(Message::GetModify),
                        "Redo remains usable after font and inheritance changes");
                editor->Send(Message::Undo);
                const auto chosenCodeFace = FontFace(editor);
                {
                    NativeFonts::Chooser chooser(frame, 18, wxString::FromUTF8(codeFace), false);
                    chooser.OpenWithMenu(text);
                }
                Require(FontFace(editor) == chosenCodeFace &&
                            editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1500,
                        "editing inactive text default retains the active code font");
                NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "Plain Text"));
                Require(FontFace(editor) == codeFace &&
                            editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1500 &&
                            editor->Send(Message::StyleGetUnderline, DefaultStyle),
                        "native text font confirmation independently updates text family/size and preserves effects");
                const auto foldStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::FoldDisplayText);
                Require(FontFace(editor, foldStyle) == chosenCodeFace,
                        "text scheme fold ellipsis always uses the separately chosen code family");
                NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "Python"));
                TestEditor::ResetStream(editor, "al", 2, 2);
                editor->Send(Message::AutoCSetSeparator, ' ');
                editor->Send(Message::AutoCShow, 2, reinterpret_cast<intptr_t>("alpha alpine"));
                wxYield();
                auto* list = CompletionList(editor);
                Require(
                    list && list->GetItemCount() == 2 &&
                        std::abs(list->GetFont().GetFractionalPointSize() - 18.75) <=
                            static_cast<double>(DisplayUnits::PointsPerInch) / editor->GetDPI().y &&
                        list->GetItemRect(0).height > 0,
                    "completion rebuilds its font and row metrics from the chosen 15pt default at 125 percent zoom");
                editor->Send(Message::AutoCCancel);
#ifdef __WXMSW__
                NativeFocus::Activate(static_cast<HWND>(frame->GetHandle()), static_cast<HWND>(editor->GetHandle()));
                wxYield();
                editor->Update();
                LOGFONTW ime{};
                const auto context = ImmGetContext(static_cast<HWND>(editor->GetHandle()));
                const bool read = context && ImmGetCompositionFontW(context, &ime);
                if (context) ImmReleaseContext(static_cast<HWND>(editor->GetHandle()), context);
                Require(read &&
                            ime.lfHeight == -MulDiv(1875, editor->GetDPI().y,
                                                    DisplayUnits::PointsPerInch * Scintilla::FontSizeMultiplier) &&
                            wxString(ime.lfFaceName).CmpNoCase(wxString::FromUTF8(chosenCodeFace)) == 0,
                        "native IME font follows chosen code face, 125 percent zoom and current DPI");
#endif
                result_ = 0;
                std::puts("PASS: original default font import and scheme inheritance");
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
        if (!path_.empty()) wxRemoveFile(path_);
        return result_;
    }

private:
    void Persistence(EditorFrame* frame) {
        auto* editor = frame->Editor();
        auto* bar = frame->GetMenuBar();
        const auto codeSize = wxAtoi(argv[2]);
        const auto cppSize = wxAtoi(argv[3]);
        NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "Python"));
        Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == codeSize * 100 &&
                    editor->Send(Message::StyleGetWeight, DefaultStyle) == (codeSize == 14 ? 600 : 700),
                "independent process restores original code point size and weight");
        const auto family = FontFace(editor);
        NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "C/C++"));
        Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == cppSize * 100 &&
                    bar->IsChecked(bar->FindMenuItem("Scheme", "Use Default Code Style")) == (cppSize == 17),
                "independent process restores per-scheme code/text inheritance and relative default style");
        NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "Plain Text"));
        Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1800,
                "independent process keeps the separate text font");
        if (argv[1] == "change" || argv[1] == "explicit" || argv[1] == "cancel") {
            NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "Python"));
            NativeFonts::Chooser chooser(frame, 14, wxString::FromUTF8(family), argv[1] == "cancel",
                                         wxColour("#123456"));
            chooser.OpenWithMenu(bar->FindMenuItem("View", "Default Code Font..."));
            chooser.Verify();
            if (argv[1] != "cancel") {
                Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1500,
                        "persisted change comes from confirmed native chooser");
                NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "C/C++"));
                NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "Use Default Code Style"));
                Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == 1700,
                        "changed scheme now uses chosen code default plus original relative size");
            }
            if (argv[1] == "explicit") NativeSearch::Command(frame, bar->FindMenuItem("Settings", "Save Settings Now"));
        }
        if (argc > 4) {
            Require(frame->Open(std::optional<wxString>(argv[4])).status == OpenStatus::Opened,
                    "open Python file through real application entry point");
            Require(editor->Send(Message::StyleGetSizeFractional, DefaultStyle) == codeSize * 100,
                    "opening a file uses restored code default independently of prior text scheme");
        }
    }
    wxString path_;
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(FontsApp);
