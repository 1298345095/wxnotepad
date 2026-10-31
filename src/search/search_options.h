#pragma once

struct SearchOptions {
    bool matchCase = false;
    bool wholeWord = false;
    bool regularExpression = false;
    bool operator==(const SearchOptions&) const = default;
};
