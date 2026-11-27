#pragma once

#include "document/encoding/encoding_detection.h"
#include "document/encoding/legacy_encoded.h"
#include <string>

std::optional<std::string> DecodeJapaneseKorean(std::string_view bytes, FileEncoding encoding, bool strict = false);
LegacyEncoded EncodeJapaneseKorean(std::string_view utf8, FileEncoding encoding);
