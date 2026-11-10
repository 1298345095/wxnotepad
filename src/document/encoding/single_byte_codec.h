#pragma once
#include "document/encoding/encoding_detection.h"
#include "document/encoding/gbk_codec.h"

std::string DecodeSingleByte(std::string_view bytes, FileEncoding encoding);
// The caller validates UTF-8, as for EncodeGbk.
GbkEncoded EncodeSingleByte(std::string_view utf8, FileEncoding encoding);
