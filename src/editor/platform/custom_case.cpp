#include "editor/platform/custom_case.h"
#include "editor/platform/windows_case_data.h"
#include <wx/string.h>
#include <algorithm>
#ifdef __WXMSW__
#include <windows.h>
#endif

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace {
template <size_t N> int Property(const WindowsCaseData::Range (&ranges)[N], uint32_t ch) {
    const auto* found = std::upper_bound(std::begin(ranges), std::end(ranges), ch,
                                         [](uint32_t value, const auto& range) { return value < range.first; });
    if (found == std::begin(ranges)) return 0;
    --found;
    return ch <= found->last ? found->value : 0;
}

wxString LinguisticMap(const wxString& text, bool title) {
#ifdef __WXMSW__
    const auto input = text.ToStdWstring();
    const DWORD flags = LCMAP_LINGUISTIC_CASING | (title ? LCMAP_TITLECASE : LCMAP_LOWERCASE);
    const int count = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, input.data(), static_cast<int>(input.size()),
                                    nullptr, 0, nullptr, nullptr, 0);
    if (!count) return text;
    std::wstring output(count, L'\0');
    if (!LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, input.data(), static_cast<int>(input.size()), output.data(),
                       count, nullptr, nullptr, 0))
        return text;
    return wxString(output);
#else
    wxString output;
    bool newWord = true;
    for (const auto character : text) {
        const uint32_t ch = character.GetValue();
        const auto mapped =
            ch + (title ? (newWord ? Property(WindowsCaseData::Title, ch) : 0) : Property(WindowsCaseData::Lower, ch));
        output += wxUniChar(mapped);
        if (title) {
            const int state = Property(WindowsCaseData::TitleState, ch);
            newWord = (state & (newWord ? 1 : 2)) != 0;
        }
    }
    return output;
#endif
}

uint32_t Invert(uint32_t ch) {
    // The original loops over WCHARs: surrogate pairs keep their case.
    if (ch > 0xffff) return ch;
#ifdef __WXMSW__
    const auto character = static_cast<wchar_t>(ch);
    if (IsCharUpperW(character))
        return LOWORD(reinterpret_cast<uintptr_t>(CharLowerW(reinterpret_cast<LPWSTR>(static_cast<uintptr_t>(ch)))));
    if (IsCharLowerW(character))
        return LOWORD(reinterpret_cast<uintptr_t>(CharUpperW(reinterpret_cast<LPWSTR>(static_cast<uintptr_t>(ch)))));
    return ch;
#else
    return ch + Property(WindowsCaseData::Invert, ch);
#endif
}

bool AlphaNumeric(uint32_t ch) {
#ifdef __WXMSW__
    return ch <= 0xffff && IsCharAlphaNumericW(static_cast<wchar_t>(ch));
#else
    return (Property(WindowsCaseData::Sentence, ch) & 1) != 0;
#endif
}
bool Lower(uint32_t ch) {
#ifdef __WXMSW__
    return ch <= 0xffff && IsCharLowerW(static_cast<wchar_t>(ch));
#else
    return (Property(WindowsCaseData::Sentence, ch) & 2) != 0;
#endif
}
bool SentenceTerminator(uint32_t ch) {
    if (ch > 0xff00 && ch < 0xff5f) ch -= 0xfee0;
    return ch == '\r' || ch == '\n' || ch == '.' || ch == ';' || ch == '!' || ch == '?' || ch == 0x85 || ch == 0x2028 ||
           ch == 0x2029;
}
} // namespace

std::string MapCustomCase(std::string_view text, int command) {
    if (text.empty()) return {};
    auto input = wxString::FromUTF8(text.data(), text.size());
    if (command == 40210) return LinguisticMap(LinguisticMap(input, false), true).ToStdString(wxConvUTF8);
    if (command == 40209) input = LinguisticMap(input, false);
    if (command != 40208 && command != 40209) return std::string(text);
    wxString output;
    bool newSentence = true;
    for (const auto character : input) {
        const uint32_t ch = character.GetValue();
        uint32_t mapped = ch;
        if (command == 40208) {
            mapped = Invert(ch);
        } else if (SentenceTerminator(ch)) {
            newSentence = true;
        } else if (newSentence && AlphaNumeric(ch)) {
            if (Lower(ch)) mapped = Invert(ch);
            newSentence = false;
        }
        output += wxUniChar(mapped);
    }
    return output.ToStdString(wxConvUTF8);
}

std::wstring OriginalNlsUppercase(std::wstring_view text) {
    if (text.empty()) return {};
#ifdef __WXMSW__
    const int count = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, text.data(),
                                    static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr, 0);
    if (!count) return std::wstring(text);
    std::wstring output(count, L'\0');
    if (!LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, text.data(), static_cast<int>(text.size()),
                       output.data(), count, nullptr, nullptr, 0))
        return std::wstring(text);
    return output;
#else
    std::wstring output;
    for (size_t index = 0; index < text.size(); ++index) {
        uint32_t ch = static_cast<uint16_t>(text[index]);
        if (ch >= 0xd800 && ch <= 0xdbff && index + 1 < text.size()) {
            const auto trail = static_cast<uint16_t>(text[index + 1]);
            if (trail >= 0xdc00 && trail <= 0xdfff) {
                ch = 0x10000 + ((ch - 0xd800) << 10) + trail - 0xdc00;
                ++index;
            }
        }
        ch += Property(WindowsCaseData::Upper, ch);
        if (ch > 0xffff) {
            ch -= 0x10000;
            output += static_cast<wchar_t>(0xd800 + (ch >> 10));
            output += static_cast<wchar_t>(0xdc00 + (ch & 0x3ff));
        } else {
            output += static_cast<wchar_t>(ch);
        }
    }
    return output;
#endif
}
