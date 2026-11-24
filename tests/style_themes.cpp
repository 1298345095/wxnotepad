#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "settings/settings_store.h"
#include "syntax/syntax_scheme.h"
#include <wx/ffile.h>
#include <wx/filename.h>
#include <SciLexer.h>

namespace {
using Scintilla::Message;
constexpr auto DefaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Pixels(EditorFrame* frame, const char* name, unsigned background) {
    auto* editor = frame->Editor();
    NativePaint::Complete(editor);
    const auto size = editor->GetClientSize();
    const auto pixel = NativePaint::Read(editor, wxRect(size.x - 20, std::min(100, size.y - 20), 1, 1)).front();
    Require(pixel.GetRGB() == background, "actual native editor background matches the selected original profile");
    wxString directory;
    if (!wxGetEnv("WXNOTEPAD_THEME_EVIDENCE", &directory)) return;
    const wxRect area(0, 0, std::min(size.x, 500), std::min(size.y, 160));
    const auto pixels = NativePaint::Read(editor, area);
    wxImage image(area.width, area.height);
    for (size_t index = 0; index < pixels.size(); ++index) {
        image.GetData()[index * 3] = pixels[index].Red();
        image.GetData()[index * 3 + 1] = pixels[index].Green();
        image.GetData()[index * 3 + 2] = pixels[index].Blue();
    }
    Require(image.SaveFile(wxFileName(directory, wxString::FromUTF8(name) + ".png").GetFullPath(), wxBITMAP_TYPE_PNG),
            "retain selected profile's actual native raster");
}
class ThemesApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppearance(Appearance::System);
        wxInitAllImageHandlers();
        directory_ = wxFileName::CreateTempFileName("wxnotepad-style-themes");
        wxRemoveFile(directory_);
        wxFileName::Mkdir(directory_);
        path_ = wxFileName(directory_, "Notepad4.ini").GetFullPath();
        if (argc > 1) {
            SetExitOnFrameDelete(false);
            CallAfter([this] {
                try {
                    ProfileSettings();
                    result_ = 0;
                } catch (const std::exception& error) {
                    std::println(stderr, "style theme settings: {}", error.what());
                }
                ExitMainLoop();
            });
            return true;
        }
        settings_ = std::make_unique<SettingsStore>(path_);
        auto* frame = new EditorFrame(settings_.get());
        frame->SetWindowStyleFlag(frame->GetWindowStyleFlag() | wxSTAY_ON_TOP);
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "theme window is presented");
                auto* bar = frame->GetMenuBar();
                const int dark = bar->FindMenuItem("Scheme", "Dark");
                const int light = bar->FindMenuItem("Scheme", "Default");
                Require(dark != wxNOT_FOUND && light != wxNOT_FOUND,
                        "original Scheme Style Theme Default/Dark commands exist");
                auto* editor = frame->Editor();
                TestEditor::ResetStream(editor, "alpha beta\r\n", 8, 2);
                editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("edited"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 8, 2);
                const auto bytes = TestEditor::Text(editor);
                const auto encoding = frame->DocumentEncoding();
#ifdef __WXMSW__
                wxString gdi;
                if (wxGetEnv("WXNOTEPAD_THEME_GDI", &gdi))
                    NativeSearch::Command(frame, bar->FindMenuItem("Settings", "Legacy GDI"));
#endif
                NativeSearch::Command(frame, dark);
                std::println("Dark: fore={:x} back={:x} checked={} theme={} code={} text={}",
                             editor->Send(Message::StyleGetFore, DefaultStyle),
                             editor->Send(Message::StyleGetBack, DefaultStyle), bar->IsChecked(dark),
                             std::to_underlying(settings_->values.fonts.theme),
                             settings_->values.fonts.codeStyle.ToStdString(),
                             settings_->values.fonts.textStyle.ToStdString());
                // Independent pinned doc/Notepad4 DarkTheme.ini global records.
                Require(editor->Send(Message::StyleGetBack, DefaultStyle) == 0x1e1e1e &&
                            editor->Send(Message::StyleGetFore, DefaultStyle) == 0xd4d4d4 && bar->IsChecked(dark),
                        "Dark applies the original global palette and checked identity");
                Pixels(frame, "dark", 0x1e1e1e);
                NativeSearch::Command(frame, dark);
                Require(editor->Send(Message::StyleGetBack, DefaultStyle) == 0x1e1e1e &&
                            !wxFileExists(settings_->DarkProfilePath()),
                        "selecting Dark again leaves the active style and missing user profile unchanged");
                NativeSearch::Command(frame, light);
#ifdef __WXMSW__
                const auto defaultBackground = ::GetSysColor(COLOR_WINDOW);
#else
                constexpr unsigned defaultBackground = 0xffffff;
#endif
                std::println("Default: fore={:x} back={:x} nativeFore={:x} nativeBack={:x}",
                             editor->Send(Message::StyleGetFore, DefaultStyle),
                             editor->Send(Message::StyleGetBack, DefaultStyle),
                             wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT).GetRGB(),
                             wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW).GetRGB());
                Require(editor->Send(Message::StyleGetBack, DefaultStyle) == defaultBackground && bar->IsChecked(light),
                        "Default restores its profile independently of system theme");
                Pixels(frame, "default", defaultBackground);
                Require(TestEditor::Text(editor) == bytes && frame->DocumentEncoding() == encoding &&
                            editor->Send(Message::GetAnchor) == 8 && editor->Send(Message::GetCurrentPos) == 2 &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo),
                        "switching preserves document bytes, encoding, selection, save point and Redo");
                NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "C/C++"));
                TestEditor::ResetStream(editor, "int main() {\r\n  // comment\r\n  return 0;\r\n}\r\n");
                editor->Send(Message::Colourise, 0, -1);
                editor->Send(Message::FoldLine, 0, std::to_underlying(Scintilla::FoldAction::Contract));
                Require(!editor->Send(Message::GetFoldExpanded, 0), "prepare a real collapsed C/C++ block");
                const auto foldedBytes = TestEditor::Text(editor);
                NativeSearch::Command(frame, dark);
                Require(editor->Send(Message::StyleGetFore, SCE_C_COMMENT) == 0x608060 &&
                            editor->Send(Message::StyleGetFore, SCE_C_COMMENTLINE) == 0x608060 &&
                            editor->Send(Message::StyleGetBack, SCE_C_COMMENTLINE) == 0x1e1e1e,
                        "original grouped comment styles inherit the Dark document background");
                Require(!editor->Send(Message::GetFoldExpanded, 0) && TestEditor::Text(editor) == foldedBytes,
                        "style-only theme reapplication preserves the collapsed block and its document bytes");
                NativeSearch::Command(frame, bar->FindMenuItem("Scheme", "Python"));
                Require(editor->Send(Message::StyleGetBack, SCE_PY_COMMENTLINE) == 0x1e1e1e &&
                            editor->Send(Message::StyleGetFore, SCE_PY_COMMENTLINE) == 0x608060,
                        "subsequent scheme changes retain Dark while using that scheme's original token defaults");
                auto customized = editor->Fonts();
                customized.codeStyle = "font:$(Code); fore:#123456; back:#456789";
                customized.Scheme(SyntaxLanguage::Python).useCode = true;
                editor->SetFontSettings(customized);
                ApplySyntaxStyles(*editor, SyntaxLanguage::Python);
                Require(editor->Send(Message::StyleGetFore, DefaultStyle) == 0x563412 &&
                            editor->Send(Message::StyleGetBack, DefaultStyle) == 0x896745 &&
                            editor->Send(Message::StyleGetBack, SCE_PY_COMMENTLINE) == 0x896745,
                        "explicit custom colours override native fallbacks and remain inherited by token styles");
                Pixels(frame, "custom", 0x896745);
                result_ = 0;
            } catch (const std::exception& error) {
                std::println(stderr, "style themes: {}", error.what());
            }
            frame->SetWindowStyleFlag(frame->GetWindowStyleFlag() & ~wxSTAY_ON_TOP);
            frame->Close();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override {
        settings_.reset();
        wxFileName::Rmdir(directory_, wxPATH_RMDIR_RECURSIVE);
        return wxApp::OnExit();
    }

private:
    void ProfileSettings() {
        {
            wxFFile file(path_, "wb");
            Require(file.IsOpened() &&
                        file.Write("[Settings]\nSaveSettings=0\nSaveBeforeRunningTools=0\n[Global Styles]\n"
                                   "Default Code Style=font:$(Code); size:13.5; weight:600\n"
                                   "Default Text Style=font:$(Text); size:16\n"
                                   "[C/C++ Source]\nDefault=size:+1.5\nComment=italic; fore:#123456\n"
                                   "[Unavailable Language]\nUnknownToken=fore:#112233\n"),
                    "write an existing customized profile");
        }
        SettingsStore settings(path_);
        Require(settings.values.fonts.Scheme(SyntaxLanguage::Cpp).useCode &&
                    settings.values.fonts.Scheme(SyntaxLanguage::Cpp).defaultStyle == "size:+1.5",
                "existing customized Default profile imports original inheritance and relative-size text");
        Require(settings.SwitchStyleTheme(StyleTheme::Dark) &&
                    settings.values.fonts.codeStyle == "font:$(Code); fore:#D4D4D4; back:#1E1E1E" &&
                    !wxFileExists(settings.DarkProfilePath()),
                "missing user Dark file uses distributed original defaults without writing the installation");
        settings.values.fonts.Scheme(SyntaxLanguage::Cpp).useCode = false;
        settings.values.saveBeforeRunningTools = true;
        Require(settings.SwitchStyleTheme(StyleTheme::Default), "dirty Dark profile switches successfully");
        // Independent original-02 capture: omitted UseDefaultCodeStyle keeps
        // the current lexer choice; it does not reset to the compiled default.
        Require(!settings.values.fonts.Scheme(SyntaxLanguage::Cpp).useCode,
                "Default with an omitted inheritance key retains the original current-lexer choice");
        Require(settings.values.fonts.codeStyle.Contains("size:13.5") &&
                    settings.values.fonts.Scheme(SyntaxLanguage::Cpp).defaultStyle == "size:+1.5",
                "default global/per-scheme styles remain separate from dirty Dark styles");
        SettingsStore restarted(path_);
        Require(restarted.values.fonts.theme == StyleTheme::Dark &&
                    !restarted.values.fonts.Scheme(SyntaxLanguage::Cpp).useCode,
                "save-before-switch persists the old theme identity and dirty Dark inheritance with SaveSettings off");
        Require(!restarted.values.saveBeforeRunningTools,
                "save-before-theme-switch writes style settings without saving unrelated dirty tool preferences");
        wxFFile mainFile(path_, "rb");
        wxString mainText;
        Require(mainFile.ReadAll(&mainText, wxConvAuto()) && mainText.Contains("UnknownToken=fore:#112233"),
                "style save retains unavailable-language settings");
    }
    wxString path_;
    wxString directory_;
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(ThemesApp);
