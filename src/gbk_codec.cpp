#include "gbk_codec.h"
#include <wx/strconv.h>
#include <wx/string.h>
#include "UniConversion.h"

namespace {
// Windows CP936 PUA assignments missing from Unix iconv's GBK repertoire.
// Measured with wxWidgets 3.3.3/Win32 strict CP936, matching Notepad4
// 89b8b36e0ee2c23f73be922315f17261ccfbbe1a (EditEncoding.cpp GBK=936).
// 2,149 mappings compressed into contiguous byte/codepoint ranges; see
// docs/evidence/issue-19/README.md for probe provenance.
struct PuaRange {
    unsigned first, last, unicode;
};
constexpr PuaRange puaRanges[] = {
    {0xa140, 0xa17e, 0xe4c6}, {0xa180, 0xa1a0, 0xe505}, {0xa240, 0xa27e, 0xe526}, {0xa280, 0xa2a0, 0xe565},
    {0xa2ab, 0xa2b0, 0xe766}, {0xa2e3, 0xa2e4, 0xe76c}, {0xa2ef, 0xa2f0, 0xe76e}, {0xa2fd, 0xa2fe, 0xe770},
    {0xa340, 0xa37e, 0xe586}, {0xa380, 0xa3a0, 0xe5c5}, {0xa440, 0xa47e, 0xe5e6}, {0xa480, 0xa4a0, 0xe625},
    {0xa4f4, 0xa4fe, 0xe772}, {0xa540, 0xa57e, 0xe646}, {0xa580, 0xa5a0, 0xe685}, {0xa5f7, 0xa5fe, 0xe77d},
    {0xa640, 0xa67e, 0xe6a6}, {0xa680, 0xa6a0, 0xe6e5}, {0xa6b9, 0xa6c0, 0xe785}, {0xa6d9, 0xa6df, 0xe78d},
    {0xa6ec, 0xa6ed, 0xe794}, {0xa6f3, 0xa6f3, 0xe796}, {0xa6f6, 0xa6fe, 0xe797}, {0xa740, 0xa77e, 0xe706},
    {0xa780, 0xa7a0, 0xe745}, {0xa7c2, 0xa7d0, 0xe7a0}, {0xa7f2, 0xa7fe, 0xe7af}, {0xa896, 0xa8a0, 0xe7bc},
    {0xa8bc, 0xa8bc, 0xe7c7}, {0xa8bf, 0xa8bf, 0xe7c8}, {0xa8c1, 0xa8c4, 0xe7c9}, {0xa8ea, 0xa8fe, 0xe7cd},
    {0xa958, 0xa958, 0xe7e2}, {0xa95b, 0xa95b, 0xe7e3}, {0xa95d, 0xa95f, 0xe7e4}, {0xa989, 0xa995, 0xe7e7},
    {0xa997, 0xa9a3, 0xe7f4}, {0xa9f0, 0xa9fe, 0xe801}, {0xaaa1, 0xaafe, 0xe000}, {0xaba1, 0xabfe, 0xe05e},
    {0xaca1, 0xacfe, 0xe0bc}, {0xada1, 0xadfe, 0xe11a}, {0xaea1, 0xaefe, 0xe178}, {0xafa1, 0xaffe, 0xe1d6},
    {0xd7fa, 0xd7fe, 0xe810}, {0xf8a1, 0xf8fe, 0xe234}, {0xf9a1, 0xf9fe, 0xe292}, {0xfaa1, 0xfafe, 0xe2f0},
    {0xfba1, 0xfbfe, 0xe34e}, {0xfca1, 0xfcfe, 0xe3ac}, {0xfda1, 0xfdfe, 0xe40a}, {0xfe50, 0xfe7e, 0xe815},
    {0xfe80, 0xfea0, 0xe844}, {0xfea1, 0xfefe, 0xe468},
};
std::optional<std::wstring> Decode(const wxMBConv& converter, std::string_view bytes) {
    if (bytes.empty()) return std::wstring{};
    const auto length = converter.ToWChar(nullptr, 0, bytes.data(), bytes.size());
    if (length == wxCONV_FAILED) return std::nullopt;
    std::wstring result(length, L'\0');
    const auto written = converter.ToWChar(result.data(), result.size(), bytes.data(), bytes.size());
    if (written == wxCONV_FAILED) return std::nullopt;
    result.resize(written);
    return result;
}
std::optional<std::string> Encode(const wxMBConv& converter, const std::wstring& text) {
    if (text.empty()) return std::string{};
    const auto length = converter.FromWChar(nullptr, 0, text.data(), text.size());
    if (length == wxCONV_FAILED) return std::nullopt;
    std::string result(length, '\0');
    const auto written = converter.FromWChar(result.data(), result.size(), text.data(), text.size());
    if (written == wxCONV_FAILED) return std::nullopt;
    result.resize(written);
    return result;
}
} // namespace
std::optional<std::string> DecodeGbk(std::string_view bytes) {
    const wxCSConv cp936("CP936");
    if (const auto wide = Decode(cp936, bytes)) return Encode(wxMBConvUTF8(), *wide);
    std::wstring wide;
    for (size_t offset = 0; offset < bytes.size();) {
        const auto lead = static_cast<unsigned char>(bytes[offset]);
        const size_t length = lead <= 0x80 ? 1 : 2;
        if (lead == 0xff || length > bytes.size() - offset) return std::nullopt;
        const auto piece = bytes.substr(offset, length);
        if (const auto decoded = Decode(cp936, piece))
            wide += *decoded;
        else {
            if (length != 2) return std::nullopt;
            const unsigned pair = (lead << 8) | static_cast<unsigned char>(piece[1]);
            const PuaRange* found = nullptr;
            for (const auto& range : puaRanges)
                if (pair >= range.first && pair <= range.last) {
                    found = &range;
                    break;
                }
            if (!found) return std::nullopt;
            wide += static_cast<wchar_t>(found->unicode + pair - found->first);
        }
        offset += length;
    }
    return Encode(wxMBConvUTF8(), wide);
}
GbkEncoded EncodeGbk(std::string_view utf8) {
    const auto wide = Decode(wxMBConvUTF8(), utf8);
    const wxCSConv cp936("CP936");
    const auto strict = Encode(cp936, *wide);
    if (strict && DecodeGbk(*strict) == utf8) return {*strict, false};
    // Original EditSaveFile retains WC_NO_BEST_FIT_CHARS output whenever
    // substitution occurs. Supplementary scalars use two UTF-16 placeholders.
    GbkEncoded result{{}, false};
    for (size_t offset = 0; offset < utf8.size();) {
        const auto* lead = reinterpret_cast<const unsigned char*>(utf8.data() + offset);
        const auto length = Scintilla::Internal::UTF8BytesOfLead(*lead);
        const auto scalar = utf8.substr(offset, length);
        std::optional<std::string> converted;
        const auto unicode = static_cast<unsigned>(Scintilla::Internal::UnicodeFromUTF8(lead));
        for (const auto& range : puaRanges) {
            if (unicode >= range.unicode && unicode <= range.unicode + range.last - range.first) {
                const auto pair = range.first + unicode - range.unicode;
                converted = std::string{static_cast<char>(pair >> 8), static_cast<char>(pair & 0xff)};
                break;
            }
        }
        if (!converted) converted = Encode(cp936, *Decode(wxMBConvUTF8(), scalar));
        if (converted && DecodeGbk(*converted) == scalar)
            result.bytes += *converted;
        else {
            result.loss = true;
            result.bytes.append(Scintilla::Internal::UTF16LengthFromUTF8ByteCount(length), '?');
        }
        offset += length;
    }
    return result;
}
