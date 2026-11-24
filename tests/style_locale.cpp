#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "syntax/syntax_scheme.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

// Test-only observation of actual native font/layout objects used by the adapter.
void ClearNativeStyleLocales();
std::vector<std::string> NativeStyleLocales();
#ifdef __WXMSW__
void SetNativeStyleLocaleDefaults(const wchar_t* user, const wchar_t* system);
#endif

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
class StyleLocaleApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->SetWindowStyleFlag(frame->GetWindowStyleFlag() | wxSTAY_ON_TOP);
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "locale editor is presented");
                auto* editor = frame->Editor();
                TestEditor::ResetStream(editor, "A\u9aa8\u76f4\u4ee4Z\r\n");
                auto fonts = editor->Fonts();
                fonts.codeStyle = "font:$(Code); locale:ja-JP";
                fonts.textStyle = "font:$(Text); locale:zh-TW";
                fonts.Scheme(SyntaxLanguage::Cpp).defaultStyle = "locale:fr-FR";
                fonts.Scheme(SyntaxLanguage::Cpp).useCode = true;
                const auto check = [&](const char* expected) {
                    ClearNativeStyleLocales();
                    editor->SetFontSettings(fonts);
                    ApplySyntaxStyles(*editor, SyntaxLanguage::Cpp);
                    NativePaint::Complete(editor);
                    const auto observed = NativeStyleLocales();
                    std::println(stderr, "native style locale: {} ({} realizations)",
                                 observed.empty() ? "<none>" : observed.front(), observed.size());
                    Require(std::ranges::find(observed, expected) != observed.end(),
                            "selected global locale reaches the actual native font/layout used for rendering");
                };
                check("ja-jp");
                fonts.Scheme(SyntaxLanguage::Cpp).useCode = false;
                check("zh-tw");
#ifdef __WXMSW__
                // Literal outcomes of Styles.cpp::Style_SetFontLocaleName:
                // a non-CJK user locale yields to a CJK system locale.
                SetNativeStyleLocaleDefaults(L"en-US", L"ja-JP");
                fonts.textStyle = "locale:invalid-locale-name";
                check("ja-jp");
                fonts.textStyle.clear();
                SetNativeStyleLocaleDefaults(L"zh-SG", L"ja-JP");
                check("zh-sg");
                SetNativeStyleLocaleDefaults(L"de-DE", L"fr-FR");
                check("de-de");
#elif defined(__WXGTK__)
                fonts.textStyle.clear();
                editor->SetFontSettings(fonts);
                ApplySyntaxStyles(*editor, SyntaxLanguage::Cpp);
                NativePaint::Complete(editor);
                const wxRect textArea(0, 0, 300, 40);
                const auto systemRaster = NativePaint::Read(editor, textArea);
                fonts.textStyle = "locale:invalid-locale-name";
                editor->SetFontSettings(fonts);
                ApplySyntaxStyles(*editor, SyntaxLanguage::Cpp);
                NativePaint::Complete(editor);
                std::array<char, 100> actual{};
                editor->Send(Scintilla::Message::GetFontLocale, 0, reinterpret_cast<intptr_t>(actual.data()));
                Require(actual.front() == '\0' && NativePaint::Read(editor, textArea) == systemRaster,
                        "invalid original locale falls back to native system language and its actual glyph raster");
#endif
                result_ = 0;
            } catch (const std::exception& error) {
                std::println(stderr, "style locale: {}", error.what());
            }
            frame->Close();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(StyleLocaleApp);
