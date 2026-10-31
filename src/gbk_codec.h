#pragma once
#include <optional>
#include <string>
#include <string_view>

std::optional<std::string> DecodeGbk(std::string_view bytes);
struct GbkEncoded { std::string bytes; bool loss; };
// The caller validates the complete UTF-8 scalar stream before conversion.
GbkEncoded EncodeGbk(std::string_view utf8);
