#pragma once

#include <optional>
#include <string_view>
#include <wx/string.h>

enum class FileEncoding { Utf8, Utf16Le, Gbk, Utf16Be, Gb18030, Big5 };
struct EncodingChoice {
    FileEncoding encoding;
    bool bom;
    bool systemDefault;
    explicit EncodingChoice(FileEncoding value, bool hasBom = false, bool nativeDefault = false)
        : encoding(value), bom(hasBom), systemDefault(nativeDefault) {}
    bool operator==(const EncodingChoice&) const = default;
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
std::optional<EncodingChoice> DefaultEncodingChoice(int setting);
int EncodingSetting(EncodingChoice choice);
std::string_view OriginalEncodingIdentifier(const EncodingChoice& encoding);
std::optional<EncodingChoice> NativeEncodingChoice();
DetectedEncoding DetectEncoding(const wxString& path, std::string_view bytes,
                                std::optional<FileEncoding> explicitEncoding, const EncodingOptions& options = {},
                                std::optional<EncodingChoice> weakEncoding = std::nullopt);
