// Observe real IDWriteTextFormat objects; return them unchanged to actual rendering.
#include "original_directwrite.inc"
#include "editor/platform/directwrite_surface.h"
#include <wx/string.h>
namespace {
std::vector<std::string> locales;
}
void ClearNativeStyleLocales() { locales.clear(); }
std::vector<std::string> NativeStyleLocales() { return locales; }

#define AllocateDirectWriteFont AllocateUnobservedDirectWriteFont
#include "editor/platform/directwrite_surface.cpp"
#undef AllocateDirectWriteFont

namespace Scintilla::Internal {
std::shared_ptr<Font> AllocateDirectWriteFont(const FontParameters& parameters) {
    auto font = AllocateUnobservedDirectWriteFont(parameters);
    auto* format = static_cast<FontWin*>(font.get())->pTextFormat.Get();
    std::wstring locale(format->GetLocaleNameLength() + 1, L'\0');
    if (FAILED(format->GetLocaleName(locale.data(), static_cast<UINT32>(locale.size()))))
        throw std::runtime_error("read actual DirectWrite text format locale");
    locales.push_back(wxString(locale.c_str()).utf8_string());
    return font;
}
} // namespace Scintilla::Internal
