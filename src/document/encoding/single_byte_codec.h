#pragma once
#include "document/encoding/encoding_detection.h"
#include "document/encoding/legacy_encoded.h"

std::string DecodeSingleByte(std::string_view bytes, FileEncoding encoding);
// The caller validates UTF-8, as for EncodeGbk.
LegacyEncoded EncodeSingleByte(std::string_view utf8, FileEncoding encoding);
