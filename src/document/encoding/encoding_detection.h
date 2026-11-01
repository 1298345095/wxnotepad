#pragma once

#include <optional>
#include <string_view>
#include <wx/string.h>

enum class FileEncoding { Utf8, Utf16Le, Gbk };
struct EncodingChoice {
    FileEncoding encoding;
    bool bom;
    bool systemDefault;
    EncodingChoice(FileEncoding value, bool hasBom = false, bool nativeDefault = false)
        : encoding(value), bom(hasBom), systemDefault(nativeDefault) {}
};
// Original Settings fields, not a second configuration source.
struct EncodingOptions {
    int defaultEncoding = -1;
    bool skipUnicodeDetection = true;
    bool loadANSIasUTF8 = true;
    bool loadASCIIasUTF8 = true;
    bool loadNFOasOEM = true;
    bool noEncodingTags = false;
    bool noFileVariables = false;
};
struct DetectedEncoding {
    std::optional<FileEncoding> encoding;
    bool bom = false;
    wxString error;
    bool systemDefault = false;
};
bool IsUtf8(std::string_view bytes);
int NormalizeEncodingSetting(int value);
DetectedEncoding DetectEncoding(const wxString& path, std::string_view bytes,
                                std::optional<FileEncoding> explicitEncoding, const EncodingOptions& options = {},
                                std::optional<EncodingChoice> weakEncoding = std::nullopt);
