#include "document/encoding/wx_conversion.h"
#include <wx/strconv.h>

std::optional<std::wstring> EncodingConversion::ToWide(const wxMBConv& converter, std::string_view bytes) {
    if (bytes.empty()) return std::wstring{};
    const auto size = converter.ToWChar(nullptr, 0, bytes.data(), bytes.size());
    if (size == wxCONV_FAILED) return std::nullopt;
    std::wstring text(size, L'\0');
    const auto written = converter.ToWChar(text.data(), text.size(), bytes.data(), bytes.size());
    if (written == wxCONV_FAILED) return std::nullopt;
    text.resize(written);
    return text;
}

std::optional<std::string> EncodingConversion::FromWide(const wxMBConv& converter, std::wstring_view text) {
    if (text.empty()) return std::string{};
    const auto size = converter.FromWChar(nullptr, 0, text.data(), text.size());
    if (size == wxCONV_FAILED) return std::nullopt;
    std::string bytes(size, '\0');
    const auto written = converter.FromWChar(bytes.data(), bytes.size(), text.data(), text.size());
    if (written == wxCONV_FAILED) return std::nullopt;
    bytes.resize(written);
    return bytes;
}
