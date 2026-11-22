#pragma once

#include <optional>
#include <string>
#include <string_view>

class wxMBConv;

namespace EncodingConversion {
// Explicit lengths retain embedded NULs; a failed conversion has no value.
std::optional<std::wstring> ToWide(const wxMBConv& converter, std::string_view bytes);
std::optional<std::string> FromWide(const wxMBConv& converter, std::wstring_view text);
} // namespace EncodingConversion
