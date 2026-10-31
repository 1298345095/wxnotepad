#pragma once
#include <optional>
#include <string>
#include <string_view>

std::optional<std::string> DecodeUtf16Le(std::string_view bytes);
std::optional<std::string> EncodeUtf16Le(std::string_view utf8);
