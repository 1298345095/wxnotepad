#include "document/encoding/utf16_codec.h"
#include <wx/strconv.h>
#include <vector>
#include <algorithm>

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

std::optional<std::string> DecodeUtf16Be(std::string_view bytes) {
    // Notepad4 89b8b36 Edit.cpp FileLoad: _swab, integer WCHAR count,
    // then WideCharToMultiByte(CP_UTF8, 0). wx's strict converter needs
    // isolated surrogates replaced before conversion on both host platforms.
    // Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
    std::string little;
    little.reserve(bytes.size());
    for (size_t offset = 0; offset + 1 < bytes.size(); offset += 2) {
        const auto high = static_cast<unsigned char>(bytes[offset]);
        if (high >= 0xd8 && high <= 0xdb && offset + 3 < bytes.size() &&
            static_cast<unsigned char>(bytes[offset + 2]) >= 0xdc &&
            static_cast<unsigned char>(bytes[offset + 2]) <= 0xdf) {
            little += bytes[offset + 1];
            little += bytes[offset];
            little += bytes[offset + 3];
            little += bytes[offset + 2];
            offset += 2;
        } else if (high >= 0xd8 && high <= 0xdf) {
            little += "\xfd\xff";
        } else {
            little += bytes[offset + 1];
            little += bytes[offset];
        }
    }
    return DecodeUtf16Le(little);
}

std::optional<std::string> EncodeUtf16Be(std::string_view utf8) {
    auto bytes = EncodeUtf16Le(utf8);
    if (bytes)
        for (size_t offset = 0; offset + 1 < bytes->size(); offset += 2)
            std::swap((*bytes)[offset], (*bytes)[offset + 1]);
    return bytes;
}
