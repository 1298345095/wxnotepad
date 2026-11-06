#pragma once

#include <optional>
#include <string>
#include <string_view>

struct OriginalFileVariables {
    std::optional<int> tabWidth;
    std::optional<int> indentWidth;
    std::optional<int> longLinesLimit;
    std::optional<bool> tabsAsSpaces;
    std::optional<bool> tabIndents;
    std::optional<std::string> encodingName;
    bool any = false;
};

OriginalFileVariables ReadOriginalFileVariables(std::string_view bytes, bool noFileVariables = false,
                                                bool noEncodingTags = false);
