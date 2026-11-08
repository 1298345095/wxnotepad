#include "document/file_variables.h"
#include "settings/original_settings_constants.h"
#include <algorithm>
#include <climits>
#include <cstdlib>

namespace {
bool AlphaNumeric(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

// Notepad4 89b8b36 Edit.cpp::FileVars_Find/ParseInt/ParseStr. Keep case-sensitive
// names, suffix-after-hyphen matching and the original punctuation/token rules.
std::optional<std::string_view> FindVariable(std::string_view text, std::string_view name) {
    const bool suffix = name.front() == '*';
    if (suffix) name.remove_prefix(1);
    size_t offset = 0;
    while ((offset = text.find(name, offset)) != std::string_view::npos) {
        const auto previous = offset ? static_cast<unsigned char>(text[offset - 1]) : 0;
        offset += name.size();
        if ((AlphaNumeric(previous) || previous == '-' || previous == '_' || previous == '.') &&
            !(suffix && previous == '-'))
            continue;
        while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t'))
            ++offset;
        if (offset < text.size() && (text[offset] == ':' || text[offset] == '=' || name.back() == ' '))
            return text.substr(offset);
    }
    return std::nullopt;
}

std::optional<int> IntegerVariable(std::string_view text, std::string_view name) {
    auto value = FindVariable(text, name);
    if (!value) return std::nullopt;
    while (!value->empty() && std::string_view(":=\"' \t").find(value->front()) != std::string_view::npos)
        value->remove_prefix(1);
    const std::string numberText(*value);
    char* end = nullptr;
    const long number = std::strtol(numberText.c_str(), &end, 10);
    if (end != numberText.c_str())
        return static_cast<int>(std::clamp(number, static_cast<long>(INT_MIN), static_cast<long>(INT_MAX)));
    if (!value->empty()) {
        if (value->front() == 't' || value->front() == 'y') return 1;
        if (value->front() == 'f' || value->front() == 'n') return 0;
    }
    return std::nullopt;
}

std::optional<std::string> StringVariable(std::string_view text, std::string_view name) {
    auto value = FindVariable(text, name);
    if (!value) return std::nullopt;
    bool quoted = false;
    while (!value->empty() && std::string_view(":=\"' \t").find(value->front()) != std::string_view::npos) {
        quoted |= value->front() == '\'' || value->front() == '"';
        value->remove_prefix(1);
    }
    std::string result;
    for (const unsigned char ch : value->substr(0, 31)) {
        if (!(AlphaNumeric(ch) || std::string_view("+-/_#").find(static_cast<char>(ch)) != std::string_view::npos ||
              (quoted && ch == ' ')))
            break;
        result += static_cast<char>(ch);
    }
    while (!result.empty() && result.back() == ' ')
        result.pop_back();
    return result;
}
void ReadLocalVariables(std::string_view window, OriginalFileVariables& variables) {
    if (const auto width = IntegerVariable(window, "tab-width"); width.has_value())
        variables.tabWidth = std::clamp(*width, 1, OriginalSettingLimits::MaxTabWidth);
    auto indent = IntegerVariable(window, "*basic-indent");
    if (!indent.has_value()) indent = IntegerVariable(window, "*basic-offset");
    if (indent.has_value()) variables.indentWidth = std::clamp(*indent, 0, OriginalSettingLimits::MaxTabWidth);
    if (const auto tabs = IntegerVariable(window, "indent-tabs-mode"); tabs.has_value())
        variables.tabsAsSpaces = *tabs == 0;
    if (const auto tabs = IntegerVariable(window, "*tab-always-indent"); tabs.has_value())
        variables.tabIndents = *tabs != 0;
    variables.any = variables.tabWidth.has_value() || variables.indentWidth.has_value() ||
                    variables.tabsAsSpaces.has_value() || variables.tabIndents.has_value();
    variables.any |= IntegerVariable(window, "truncate-lines").has_value();
    if (const auto boundary = IntegerVariable(window, "fill-column"); boundary.has_value())
        variables.longLinesLimit = std::clamp(*boundary, 0, OriginalSettingLimits::MaxLongLineColumn);
    variables.any |= variables.longLinesLimit.has_value();
    variables.any |= StringVariable(window, "mode").has_value();
    if (variables.tabWidth.has_value() && !variables.indentWidth.has_value())
        variables.indentWidth = variables.tabWidth;
    if (variables.indentWidth.has_value() && !variables.tabWidth.has_value())
        variables.tabWidth = variables.indentWidth;
}

void ReadEncodingVariable(std::string_view window, OriginalFileVariables& variables) {
    for (const auto key : {"encoding", "charset", "coding", "fileencoding", "/*!40101 SET NAMES "}) {
        if (auto name = StringVariable(window, key)) {
            variables.encodingName = std::move(name);
            variables.any = true;
            return;
        }
    }
}
} // namespace

OriginalFileVariables ReadOriginalFileVariables(std::string_view bytes, bool noFileVariables, bool noEncodingTags) {
    const bool utf8Signature = bytes.starts_with("\xef\xbb\xbf");
    for (const auto text : {bytes.substr(0, 512), bytes.substr(bytes.size() > 512 ? bytes.size() - 512 : 0)}) {
        const auto window = text.substr(0, text.find('\0'));
        OriginalFileVariables variables;
        if (!noFileVariables) {
            if (IntegerVariable(window, "enable-local-variables") == 0) return {};
            ReadLocalVariables(window, variables);
        }
        if (!utf8Signature && !noEncodingTags) ReadEncodingVariable(window, variables);
        if (variables.any) return variables;
        if (bytes.size() <= 512) break;
    }
    return {};
}
