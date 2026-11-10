#include "document/encoding/single_byte_codec.h"
#include "UniConversion.h"
#include <wx/strconv.h>
#include <wx/string.h>
#include <array>
#include <algorithm>

namespace {
std::array<std::string, 256> MakeByteMappings(unsigned page) {
    std::array<std::string, 256> mappings;
    const wxCSConv converter(wxString::Format("CP%u", page));
    for (size_t value = 0; value < mappings.size(); ++value) {
        const char byte = static_cast<char>(value);
        // Windows MultiByteToWideChar maps these nominally undefined
        // CP1252 bytes to C1 controls; Unix iconv rejects them. The original
        // all-256 capture establishes their exact scalar values.
        if (page == 1252 && (value == 0x81 || value == 0x8d || value == 0x8f || value == 0x90 || value == 0x9d))
            mappings[value] = wxString(static_cast<wxUniChar>(value)).utf8_string();
        else
            mappings[value] = wxString(&byte, converter, 1).utf8_string();
    }
    return mappings;
}
const std::array<std::string, 256>& ByteMappings(FileEncoding encoding) {
    static const auto windows1252 = MakeByteMappings(1252);
    static const auto dos437 = MakeByteMappings(437);
    return encoding == FileEncoding::Windows1252 ? windows1252 : dos437;
}
} // namespace

std::string DecodeSingleByte(std::string_view bytes, FileEncoding encoding) {
    const auto& mappings = ByteMappings(encoding);
    std::string result;
    for (const unsigned char byte : bytes)
        result += mappings[byte];
    return result;
}

LegacyEncoded EncodeSingleByte(std::string_view utf8, FileEncoding encoding) {
    // Adapt original EditSaveFile's WC_NO_BEST_FIT_CHARS and substitution per
    // UTF-16 code unit. wx/iconv best-fit and undefined-byte behavior differ;
    // inverting the measured decode mapping preserves the original repertoire.
    const auto& mappings = ByteMappings(encoding);
    LegacyEncoded result{{}, false};
    for (size_t offset = 0; offset < utf8.size();) {
        const auto* lead = reinterpret_cast<const unsigned char*>(utf8.data() + offset);
        const auto length = Scintilla::Internal::UTF8BytesOfLead(*lead);
        const auto found = std::find(mappings.begin(), mappings.end(), utf8.substr(offset, length));
        if (found != mappings.end())
            result.bytes += static_cast<char>(found - mappings.begin());
        else {
            result.loss = true;
            result.bytes.append(Scintilla::Internal::UTF16LengthFromUTF8ByteCount(length), '?');
        }
        offset += length;
    }
    return result;
}
