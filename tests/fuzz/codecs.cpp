#include "support.h"
#include "document/encoding/encoding_detection.h"
#include "document/encoding/utf16_codec.h"
#include "document/encoding/gbk_codec.h"
#include "document/encoding/chinese_codec.h"
#include "document/encoding/single_byte_codec.h"

namespace {
void CheckDecoded(const std::optional<std::string>& text) {
    if (text) Fuzz::Check(IsUtf8(*text), "a successful decoder returns valid UTF-8, including embedded NUL");
}
void CheckUtf16(std::string_view text) {
    const auto little = EncodeUtf16Le(text);
    const auto big = EncodeUtf16Be(text);
    if (!little || !big) Fuzz::Fail("valid UTF-8 is representable in either UTF-16 byte order");
    Fuzz::Check(DecodeUtf16Le(*little) == text && DecodeUtf16Be(*big) == text,
                "valid Unicode roundtrips through UTF-16 without changing scalars");
}
void CheckLegacy(std::string_view text) {
    const auto gbk = EncodeGbk(text);
    if (!gbk.loss) Fuzz::Check(DecodeGbk(gbk.bytes) == text, "lossless GBK encoding preserves Unicode");
    for (const auto encoding : {FileEncoding::Gb18030, FileEncoding::Big5}) {
        const auto encoded = EncodeChinese(text, encoding);
        if (!encoded.loss)
            Fuzz::Check(DecodeChinese(encoded.bytes, encoding) == text, "lossless Chinese encoding preserves Unicode");
    }
    for (const auto encoding : {FileEncoding::Windows1252, FileEncoding::Dos437}) {
        const auto encoded = EncodeSingleByte(text, encoding);
        if (!encoded.loss)
            Fuzz::Check(DecodeSingleByte(encoded.bytes, encoding) == text,
                        "lossless single-byte encoding preserves Unicode");
    }
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    Fuzz::InitializeWx();
    const auto bytes = Fuzz::Bytes(data, size);
    CheckDecoded(DecodeUtf16Le(bytes));
    CheckDecoded(DecodeUtf16Be(bytes));
    CheckDecoded(DecodeGbk(bytes));
    for (const auto encoding : {FileEncoding::Gb18030, FileEncoding::Big5}) {
        CheckDecoded(DecodeChinese(bytes, encoding));
        CheckDecoded(DecodeChinese(bytes, encoding, true));
    }
    for (const auto encoding : {FileEncoding::Windows1252, FileEncoding::Dos437}) {
        const auto decoded = DecodeSingleByte(bytes, encoding);
        Fuzz::Check(IsUtf8(decoded), "every single-byte value has a valid Unicode mapping");
        const auto encoded = EncodeSingleByte(decoded, encoding);
        Fuzz::Check(!encoded.loss && encoded.bytes == bytes, "single-byte mappings roundtrip all source bytes");
    }
    // Legacy encoders have a valid-UTF-8 precondition, enforced by DocumentSession.
    // Malformed byte streams are tested at the decoder/validator boundary instead.
    if (IsUtf8(bytes)) {
        CheckUtf16(bytes);
        CheckLegacy(bytes);
    }
    return 0;
}
