#include "support.h"
#include "document/file_variables.h"
#include "settings/original_settings_constants.h"

namespace {
constexpr size_t OriginalEncodingNameMaxBytes = 31;
void CheckVariables(std::string_view bytes, bool noVariables, bool noEncoding) {
    const auto parsed = ReadOriginalFileVariables(bytes, noVariables, noEncoding);
    if (parsed.tabWidth.has_value())
        // An omitted tab width can inherit the original zero indent width.
        Fuzz::Check(*parsed.tabWidth >= 0 && *parsed.tabWidth <= OriginalSettingLimits::MaxTabWidth,
                    "tab width retains the original allowed range");
    if (parsed.indentWidth.has_value())
        Fuzz::Check(*parsed.indentWidth >= 0 && *parsed.indentWidth <= OriginalSettingLimits::MaxTabWidth,
                    "indent width retains the original allowed range");
    if (parsed.longLinesLimit.has_value())
        Fuzz::Check(*parsed.longLinesLimit >= 0 && *parsed.longLinesLimit <= OriginalSettingLimits::MaxLongLineColumn,
                    "long-line columns retain the original allowed range");
    Fuzz::Check(parsed.tabWidth.has_value() == parsed.indentWidth.has_value(),
                "an omitted tab/indent width inherits its counterpart");
    if (noVariables)
        Fuzz::Check(!parsed.tabWidth.has_value() && !parsed.indentWidth.has_value() &&
                        !parsed.longLinesLimit.has_value() && !parsed.tabsAsSpaces.has_value() &&
                        !parsed.tabIndents.has_value(),
                    "disabling file variables leaves editor properties absent");
    if (noEncoding || bytes.starts_with("\xef\xbb\xbf"))
        Fuzz::Check(!parsed.encodingName, "disabled declarations and UTF-8 BOM suppress encoding tags");
    if (parsed.encodingName)
        Fuzz::Check(parsed.encodingName->size() <= OriginalEncodingNameMaxBytes,
                    "encoding tokens retain the original 31-byte limit");
    if (noVariables && noEncoding) Fuzz::Check(!parsed.any, "both disabled sources produce no variables");
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const auto bytes = Fuzz::Bytes(data, size);
    for (const bool noVariables : {false, true}) {
        for (const bool noEncoding : {false, true})
            CheckVariables(bytes, noVariables, noEncoding);
    }
    return 0;
}
