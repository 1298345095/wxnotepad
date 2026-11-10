#include "support.h"
#include "document/encoding/encoding_detection.h"
#include <array>

namespace {
constexpr size_t HeaderBytes = 6;
enum WeakFlags : uint8_t { WeakBom = 1, WeakNativeDefault = 2, WeakNativeOem = 4 };
enum DetectionFlags : uint8_t {
    SkipUnicode = 1,
    AnsiAsUtf8 = 2,
    AsciiAsUtf8 = 4,
    NfoAsOem = 8,
    NoEncodingTags = 16,
    NoFileVariables = 32
};
constexpr std::array<std::optional<FileEncoding>, 9> encodings = {
    std::nullopt,          FileEncoding::Utf8, FileEncoding::Utf16Le,     FileEncoding::Utf16Be, FileEncoding::Gbk,
    FileEncoding::Gb18030, FileEncoding::Big5, FileEncoding::Windows1252, FileEncoding::Dos437};
constexpr std::array defaults = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 1252, 437, 936, 950, 54936, 932};
constexpr std::array paths = {"sample.txt", "sample.nfo", "sample.DIZ",  "sample.bat", "sample.cmd",
                              "sample.sh",  "sample.vhd", "sample.VHDL", "sample.xml", "sample.py"};
bool Same(const DetectedEncoding& first, const DetectedEncoding& second) {
    return first.encoding == second.encoding && first.bom == second.bom && first.error == second.error &&
           first.systemDefault == second.systemDefault && first.systemOem == second.systemOem;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < HeaderBytes) return 0;
    Fuzz::InitializeWx();
    const EncodingOptions options{.defaultEncoding = defaults[data[1] % defaults.size()],
                                  .skipUnicodeDetection = (data[0] & SkipUnicode) != 0,
                                  .loadANSIasUTF8 = (data[0] & AnsiAsUtf8) != 0,
                                  .loadASCIIasUTF8 = (data[0] & AsciiAsUtf8) != 0,
                                  .loadNFOasOEM = (data[0] & NfoAsOem) != 0,
                                  .noEncodingTags = (data[0] & NoEncodingTags) != 0,
                                  .noFileVariables = (data[0] & NoFileVariables) != 0};
    const auto strong = encodings[data[2] % encodings.size()];
    std::optional<EncodingChoice> weak;
    if (const auto codec = encodings[data[3] % encodings.size()])
        weak = EncodingChoice(*codec, (data[5] & WeakBom) != 0);
    if (data[5] & WeakNativeDefault)
        weak = NativeEncodingChoice();
    else if (data[5] & WeakNativeOem)
        weak = NativeOemEncodingChoice();
    const auto path = wxString::FromUTF8(paths[data[4] % paths.size()]);
    const auto bytes = Fuzz::Bytes(data + HeaderBytes, size - HeaderBytes);
    const auto detected = DetectEncoding(path, bytes, strong, options, weak);
    Fuzz::Check(Same(detected, DetectEncoding(path, bytes, strong, options, weak)),
                "detection is deterministic for the same bytes, choices and native locale");
    if (strong) Fuzz::Check(detected.encoding == strong, "an explicit supported encoding takes precedence");
    if (detected.encoding) {
        const EncodingChoice choice(*detected.encoding, detected.bom, detected.systemDefault, detected.systemOem);
        Fuzz::Check(!OriginalEncodingIdentifier(choice).empty(), "detected encodings retain an original identifier");
    }
    const auto normalized = NormalizeEncodingSetting(options.defaultEncoding);
    Fuzz::Check(NormalizeEncodingSetting(normalized) == normalized, "persisted encoding normalization is idempotent");
    return 0;
}
