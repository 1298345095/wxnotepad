#pragma once
#include "document/encoding/encoding_detection.h"
#include "document/encoding/gbk_codec.h"

// Strict conversion is used only by the original detector's candidate gate.
// Explicit file loads retain the original Windows replacement behavior.
std::optional<std::string> DecodeChinese(std::string_view bytes, FileEncoding encoding, bool strict = false);
GbkEncoded EncodeChinese(std::string_view utf8, FileEncoding encoding);
