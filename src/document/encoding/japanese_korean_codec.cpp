// Original Notepad4 89b8b36 EditLoadFile/EditSaveFile conversion boundaries.
// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
#include "document/encoding/japanese_korean_codec.h"
#include "document/encoding/wx_conversion.h"
#include "UniConversion.h"
#include <wx/strconv.h>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace {
#ifndef __WXMSW__
constexpr unsigned KoreanPuaRowLength = 94;
// Differences measured against Windows' pinned-original conversion APIs for
// every single/two-byte input and every BMP scalar. See docs/encoding.md.
std::optional<wchar_t> CompatibilityDecode(std::string_view bytes, bool japanese, bool strict) {
    const auto lead = static_cast<unsigned char>(bytes[0]);
    if (bytes.size() == 1) {
        if (lead == 0x80) return wchar_t{0x80};
        if (!strict) {
            if (japanese && lead == 0xa0) return wchar_t{0xf8f0};
            if (japanese && lead >= 0xfd) return static_cast<wchar_t>(0xf8f1 + lead - 0xfd);
            if (!japanese && lead == 0xff) return wchar_t{0xf8f7};
        }
    } else if (!japanese) {
        const auto trail = static_cast<unsigned char>(bytes[1]);
        if ((lead == 0xc9 || lead == 0xfe) && trail >= 0xa1 && trail <= 0xfe)
            return static_cast<wchar_t>(0xe000 + (lead == 0xfe ? KoreanPuaRowLength : 0) + trail - 0xa1);
    }
    return std::nullopt;
}
std::optional<std::string> CompatibilityEncode(unsigned scalar, bool japanese) {
    if (scalar == 0x80) return std::string(1, '\x80');
    if (japanese) {
        if (scalar == 0xf8f0) return std::string(1, '\xa0');
        if (scalar >= 0xf8f1 && scalar <= 0xf8f3) return std::string(1, static_cast<char>(0xfd + scalar - 0xf8f1));
    } else {
        if (scalar == 0xf8f7) return std::string(1, '\xff');
        if (scalar >= 0xe000 && scalar < 0xe000 + 2 * KoreanPuaRowLength) {
            const auto offset = scalar - 0xe000;
            return std::string{static_cast<char>(offset < KoreanPuaRowLength ? 0xc9 : 0xfe),
                               static_cast<char>(0xa1 + offset % KoreanPuaRowLength)};
        }
    }
    return std::nullopt;
}
#endif
} // namespace

std::optional<std::string> DecodeJapaneseKorean(std::string_view bytes, FileEncoding encoding, bool strict) {
    using namespace EncodingConversion;
    if (bytes.empty()) return std::string{};
    const bool japanese = encoding == FileEncoding::Windows932;
#ifdef __WXMSW__
    const UINT page = japanese ? 932 : 949;
    const int count = static_cast<int>(bytes.size());
    const DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
    const int size = MultiByteToWideChar(page, flags, bytes.data(), count, nullptr, 0);
    if (!size) return std::nullopt;
    std::wstring text(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(page, flags, bytes.data(), count, text.data(), size);
#else
    const wxCSConv converter(japanese ? "CP932" : "CP949");
    std::wstring text;
    for (size_t offset = 0; offset < bytes.size();) {
        const auto lead = static_cast<unsigned char>(bytes[offset]);
        const bool dbcs = japanese ? ((lead >= 0x81 && lead <= 0x9f) || (lead >= 0xe0 && lead <= 0xfc))
                                   : (lead >= 0x81 && lead <= 0xfe);
        // Windows consumes malformed DBCS pairs together, retaining a NUL.
        const size_t width = dbcs && offset + 1 < bytes.size() && bytes[offset + 1] != '\0' ? 2 : 1;
        const auto piece = bytes.substr(offset, width);
        if (const auto compatible = CompatibilityDecode(piece, japanese, strict))
            text += *compatible;
        else if (const auto converted = ToWide(converter, piece))
            text += *converted;
        else if (strict)
            return std::nullopt;
        else
            text += japanese ? L'\u30fb' : L'?';
        offset += width;
    }
#endif
    return FromWide(wxMBConvUTF8(), text);
}

LegacyEncoded EncodeJapaneseKorean(std::string_view utf8, FileEncoding encoding) {
    using namespace EncodingConversion;
    // DocumentSession validates UTF-8 before reaching legacy encoders.
    const auto text = ToWide(wxMBConvUTF8(), utf8).value();
    if (text.empty()) return {{}, false};
    const bool japanese = encoding == FileEncoding::Windows932;
#ifdef __WXMSW__
    const UINT page = japanese ? 932 : 949;
    BOOL loss = FALSE;
    // Like the original, retain the no-best-fit output when substitution occurs;
    // only a lossless pass is followed by the zero-flags conversion.
    constexpr size_t MaximumEncodedBytesPerUtf8Byte = 2;
    std::string bytes(utf8.size() * MaximumEncodedBytesPerUtf8Byte, '\0');
    const int capacity = static_cast<int>(bytes.size());
    const int count = static_cast<int>(text.size());
    int written =
        WideCharToMultiByte(page, WC_NO_BEST_FIT_CHARS, text.data(), count, bytes.data(), capacity, nullptr, &loss);
    if (!loss) written = WideCharToMultiByte(page, 0, text.data(), count, bytes.data(), capacity, nullptr, nullptr);
    bytes.resize(static_cast<size_t>(written));
    return {bytes, loss != FALSE};
#else
    const wxCSConv converter(japanese ? "CP932" : "CP949");
    LegacyEncoded result{{}, false};
    for (size_t offset = 0; offset < utf8.size();) {
        const auto* lead = reinterpret_cast<const unsigned char*>(utf8.data() + offset);
        const auto width = Scintilla::Internal::UTF8BytesOfLead(*lead);
        const auto scalar = utf8.substr(offset, width);
        auto converted =
            CompatibilityEncode(static_cast<unsigned>(Scintilla::Internal::UnicodeFromUTF8(lead)), japanese);
        if (!converted) converted = FromWide(converter, ToWide(wxMBConvUTF8(), scalar).value());
        if (converted && DecodeJapaneseKorean(*converted, encoding) == scalar)
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
