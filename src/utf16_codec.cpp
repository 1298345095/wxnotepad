#include "utf16_codec.h"
#include <wx/strconv.h>
#include <vector>

namespace {
std::optional<std::string> Convert(std::string_view bytes, const wxMBConv& from, const wxMBConv& to) {
    if (bytes.empty()) return std::string{};
    const auto size = from.ToWChar(nullptr, 0, bytes.data(), bytes.size());
    if (size == wxCONV_FAILED) return std::nullopt;
    std::vector<wchar_t> wide(size);
    const auto written = from.ToWChar(wide.data(), wide.size(), bytes.data(), bytes.size());
    if (written == wxCONV_FAILED) return std::nullopt;
    const auto length = to.FromWChar(nullptr, 0, wide.data(), written);
    if (length == wxCONV_FAILED) return std::nullopt;
    std::string result(length, '\0');
    const auto converted = to.FromWChar(result.data(), result.size(), wide.data(), written);
    if (converted == wxCONV_FAILED) return std::nullopt;
    result.resize(converted);
    return result;
}
} // namespace

std::optional<std::string> DecodeUtf16Le(std::string_view bytes) {
    return Convert(bytes, wxMBConvUTF16LE{}, wxMBConvUTF8{});
}

std::optional<std::string> EncodeUtf16Le(std::string_view utf8) {
    return Convert(utf8, wxMBConvUTF8{}, wxMBConvUTF16LE{});
}
