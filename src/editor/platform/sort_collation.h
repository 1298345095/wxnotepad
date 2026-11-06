#pragma once
#include <string>
#include <string_view>

// Windows keeps the original StrCmpLogicalW call. Linux uses captured NLS
// weights, prepared once per line rather than on every comparator invocation.
class OriginalLogicalKey {
public:
    explicit OriginalLogicalKey(std::wstring_view text);
    int Compare(const OriginalLogicalKey& other) const;

private:
#ifdef _WIN32
    std::wstring text_;
#else
    std::string key_;
#endif
};

size_t OriginalExtensionOffset(const std::wstring& text);
