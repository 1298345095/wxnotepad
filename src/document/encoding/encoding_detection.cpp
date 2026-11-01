// Decision order, catalog and file-variable rules adapted from Notepad4
// 89b8b36, src/EditEncoding.cpp and src/Edit.cpp. See licenses/Notepad4.txt.
#include "document/encoding/encoding_detection.h"
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
struct OriginalEncoding {
    unsigned codePage;
    bool legacy;
    std::string_view names;
};
#include "original_encoding_catalog.h"

int CodePageIndex(unsigned page) {
    for (size_t index = 6; index < std::size(catalog); ++index) {
        if (catalog[index].codePage == page) return static_cast<int>(index);
    }
    return -1;
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
    return -1;
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
    constexpr int mapping[] = {0, 2, 3, 6, 7, 1, 4, 5, 8};
    if (value < 0) return 6;
    if (value < static_cast<int>(std::size(mapping))) return mapping[value];
    const int index = CodePageIndex(static_cast<unsigned>(value));
    return index < 0 ? 0 : index;
}

int EncodingIndex(FileEncoding encoding) {
    switch (encoding) {
    case FileEncoding::Utf8:
        return 6;
    case FileEncoding::Utf16Le:
        return 4;
    case FileEncoding::Gbk:
        return CodePageIndex(936);
    }
    return -1;
}

int EncodingIndex(const EncodingChoice& choice) {
    if (choice.systemDefault) return 0;
    if (choice.bom && choice.encoding == FileEncoding::Utf16Le) return 2;
    if (choice.bom && choice.encoding == FileEncoding::Utf8) return 7;
    return EncodingIndex(choice.encoding);
}

bool ValidLegacy(unsigned page, std::string_view bytes) {
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

// Port of FileVars_Find / FileVars_ParseStr (Edit.cpp), including case-sensitive
// keys, full-name boundaries, punctuation skipping and the 31-character token.
std::optional<std::string> FileVariable(std::string_view text, std::string_view name) {
    const bool suffix = name.front() == '*';
    if (suffix) name.remove_prefix(1);
    size_t offset = 0;
    while ((offset = text.find(name, offset)) != std::string_view::npos) {
        const auto previous = offset ? static_cast<unsigned char>(text[offset - 1]) : 0;
        offset += name.size();
        if ((std::isalnum(previous) || previous == '-' || previous == '_' || previous == '.') &&
            !(suffix && previous == '-'))
            continue;
        while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t'))
            ++offset;
        if (offset == text.size() || (text[offset] != ':' && text[offset] != '=' && name.back() != ' ')) continue;
        bool quoted = false;
        while (offset < text.size() && std::string_view(":=\"' \t").find(text[offset]) != std::string_view::npos) {
            quoted |= text[offset] == '\'' || text[offset] == '"';
            ++offset;
        }
        std::string value;
        while (offset < text.size() && value.size() < 31) {
            const auto ch = static_cast<unsigned char>(text[offset++]);
            if (!(std::isalnum(ch) || std::string_view("+-/_#").find(static_cast<char>(ch)) != std::string_view::npos ||
                  (quoted && ch == ' ')))
                break;
            value += static_cast<char>(ch);
        }
        while (!value.empty() && value.back() == ' ')
            value.pop_back();
        if (!value.empty()) return value;
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<int> IntegerVariable(std::string_view text, std::string_view key) {
    const auto value = FileVariable(text, key);
    if (!value) return std::nullopt;
    char* end = nullptr;
    const auto number = std::strtol(value->c_str(), &end, 10);
    if (end != value->c_str())
        return static_cast<int>(std::clamp(number, static_cast<long>(INT_MIN), static_cast<long>(INT_MAX)));
    if (value->front() == 't' || value->front() == 'y') return 1;
    if (value->front() == 'f' || value->front() == 'n') return 0;
    return std::nullopt;
}

struct FileVariables {
    int encoding = -1;
    bool any = false;
};

FileVariables ReadFileVariables(std::string_view bytes, const EncodingOptions& options) {
    for (const auto text : {bytes.substr(0, 512), bytes.substr(bytes.size() > 512 ? bytes.size() - 512 : 0)}) {
        const auto window = text.substr(0, text.find('\0'));
        FileVariables variables;
        if (!options.noFileVariables) {
            if (IntegerVariable(window, "enable-local-variables") == 0) return {};
            for (const auto key : {"tab-width", "*basic-indent", "*basic-offset", "indent-tabs-mode",
                                   "*tab-always-indent", "truncate-lines", "fill-column"}) {
                variables.any |= IntegerVariable(window, key).has_value();
            }
            variables.any |= FileVariable(window, "mode").has_value();
        }
        if (!options.noEncodingTags) {
            for (const auto key : {"encoding", "charset", "coding", "fileencoding", "/*!40101 SET NAMES "}) {
                if (const auto name = FileVariable(window, key)) {
                    const int encoding = MatchEncoding(*name);
                    variables.encoding = encoding >= 2 && encoding <= 5 ? -1 : encoding;
                    variables.any = true;
                    break;
                }
            }
        }
        if (variables.any) return variables;
        if (bytes.size() <= 512) break;
    }
    return {};
}

DetectedEncoding Choice(int index, unsigned native, unsigned oem, bool systemDefault = false) {
    if (index == 2 || index == 4) return {FileEncoding::Utf16Le, index == 2, {}};
    if (index == 3 || index == 5)
        return {std::nullopt, false, "The selected encoding (UTF-16BE) is not migrated. The document was not changed."};
    if (index == 6 || index == 7) return {FileEncoding::Utf8, index == 7, {}};
    const unsigned page = index == 0 ? native : index == 1 ? oem : index >= 0 ? catalog[index].codePage : 0;
    if (page == 936) return {FileEncoding::Gbk, false, {}, systemDefault};
    if (page == 65001) return {FileEncoding::Utf8, false, {}, systemDefault};
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
        if (latin) return order == 1 ? 4 : 5;
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

int UnicodeFallback(std::string_view bytes, bool skipWithoutNull, unsigned native) {
    if (skipWithoutNull && bytes.substr(0, 4096).find('\0') == std::string_view::npos &&
        (bytes.size() <= 4096 || bytes.substr(bytes.size() - 4096).find('\0') == std::string_view::npos))
        return 0;
#ifdef __WXMSW__
    (void)native;
    int flags = 0xffff;
    IsTextUnicode(bytes.data(), static_cast<int>(bytes.size()), &flags);
    if (flags == 0xffff || (flags & IS_TEXT_UNICODE_ILLEGAL_CHARS)) return 0;
    if ((flags & IS_TEXT_UNICODE_UNICODE_MASK) && DecodeUtf16Le(bytes)) return 4;
    if (flags & IS_TEXT_UNICODE_REVERSE_MASK) return 5;
#else
    // The original consumes the result flags, even when IsTextUnicode returns
    // false. Test its Unicode control characters rather than an encoding label
    // guessed by a converter. Check the same leading 256 UTF-16 code units.
    bool controls = false;
    bool reverseControls = false;
    int lowVariation = 0;
    int highVariation = 0;
    int previousLow = 0;
    int previousHigh = 0;
    unsigned crossLineEndings = 0;
    size_t examined = std::min<size_t>(bytes.size(), 512);
    if (bytes.size() <= 512 && bytes.size() > 2 && bytes.back() == '\0') examined -= 2;
    for (size_t offset = 0; offset < examined; offset += 2) {
        const unsigned unit =
            static_cast<unsigned char>(bytes[offset]) | (static_cast<unsigned char>(bytes[offset + 1]) << 8);
        if (unit == 0 || unit == 0xffff || unit == 0xfffe || unit == 0xa0d) return 0;
        controls |= unit == 9 || unit == 10 || unit == 13 || unit == 32 || unit == 0x3000;
        reverseControls |= unit == 0x900 || unit == 0xa00 || unit == 0xd00 || unit == 0x2000;
        const int low = static_cast<int>(unit & 0xff);
        const int high = static_cast<int>(unit >> 8);
        crossLineEndings += (previousHigh == 10 && low == 13) || (previousHigh == 13 && low == 10);
        lowVariation += std::abs(low - previousLow);
        highVariation += std::abs(high - previousHigh);
        previousLow = low;
        previousHigh = high;
    }
    crossLineEndings += (previousHigh == 10 && previousLow == 13) || (previousHigh == 13 && previousLow == 10);
    if (crossLineEndings && crossLineEndings >= std::min<size_t>(bytes.size(), 512) / 40) return 0;
    const auto dbcsLead = [native](unsigned char ch) {
        if (native == 936 || native == 949 || native == 950) return ch >= 0x81 && ch <= 0xfe;
        if (native == 932) return (ch >= 0x81 && ch <= 0x9f) || (ch >= 0xe0 && ch <= 0xfc);
        if (native == 1361)
            return (ch >= 0x84 && ch <= 0xd3) || (ch >= 0xd8 && ch <= 0xde) || (ch >= 0xe0 && ch <= 0xf9);
        return false;
    };
    unsigned leadCount = 0;
    for (size_t offset = 0; offset < std::min<size_t>(bytes.size(), 512); ++offset) {
        if (dbcsLead(static_cast<unsigned char>(bytes[offset]))) {
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
    if ((controls || factor * highVariation < lowVariation) && DecodeUtf16Le(bytes)) return 4;
    if (reverseControls || factor * lowVariation < highVariation) return 5;
#endif
    return 0;
}
} // namespace

int NormalizeEncodingSetting(int value) {
    const int index = DefaultEncoding(value);
    if (index == 6) return -1; // Original global UTF-8 default is omitted on Save.
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
    const auto choose = [=](int index, bool systemDefault = false) {
        return Choice(index, legacy, oem, systemDefault);
    };
    const int strong = explicitEncoding ? EncodingIndex(*explicitEncoding) : -1;
    const int weak = weakEncoding ? EncodingIndex(*weakEncoding) : -1;
    const int fallback = DefaultEncoding(options.defaultEncoding);
    int preferred = -1;
    const auto extension = wxFileName(path).GetExt();
    if (options.loadNFOasOEM && (extension.CmpNoCase("nfo") == 0 || extension.CmpNoCase("diz") == 0))
        preferred = CodePageIndex(437);
    else if (extension.CmpNoCase("bat") == 0 || extension.CmpNoCase("cmd") == 0)
        preferred = 0;
    else if (extension == "sh" || bytes.starts_with("#!/"))
        preferred = 6;
    else if (extension.CmpNoCase("vhd") == 0 || extension.CmpNoCase("vhdl") == 0)
        preferred = CodePageIndex(1252);

    const auto asciiChoice = [&](int index, bool empty) {
        if (!empty && index == 7) index = 6;
        if (index == 0 && (options.loadANSIasUTF8 || options.loadASCIIasUTF8 || native == 65001))
            index = CodePageIndex(native);
        return choose(index, index == 0);
    };
    if (bytes.empty())
        return asciiChoice(strong >= 0               ? strong
                           : weak >= 0               ? weak
                           : preferred >= 0          ? preferred
                           : options.loadASCIIasUTF8 ? 6
                                                     : fallback,
                           true);
    constexpr size_t maxConvertedSize = (1U << 30) - 16;
    if (explicitEncoding && (bytes.size() < maxConvertedSize || *explicitEncoding == FileEncoding::Utf8)) {
        return {explicitEncoding,
                *explicitEncoding == FileEncoding::Utf16Le
                    ? bytes.starts_with("\xff\xfe")
                    : *explicitEncoding == FileEncoding::Utf8 && bytes.starts_with("\xef\xbb\xbf"),
                {}};
    }
    if (bytes.size() < maxConvertedSize && bytes.size() % 2 == 0) {
        if (bytes.starts_with("\xff\xfe")) return choose(2);
        if (bytes.starts_with("\xfe\xff")) return choose(3);
    }
    if (bytes.starts_with("\xef\xbb\xbf")) return choose(7);
    if (bytes.size() >= maxConvertedSize) return choose(IsUtf8(bytes) ? 6 : 0);
    const auto variables = ReadFileVariables(bytes, options);
    const int declared = variables.encoding;
    if (bytes.size() % 2 == 0 && !variables.any) {
        const int latin = LatinUtf16(bytes);
        if (latin) return choose(latin);
    }
    const bool ascii = std::all_of(bytes.begin(), bytes.end(), [](unsigned char ch) { return ch < 128; });
    if (ascii) {
        int index = weak >= 0 && !(weak >= 2 && weak <= 5) ? weak
                    : preferred >= 0                       ? preferred
                    : declared >= 0                        ? declared
                    : options.loadASCIIasUTF8              ? 6
                    : fallback >= 2 && fallback <= 5       ? 0
                                                           : fallback;
        if (index == declared && index > 0 && (catalog[index].codePage == 28591 || catalog[index].codePage == 28599))
            index = CodePageIndex(catalog[index].codePage == 28591 ? 1252 : 1254);
        return asciiChoice(index, false);
    }
    if (IsUtf8(bytes)) return choose(6);
    for (const int index : {weak, preferred, declared, fallback}) {
        if (index > 0 && catalog[index].legacy && ValidLegacy(index == 1 ? oem : catalog[index].codePage, bytes)) {
            unsigned page = index == 1 ? oem : catalog[index].codePage;
            // Original prefers Windows aliases for sniffed ISO-8859 declarations.
            if (index == declared && (page == 28591 || page == 28599)) page = page == 28591 ? 1252 : 1254;
            return choose(index == 1 ? 1 : CodePageIndex(page));
        }
    }
    const bool nativeValid = native != 65001 && ValidLegacy(native, bytes);
    bool binary = false;
    const bool maybeBinary = MaybeBinary(bytes, binary);
    if ((!nativeValid || maybeBinary) && bytes.size() % 2 == 0 && !variables.any) {
        const int unicode = UnicodeFallback(bytes, options.skipUnicodeDetection, native);
        if (unicode) return choose(unicode);
    }
    return choose(0, !nativeValid || binary || !options.loadANSIasUTF8 || weak == 0);
}
