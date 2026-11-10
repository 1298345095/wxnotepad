// Conversion boundaries adapted from Notepad4 89b8b36 EditLoadFile/EditSaveFile.
// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
#include "document/encoding/chinese_codec.h"
#include "UniConversion.h"
#include <algorithm>
#include <wx/strconv.h>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace {
std::optional<std::wstring> Wide(const wxMBConv& converter, std::string_view bytes) {
    if (bytes.empty()) return std::wstring{};
    const auto size = converter.ToWChar(nullptr, 0, bytes.data(), bytes.size());
    if (size == wxCONV_FAILED) return std::nullopt;
    std::wstring text(size, L'\0');
    const auto written = converter.ToWChar(text.data(), text.size(), bytes.data(), bytes.size());
    if (written == wxCONV_FAILED) return std::nullopt;
    text.resize(written);
    return text;
}
std::optional<std::string> Narrow(const wxMBConv& converter, std::wstring_view text) {
    if (text.empty()) return std::string{};
    const auto size = converter.FromWChar(nullptr, 0, text.data(), text.size());
    if (size == wxCONV_FAILED) return std::nullopt;
    std::string bytes(size, '\0');
    const auto written = converter.FromWChar(bytes.data(), bytes.size(), text.data(), text.size());
    if (written == wxCONV_FAILED) return std::nullopt;
    bytes.resize(written);
    return bytes;
}
#ifndef __WXMSW__
// Windows CP54936 retains the original GB18030 mapping. glibc implements
// newer revisions: these 19 two-byte/four-byte swaps were measured against
// Windows for every BMP scalar and every valid two-byte sequence.
struct GbMapping {
    unsigned pair;
    wchar_t pua;
    wchar_t revised;
    unsigned four;
};
constexpr GbMapping gbMappings[] = {
    {0xa6d9, 0xe78d, 0xfe10, 0x84318236}, {0xa6da, 0xe78e, 0xfe12, 0x84318238}, {0xa6db, 0xe78f, 0xfe11, 0x84318237},
    {0xa6dc, 0xe790, 0xfe13, 0x84318239}, {0xa6dd, 0xe791, 0xfe14, 0x84318330}, {0xa6de, 0xe792, 0xfe15, 0x84318331},
    {0xa6df, 0xe793, 0xfe16, 0x84318332}, {0xa6ec, 0xe794, 0xfe17, 0x84318333}, {0xa6ed, 0xe795, 0xfe18, 0x84318334},
    {0xa6f3, 0xe796, 0xfe19, 0x84318335}, {0xa8bc, 0xe7c7, 0x1e3f, 0x8135f437}, {0xfe59, 0xe81e, 0x9fb4, 0x82359037},
    {0xfe61, 0xe826, 0x9fb5, 0x82359038}, {0xfe66, 0xe82b, 0x9fb6, 0x82359039}, {0xfe67, 0xe82c, 0x9fb7, 0x82359130},
    {0xfe6d, 0xe832, 0x9fb8, 0x82359131}, {0xfe7e, 0xe843, 0x9fb9, 0x82359132}, {0xfe90, 0xe854, 0x9fba, 0x82359133},
    {0xfea0, 0xe864, 0x9fbb, 0x82359134},
};
struct Big5PuaRange {
    unsigned firstLead, lastLead, unicode;
};
constexpr Big5PuaRange big5PuaRanges[] = {{0x81, 0x8d, 0xeeb8}, {0x8e, 0xa0, 0xe311}, {0xfa, 0xfe, 0xe000}};
constexpr unsigned Big5TrailsPerLead = 157;
bool Big5Trail(unsigned trail) {
    return (trail >= 0x40 && trail <= 0x7e) || (trail >= 0xa1 && trail <= 0xfe);
}
std::optional<wchar_t> CompatibilityDecode(std::string_view bytes, FileEncoding encoding) {
    unsigned value = 0;
    for (const unsigned char byte : bytes)
        value = (value << 8) | byte;
    if (encoding == FileEncoding::Gb18030) {
        for (const auto& mapping : gbMappings) {
            if (value == mapping.pair) return mapping.pua;
            if (value == mapping.four) return mapping.revised;
        }
    } else if (bytes.size() == 1 && (value == 0x80 || value == 0xff))
        return value == 0x80 ? wchar_t{0x80} : wchar_t{0xf8f8};
    else if (bytes.size() == 2 && Big5Trail(value & 0xff)) {
        const unsigned lead = value >> 8;
        const unsigned trail = value & 0xff;
        for (const auto& range : big5PuaRanges) {
            if (lead >= range.firstLead && lead <= range.lastLead)
                return static_cast<wchar_t>(range.unicode + (lead - range.firstLead) * Big5TrailsPerLead +
                                            (trail <= 0x7e ? trail - 0x40 : trail - 0xa1 + 63));
        }
    }
    return std::nullopt;
}
std::string PackedBytes(unsigned value) {
    std::string bytes;
    if (value > 0xffff) {
        bytes += static_cast<char>(value >> 24);
        bytes += static_cast<char>(value >> 16);
    }
    bytes += static_cast<char>(value >> 8);
    bytes += static_cast<char>(value);
    return bytes;
}
std::optional<std::string> CompatibilityEncode(unsigned scalar, FileEncoding encoding) {
    if (encoding == FileEncoding::Gb18030) {
        for (const auto& mapping : gbMappings) {
            if (scalar == static_cast<unsigned>(mapping.pua)) return PackedBytes(mapping.pair);
            if (scalar == static_cast<unsigned>(mapping.revised)) return PackedBytes(mapping.four);
        }
    } else {
        // Windows selects the CP950 extension duplicates, while glibc selects
        // their older A2 aliases (captured from the original application's Save).
        switch (scalar) {
        case 0x2550:
            return PackedBytes(0xf9f9);
        case 0x255e:
            return PackedBytes(0xf9e9);
        case 0x2561:
            return PackedBytes(0xf9eb);
        case 0x256a:
            return PackedBytes(0xf9ea);
        }
        if (scalar == 0x80) return std::string(1, '\x80');
        if (scalar == 0xf8f8) return std::string(1, '\xff');
        for (const auto& range : big5PuaRanges) {
            if (scalar < range.unicode ||
                scalar >= range.unicode + (range.lastLead - range.firstLead + 1) * Big5TrailsPerLead)
                continue;
            const unsigned offset = scalar - range.unicode;
            const unsigned trail = offset % Big5TrailsPerLead;
            return PackedBytes(((range.firstLead + offset / Big5TrailsPerLead) << 8) |
                               (trail < 63 ? trail + 0x40 : trail - 63 + 0xa1));
        }
    }
    return std::nullopt;
}
#endif
} // namespace

std::optional<std::string> DecodeChinese(std::string_view bytes, FileEncoding encoding, bool strict) {
    if (bytes.empty()) return std::string{};
#ifdef __WXMSW__
    const UINT page = encoding == FileEncoding::Gb18030 ? 54936 : 950;
    const DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
    const int count = static_cast<int>(bytes.size());
    const int size = MultiByteToWideChar(page, flags, bytes.data(), count, nullptr, 0);
    if (!size) return std::nullopt;
    std::wstring text(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(page, flags, bytes.data(), count, text.data(), size);
#else
    const wxCSConv converter(encoding == FileEncoding::Gb18030 ? "GB18030" : "CP950");
    std::wstring text;
    for (size_t offset = 0; offset < bytes.size();) {
        const auto lead = static_cast<unsigned char>(bytes[offset]);
        size_t width = 1;
        if (lead >= 0x81 && lead <= 0xfe) {
            if (encoding == FileEncoding::Big5)
                width = std::min(size_t{2}, bytes.size() - offset);
            else if (bytes.size() - offset >= 4 && bytes[offset + 1] >= '0' && bytes[offset + 1] <= '9' &&
                     static_cast<unsigned char>(bytes[offset + 2]) >= 0x81 &&
                     static_cast<unsigned char>(bytes[offset + 2]) <= 0xfe && bytes[offset + 3] >= '0' &&
                     bytes[offset + 3] <= '9')
                width = 4;
            else if (bytes.size() - offset >= 2 && static_cast<unsigned char>(bytes[offset + 1]) >= 0x40 &&
                     static_cast<unsigned char>(bytes[offset + 1]) <= 0xfe && bytes[offset + 1] != '\x7f')
                width = 2;
        }
        const auto piece = bytes.substr(offset, width);
        const auto compatible = CompatibilityDecode(piece, encoding);
        if (compatible && !(strict && lead == 0xff))
            text += *compatible;
        else if (const auto converted = Wide(converter, piece))
            text += *converted;
        else if (strict)
            return std::nullopt;
        else
            text += L'?';
        offset += width;
    }
#endif
    return Narrow(wxMBConvUTF8(), text);
}

GbkEncoded EncodeChinese(std::string_view utf8, FileEncoding encoding) {
    const auto text = Wide(wxMBConvUTF8(), utf8);
    if (text->empty()) return {{}, false};
#ifdef __WXMSW__
    const UINT page = encoding == FileEncoding::Gb18030 ? 54936 : 950;
    const int count = static_cast<int>(text->size());
    BOOL loss = FALSE;
    // CP54936 is an original IsZeroFlagsCodePage: no default-char pointers.
    const DWORD flags = encoding == FileEncoding::Gb18030 ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL* usedDefault = encoding == FileEncoding::Gb18030 ? nullptr : &loss;
    // CP950's sizing query can return one byte for a substituted best-fit pair
    // while the write still needs two bytes of space. Original EditSaveFile
    // keeps its UTF-8 allocation; retain sufficient conversion capacity here.
    constexpr size_t MaxChineseBytesPerUtf8Byte = 2;
    std::string bytes(utf8.size() * MaxChineseBytesPerUtf8Byte, '\0');
    const int capacity = static_cast<int>(bytes.size());
    int written = WideCharToMultiByte(page, flags, text->data(), count, bytes.data(), capacity, nullptr, usedDefault);
    if (!loss && flags)
        written = WideCharToMultiByte(page, 0, text->data(), count, bytes.data(), capacity, nullptr, nullptr);
    bytes.resize(static_cast<size_t>(written));
    return {bytes, loss != FALSE};
#else
    const wxCSConv converter(encoding == FileEncoding::Gb18030 ? "GB18030" : "CP950");
    GbkEncoded result{{}, false};
    for (size_t offset = 0; offset < utf8.size();) {
        const auto* lead = reinterpret_cast<const unsigned char*>(utf8.data() + offset);
        const auto width = Scintilla::Internal::UTF8BytesOfLead(*lead);
        const auto scalar = utf8.substr(offset, width);
        auto converted =
            CompatibilityEncode(static_cast<unsigned>(Scintilla::Internal::UnicodeFromUTF8(lead)), encoding);
        if (!converted) converted = Narrow(converter, *Wide(wxMBConvUTF8(), scalar));
        if (converted && DecodeChinese(*converted, encoding) == scalar)
            result.bytes += *converted;
        else {
            result.loss = true;
            result.bytes.append(Scintilla::Internal::UTF16LengthFromUTF8ByteCount(width), '?');
        }
        offset += width;
    }
    return result;
#endif
}
