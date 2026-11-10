// Decision order, catalog and file-variable rules adapted from Notepad4
// 89b8b36, src/EditEncoding.cpp and src/Edit.cpp. See licenses/Notepad4.txt.
#include "document/encoding/encoding_detection.h"
#include "document/file_variables.h"
#include "document/encoding/gbk_codec.h"
#include "document/encoding/utf16_codec.h"
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/strconv.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace {
// Original Edit.h catalog indices. These differ from the persisted INI order.
enum OriginalEncodingIndex {
    CPI_NONE = -1,
    CPI_DEFAULT = 0,
    CPI_OEM = 1,
    CPI_UNICODEBOM = 2,
    CPI_UNICODEBEBOM = 3,
    CPI_UNICODE = 4,
    CPI_UNICODEBE = 5,
    CPI_UTF8 = 6,
    CPI_UTF8SIGN = 7,
    CPI_UTF7 = 8,
};

constexpr bool IsUnicodeIndex(int index) {
    return index >= CPI_UNICODEBOM && index <= CPI_UNICODEBE;
}

struct OriginalEncoding {
    unsigned codePage;
    bool legacy;
    std::string_view names;
};
#include "original_encoding_catalog.h"

int CodePageIndex(unsigned page) {
    for (size_t index = CPI_UTF8; index < std::size(catalog); ++index) {
        if (catalog[index].codePage == page) return static_cast<int>(index);
    }
    return CPI_NONE;
}

int MatchEncoding(std::string_view name) {
    std::string normalized = ",";
    for (const unsigned char ch : name) {
        if (ch != '-' && ch != '_') normalized += static_cast<char>(std::tolower(ch));
    }
    normalized += ',';
    for (size_t index = 0; index < std::size(catalog); ++index) {
        if (catalog[index].names.find(normalized) != std::string_view::npos) return static_cast<int>(index);
    }
    return CPI_NONE;
}

unsigned NativeCodePage() {
#ifdef __WXMSW__
    return GetACP();
#else
    const auto name = wxLocale::GetSystemEncodingName().utf8_string();
    const int index = MatchEncoding(name);
    return index >= 0 ? catalog[index].codePage : 0;
#endif
}

unsigned OemCodePage(unsigned native) {
#ifdef __WXMSW__
    (void)native;
    UINT page = GetOEMCP();
    if (page == CP_UTF8) {
        // Same numeric locale input as original GetLegacyOEMCP.
        constexpr LCTYPE legacyOem = 0x00000999;
        GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, legacyOem | LOCALE_RETURN_NUMBER, reinterpret_cast<LPWSTR>(&page),
                        sizeof(page) / sizeof(WCHAR));
    }
    return page;
#else
    return native;
#endif
}

unsigned LegacyCodePage(unsigned native) {
#ifdef __WXMSW__
    UINT page = native;
    if (page == CP_UTF8) {
        // LOCALE_IUSEUTF8LEGACYACP, used by pinned GetLegacyACP.
        constexpr LCTYPE legacyAnsi = 0x00000666;
        GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, legacyAnsi | LOCALE_RETURN_NUMBER, reinterpret_cast<LPWSTR>(&page),
                        sizeof(page) / sizeof(WCHAR));
    }
    return page;
#else
    return native;
#endif
}

int DefaultEncoding(int value) {
    constexpr int mapping[] = {CPI_DEFAULT, CPI_UNICODEBOM, CPI_UNICODEBEBOM, CPI_UTF8, CPI_UTF8SIGN,
                               CPI_OEM,     CPI_UNICODE,    CPI_UNICODEBE,    CPI_UTF7};
    if (value < 0) return CPI_UTF8;
    if (value < static_cast<int>(std::size(mapping))) return mapping[value];
    const int index = CodePageIndex(static_cast<unsigned>(value));
    return index < 0 ? CPI_DEFAULT : index;
}

int EncodingIndex(FileEncoding encoding) {
    switch (encoding) {
    case FileEncoding::Utf8:
        return CPI_UTF8;
    case FileEncoding::Utf16Le:
        return CPI_UNICODE;
    case FileEncoding::Utf16Be:
        return CPI_UNICODEBE;
    case FileEncoding::Gbk:
        return CodePageIndex(936);
    case FileEncoding::Windows1252:
        return CodePageIndex(1252);
    case FileEncoding::Dos437:
        return CodePageIndex(437);
    }
    return CPI_NONE;
}

int EncodingIndex(const EncodingChoice& choice) {
    if (choice.systemDefault) return CPI_DEFAULT;
    if (choice.systemOem) return CPI_OEM;
    if (choice.bom && choice.encoding == FileEncoding::Utf16Le) return CPI_UNICODEBOM;
    if (choice.bom && choice.encoding == FileEncoding::Utf16Be) return CPI_UNICODEBEBOM;
    if (choice.bom && choice.encoding == FileEncoding::Utf8) return CPI_UTF8SIGN;
    return EncodingIndex(choice.encoding);
}

bool ValidLegacy(unsigned page, std::string_view bytes) {
    // Windows accepts every byte in these single-byte repertoires. In
    // particular iconv rejects CP1252's five C1 mappings and may not expose the
    // numeric ISO-8859-1 converter name used before the original alias rewrite.
    if (page == 1252 || page == 437 || page == 28591) return true;
    if (page == 936) return DecodeGbk(bytes).has_value();
    if (page == 65001) return IsUtf8(bytes);
    if (page == 0) return false;
#ifdef __WXMSW__
    return bytes.size() <= INT_MAX && MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, bytes.data(),
                                                          static_cast<int>(bytes.size()), nullptr, 0) != 0;
#else
    const wxCSConv converter(wxString::Format("CP%u", page));
    return converter.IsOk() && converter.ToWChar(nullptr, 0, bytes.data(), bytes.size()) != wxCONV_FAILED;
#endif
}

struct FileVariables {
    int encoding = CPI_NONE;
    bool any = false;
};

FileVariables ReadFileVariables(std::string_view bytes, const EncodingOptions& options) {
    const auto parsed = ReadOriginalFileVariables(bytes, options.noFileVariables, options.noEncodingTags);
    const int encoding = parsed.encodingName ? MatchEncoding(*parsed.encodingName) : CPI_NONE;
    return {IsUnicodeIndex(encoding) ? CPI_NONE : encoding, parsed.any};
}

DetectedEncoding Choice(int index, unsigned native, unsigned oem, bool systemDefault = false) {
    if (index == CPI_UNICODEBOM || index == CPI_UNICODE) return {FileEncoding::Utf16Le, index == CPI_UNICODEBOM, {}};
    if (index == CPI_UNICODEBEBOM || index == CPI_UNICODEBE)
        return {FileEncoding::Utf16Be, index == CPI_UNICODEBEBOM, {}};
    if (index == CPI_UTF8 || index == CPI_UTF8SIGN) return {FileEncoding::Utf8, index == CPI_UTF8SIGN, {}};
    const unsigned page = index == CPI_DEFAULT ? native
                          : index == CPI_OEM   ? oem
                          : index >= 0         ? catalog[index].codePage
                                               : 0;
    if (page == 936) return {FileEncoding::Gbk, false, {}, systemDefault, index == CPI_OEM};
    if (page == 1252) return {FileEncoding::Windows1252, false, {}, systemDefault, index == CPI_OEM};
    if (page == 437) return {FileEncoding::Dos437, false, {}, systemDefault, index == CPI_OEM};
    if (page == 65001) return {FileEncoding::Utf8, false, {}, systemDefault, index == CPI_OEM};
    return {
        std::nullopt, false,
        wxString::Format("The selected encoding (code page %u) is not migrated. The document was not changed.", page)};
}

int LatinUtf16(std::string_view bytes) {
    // Scalar equivalent of original DetectUTF16LatinExt: every code unit is < 0x800.
    for (const unsigned order : {1U, 0U}) {
        bool latin = true;
        for (size_t offset = order; offset < bytes.size(); offset += 2) {
            if (static_cast<unsigned char>(bytes[offset]) >= 8) {
                latin = false;
                break;
            }
        }
        if (latin) return order == 1 ? CPI_UNICODE : CPI_UNICODEBE;
    }
    return 0;
}

bool MaybeBinary(std::string_view bytes, bool& binary) {
    // Original MaybeBinaryFile: eight C0 controls or two adjacent controls,
    // excluding the whitespace range TAB..CR, in the first 1024 bytes.
    const auto control = [](unsigned char ch) { return ch < 32 && (ch < 9 || ch > 13); };
    unsigned count = 0;
    unsigned mask = 0;
    bool adjacent = false;
    for (size_t offset = 0; offset < std::min<size_t>(bytes.size(), 1024); ++offset) {
        if (control(static_cast<unsigned char>(bytes[offset]))) {
            mask |= 1U << static_cast<unsigned char>(bytes[offset]);
            adjacent |= ++count >= 8 || (offset + 1 < std::min<size_t>(bytes.size(), 1024) &&
                                         control(static_cast<unsigned char>(bytes[offset + 1])));
            binary = adjacent && (mask & (mask - 1)) != 0;
            if (binary) break;
        }
    }
    return adjacent;
}

#ifndef __WXMSW__
struct Utf16Statistics {
    bool controls = false;
    bool reverseControls = false;
    int lowVariation = 0;
    int highVariation = 0;
};

std::optional<Utf16Statistics> ExamineUtf16Bytes(std::string_view bytes) {
    // The original consumes the result flags, even when IsTextUnicode returns
    // false. Test its Unicode control characters rather than an encoding label
    // guessed by a converter. Check the same leading 256 UTF-16 code units.
    Utf16Statistics statistics;
    int previousLow = 0;
    int previousHigh = 0;
    unsigned crossLineEndings = 0;
    size_t examined = std::min<size_t>(bytes.size(), 512);
    if (bytes.size() <= 512 && bytes.size() > 2 && bytes.back() == '\0') examined -= 2;
    for (size_t offset = 0; offset < examined; offset += 2) {
        const unsigned unit =
            static_cast<unsigned char>(bytes[offset]) | (static_cast<unsigned char>(bytes[offset + 1]) << 8);
        if (unit == 0 || unit == 0xffff || unit == 0xfffe || unit == 0xa0d) return std::nullopt;
        statistics.controls |= unit == 9 || unit == 10 || unit == 13 || unit == 32 || unit == 0x3000;
        statistics.reverseControls |= unit == 0x900 || unit == 0xa00 || unit == 0xd00 || unit == 0x2000;
        const int low = static_cast<int>(unit & 0xff);
        const int high = static_cast<int>(unit >> 8);
        crossLineEndings += (previousHigh == 10 && low == 13) || (previousHigh == 13 && low == 10);
        statistics.lowVariation += std::abs(low - previousLow);
        statistics.highVariation += std::abs(high - previousHigh);
        previousLow = low;
        previousHigh = high;
    }
    crossLineEndings += (previousHigh == 10 && previousLow == 13) || (previousHigh == 13 && previousLow == 10);
    if (crossLineEndings && crossLineEndings >= std::min<size_t>(bytes.size(), 512) / 40) return std::nullopt;
    return statistics;
}

bool IsDbcsLead(unsigned char ch, unsigned native) {
    if (native == 936 || native == 949 || native == 950) return ch >= 0x81 && ch <= 0xfe;
    if (native == 932) return (ch >= 0x81 && ch <= 0x9f) || (ch >= 0xe0 && ch <= 0xfc);
    if (native == 1361) return (ch >= 0x84 && ch <= 0xd3) || (ch >= 0xd8 && ch <= 0xde) || (ch >= 0xe0 && ch <= 0xf9);
    return false;
}

int DbcsVariationFactor(std::string_view bytes, unsigned native) {
    unsigned leadCount = 0;
    for (size_t offset = 0; offset < std::min<size_t>(bytes.size(), 512); ++offset) {
        if (IsDbcsLead(static_cast<unsigned char>(bytes[offset]), native)) {
            ++leadCount;
            ++offset;
        }
    }
    // Native RtlIsTextUnicode compares byte variation with this DBCS-adjusted
    // factor. Previous bytes start at zero; UTF-8/native SBCS uses factor three.
    int factor = 3;
    if (leadCount) {
        const auto base = static_cast<uint32_t>(std::min<size_t>(bytes.size() / 2, 256) / 2) - 1;
        factor = leadCount < base / 3 ? 3 : leadCount < 2 * base / 3 ? 2 : 1;
    }
    return factor;
}
#endif

int UnicodeFallback(std::string_view bytes, bool skipWithoutNull, unsigned native) {
    if (skipWithoutNull && bytes.substr(0, 4096).find('\0') == std::string_view::npos &&
        (bytes.size() <= 4096 || bytes.substr(bytes.size() - 4096).find('\0') == std::string_view::npos))
        return 0;
#ifdef __WXMSW__
    (void)native;
    int flags = 0xffff;
    IsTextUnicode(bytes.data(), static_cast<int>(bytes.size()), &flags);
    if (flags == 0xffff || (flags & IS_TEXT_UNICODE_ILLEGAL_CHARS)) return 0;
    if ((flags & IS_TEXT_UNICODE_UNICODE_MASK) && DecodeUtf16Le(bytes)) return CPI_UNICODE;
    if (flags & IS_TEXT_UNICODE_REVERSE_MASK) return CPI_UNICODEBE;
#else
    const auto statistics = ExamineUtf16Bytes(bytes);
    if (!statistics.has_value()) return 0;
    const auto factor = DbcsVariationFactor(bytes, native);
    if ((statistics->controls || factor * statistics->highVariation < statistics->lowVariation) && DecodeUtf16Le(bytes))
        return CPI_UNICODE;
    if (statistics->reverseControls || factor * statistics->lowVariation < statistics->highVariation)
        return CPI_UNICODEBE;
#endif
    return 0;
}
int PreferredEncoding(const wxString& path, std::string_view bytes, const EncodingOptions& options) {
    const auto extension = wxFileName(path).GetExt();
    if (options.loadNFOasOEM && (extension.CmpNoCase("nfo") == 0 || extension.CmpNoCase("diz") == 0))
        return CodePageIndex(437);
    if (extension.CmpNoCase("bat") == 0 || extension.CmpNoCase("cmd") == 0) return CPI_DEFAULT;
    if (extension == "sh" || bytes.starts_with("#!/")) return CPI_UTF8;
    if (extension.CmpNoCase("vhd") == 0 || extension.CmpNoCase("vhdl") == 0) return CodePageIndex(1252);
    return CPI_NONE;
}

struct EncodingCandidates {
    int strong;
    int weak;
    int fallback;
    int preferred;
    int declared = CPI_NONE;
};

int EmptyFileEncoding(const EncodingCandidates& candidates, bool loadAsciiAsUtf8) {
    if (candidates.strong >= 0) return candidates.strong;
    if (candidates.weak >= 0) return candidates.weak;
    if (candidates.preferred >= 0) return candidates.preferred;
    return loadAsciiAsUtf8 ? CPI_UTF8 : candidates.fallback;
}

int AsciiFileEncoding(const EncodingCandidates& candidates, bool loadAsciiAsUtf8) {
    int index;
    if (candidates.weak >= 0 && !IsUnicodeIndex(candidates.weak))
        index = candidates.weak;
    else if (candidates.preferred >= 0)
        index = candidates.preferred;
    else if (candidates.declared >= 0)
        index = candidates.declared;
    else if (loadAsciiAsUtf8)
        index = CPI_UTF8;
    else
        index = IsUnicodeIndex(candidates.fallback) ? CPI_DEFAULT : candidates.fallback;
    if (index == candidates.declared && index > CPI_DEFAULT &&
        (catalog[index].codePage == 28591 || catalog[index].codePage == 28599))
        index = CodePageIndex(catalog[index].codePage == 28591 ? 1252 : 1254);
    return index;
}

int LegacyFileEncoding(std::string_view bytes, const EncodingCandidates& candidates, unsigned oem) {
    for (const int index : {candidates.weak, candidates.preferred, candidates.declared, candidates.fallback}) {
        if (index > CPI_DEFAULT && catalog[index].legacy &&
            ValidLegacy(index == CPI_OEM ? oem : catalog[index].codePage, bytes)) {
            unsigned page = index == CPI_OEM ? oem : catalog[index].codePage;
            // Original prefers Windows aliases for sniffed ISO-8859 declarations.
            if (index == candidates.declared && (page == 28591 || page == 28599)) page = page == 28591 ? 1252 : 1254;
            return index == CPI_OEM ? CPI_OEM : CodePageIndex(page);
        }
    }
    return CPI_NONE;
}

DetectedEncoding AsciiChoice(int index, bool empty, const EncodingOptions& options, unsigned native, unsigned legacy,
                             unsigned oem) {
    if (!empty && index == CPI_UTF8SIGN) index = CPI_UTF8;
    if (index == CPI_DEFAULT && (options.loadANSIasUTF8 || options.loadASCIIasUTF8 || native == 65001))
        index = CodePageIndex(native);
    return Choice(index, legacy, oem, index == CPI_DEFAULT);
}

std::optional<DetectedEncoding> DetectSignatureEncoding(std::string_view bytes,
                                                        std::optional<FileEncoding> explicitEncoding, unsigned legacy,
                                                        unsigned oem) {
    constexpr size_t maxConvertedSize = (1U << 30) - 16;
    if (explicitEncoding && (bytes.size() < maxConvertedSize || *explicitEncoding == FileEncoding::Utf8)) {
        return DetectedEncoding{explicitEncoding,
                                *explicitEncoding == FileEncoding::Utf16Le ? bytes.starts_with("\xff\xfe")
                                : *explicitEncoding == FileEncoding::Utf16Be
                                    ? bytes.starts_with("\xfe\xff")
                                    : *explicitEncoding == FileEncoding::Utf8 && bytes.starts_with("\xef\xbb\xbf"),
                                {}};
    }
    if (bytes.size() < maxConvertedSize && bytes.size() % 2 == 0) {
        if (bytes.starts_with("\xff\xfe")) return Choice(CPI_UNICODEBOM, legacy, oem);
        if (bytes.starts_with("\xfe\xff")) return Choice(CPI_UNICODEBEBOM, legacy, oem);
    }
    if (bytes.starts_with("\xef\xbb\xbf")) return Choice(CPI_UTF8SIGN, legacy, oem);
    if (bytes.size() >= maxConvertedSize) return Choice(IsUtf8(bytes) ? CPI_UTF8 : CPI_DEFAULT, legacy, oem);
    return std::nullopt;
}
} // namespace

std::string_view OriginalEncodingIdentifier(const EncodingChoice& encoding) {
    const auto names = catalog[EncodingIndex(encoding)].names;
    const auto separator = names.find(',');
    if (separator != std::string_view::npos) return names.substr(0, separator);
    // The original falls back to the status item's short display name for the
    // BOM UTF-16 catalog entries, whose parse-name lists are empty.
    return encoding.encoding == FileEncoding::Utf16Be ? "UTF-16BE BOM" : "UTF-16LE BOM";
}

std::optional<EncodingChoice> NativeEncodingChoice() {
    const auto native = NativeCodePage();
    const auto choice = Choice(CPI_DEFAULT, native, OemCodePage(native), true);
    if (!choice.encoding) return std::nullopt;
    return EncodingChoice(*choice.encoding, false, true);
}

std::optional<EncodingChoice> NativeOemEncodingChoice() {
    const auto native = NativeCodePage();
    const auto choice = Choice(CPI_OEM, native, OemCodePage(native));
    if (!choice.encoding) return std::nullopt;
    return EncodingChoice(*choice.encoding, false, false, true);
}

int NormalizeEncodingSetting(int value) {
    const int index = DefaultEncoding(value);
    if (index == CPI_UTF8) return CPI_NONE; // Original global UTF-8 default is omitted on Save.
    // Persisted INI values, indexed by the original catalog order.
    constexpr int mapping[] = {0, 5, 1, 2, 6, 7, 3, 4, 8};
    return index < static_cast<int>(std::size(mapping)) ? mapping[index] : static_cast<int>(catalog[index].codePage);
}

bool IsUtf8(std::string_view bytes) {
    for (size_t i = 0; i < bytes.size();) {
        const auto lead = static_cast<unsigned char>(bytes[i++]);
        if (lead < 0x80) continue;
        unsigned int value;
        size_t remaining;
        unsigned int minimum;
        if (lead >= 0xc2 && lead <= 0xdf) {
            value = lead & 0x1f;
            remaining = 1;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            value = lead & 0x0f;
            remaining = 2;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            value = lead & 7;
            remaining = 3;
            minimum = 0x10000;
        } else
            return false;
        if (remaining > bytes.size() - i) return false;
        while (remaining--) {
            const auto next = static_cast<unsigned char>(bytes[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

DetectedEncoding DetectEncoding(const wxString& path, std::string_view bytes,
                                std::optional<FileEncoding> explicitEncoding, const EncodingOptions& options,
                                std::optional<EncodingChoice> weakEncoding) {
    const auto native = NativeCodePage();
    const auto legacy = LegacyCodePage(native);
    const auto oem = OemCodePage(native);
    const auto choose = [legacy, oem](int index, bool systemDefault = false) {
        return Choice(index, legacy, oem, systemDefault);
    };
    EncodingCandidates candidates{.strong = explicitEncoding ? EncodingIndex(*explicitEncoding) : CPI_NONE,
                                  .weak = weakEncoding ? EncodingIndex(*weakEncoding) : CPI_NONE,
                                  .fallback = DefaultEncoding(options.defaultEncoding),
                                  .preferred = PreferredEncoding(path, bytes, options)};

    if (bytes.empty())
        return AsciiChoice(EmptyFileEncoding(candidates, options.loadASCIIasUTF8), true, options, native, legacy, oem);
    if (const auto signature = DetectSignatureEncoding(bytes, explicitEncoding, legacy, oem)) return *signature;
    const auto variables = ReadFileVariables(bytes, options);
    candidates.declared = variables.encoding;
    if (bytes.size() % 2 == 0 && !variables.any) {
        const int latin = LatinUtf16(bytes);
        if (latin) return choose(latin);
    }
    const bool ascii = std::all_of(bytes.begin(), bytes.end(), [](unsigned char ch) { return ch < 128; });
    if (ascii) {
        return AsciiChoice(AsciiFileEncoding(candidates, options.loadASCIIasUTF8), false, options, native, legacy, oem);
    }
    if (IsUtf8(bytes)) return choose(CPI_UTF8);
    const int legacyChoice = LegacyFileEncoding(bytes, candidates, oem);
    if (legacyChoice >= 0) return choose(legacyChoice);
    const bool nativeValid = native != 65001 && ValidLegacy(native, bytes);
    bool binary = false;
    const bool maybeBinary = MaybeBinary(bytes, binary);
    if ((!nativeValid || maybeBinary) && bytes.size() % 2 == 0 && !variables.any) {
        const int unicode = UnicodeFallback(bytes, options.skipUnicodeDetection, native);
        if (unicode) return choose(unicode);
    }
    return choose(CPI_DEFAULT, !nativeValid || binary || !options.loadANSIasUTF8 || candidates.weak == CPI_DEFAULT);
}
