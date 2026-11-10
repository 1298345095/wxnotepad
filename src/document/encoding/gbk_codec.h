#pragma once
#include "document/encoding/legacy_encoded.h"
#include <optional>
#include <string_view>

std::optional<std::string> DecodeGbk(std::string_view bytes);
// The caller validates the complete UTF-8 scalar stream before conversion.
LegacyEncoded EncodeGbk(std::string_view utf8);
