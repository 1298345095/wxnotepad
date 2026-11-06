#include "editor/platform/sort_collation.h"
#include <algorithm>
#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#else
#include "editor/platform/generated/sort_nls_data.h"
#include <wx/mstream.h>
#include <wx/zstream.h>
#include <array>
#include <stdexcept>
#include <vector>

namespace {
struct SortRange {
    uint32_t first;
    uint32_t last;
    int64_t stride;
    uint8_t primaryLength;
    std::string_view key;
};
template <typename T> T ReadLittleEndian(const char*& cursor) {
    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        value |= uint64_t(static_cast<unsigned char>(*cursor++)) << (i * 8);
    return static_cast<T>(value);
}
class SortData {
public:
    SortData() : bytes_(OriginalNls::sortDataSize, '\0') {
        wxMemoryInputStream compressed(OriginalNls::sortData, sizeof(OriginalNls::sortData) - 1);
        wxZlibInputStream input(compressed);
        input.Read(bytes_.data(), bytes_.size());
        if (input.LastRead() != bytes_.size()) throw std::runtime_error("Could not load original NLS sort data");
        const char* cursor = bytes_.data();
        const char* end = cursor + bytes_.size();
        while (cursor < end) {
            SortRange range;
            range.first = ReadLittleEndian<uint32_t>(cursor);
            range.last = ReadLittleEndian<uint32_t>(cursor);
            range.stride = ReadLittleEndian<int64_t>(cursor);
            range.primaryLength = ReadLittleEndian<uint8_t>(cursor);
            const auto length = ReadLittleEndian<uint8_t>(cursor);
            range.key = {cursor, length};
            cursor += length;
            ranges_.push_back(range);
        }
    }
    std::string Key(uint32_t codepoint) const {
        const auto found = std::lower_bound(ranges_.begin(), ranges_.end(), codepoint,
                                            [](const SortRange& range, uint32_t value) { return range.last < value; });
        if (found == ranges_.end() || codepoint < found->first) return Key(0xfffd);
        std::string key(found->key);
        if (found->stride != 0) {
            uint64_t primary = 0;
            for (size_t i = 0; i < found->primaryLength; ++i)
                primary = (primary << 8) | static_cast<unsigned char>(key[i]);
            primary += static_cast<uint64_t>(found->stride * (codepoint - found->first));
            for (size_t i = found->primaryLength; i > 0; --i) {
                key[i - 1] = static_cast<char>(primary);
                primary >>= 8;
            }
        }
        return key;
    }

private:
    std::string bytes_;
    std::vector<SortRange> ranges_;
};
using Levels = std::array<std::string, 5>;
Levels SplitKey(std::string_view key) {
    Levels levels;
    for (size_t i = 0; i < levels.size(); ++i) {
        const auto end = key.find('\1');
        levels[i] = key.substr(0, end);
        if (end == std::string_view::npos) break;
        key.remove_prefix(end + 1);
    }
    return levels;
}
int Decimal(const Levels& key) {
    if (key[0].size() != 2 || key[0][0] != '\x0d') return -1;
    const unsigned value = static_cast<unsigned char>(key[0][1]);
    if (value == 3) return 0;
    return value >= 0x1a && value <= 0x2a && value % 2 == 0 ? static_cast<int>((value - 0x18) / 2) : -1;
}
void AppendLevels(Levels& target, const Levels& source) {
    const size_t elements = source[0].size() / 2;
    for (size_t i = 1; i < 4; ++i) {
        if (elements == 0 && !source[i].empty() && !target[i].empty()) {
            // Combining weights add to the previous collation element.
            target[i].back() = static_cast<char>(static_cast<unsigned char>(target[i].back()) +
                                                 static_cast<unsigned char>(source[i][0]) - 2);
            target[i].append(source[i].substr(1));
        } else {
            target[i] += source[i].empty() ? std::string(elements, '\2') : source[i];
        }
    }
    if (!source[4].empty()) {
        const auto position = static_cast<uint16_t>(0xffff - target[0].size() / 2);
        target[4].push_back(static_cast<char>(position >> 8));
        target[4].push_back(static_cast<char>(position));
        target[4] += source[4].substr(2);
    }
}
std::string LogicalKey(std::wstring_view text) {
    static const SortData data;
    std::vector<Levels> characters;
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t codepoint = static_cast<uint32_t>(text[i]);
        if (codepoint >= 0xd800 && codepoint <= 0xdbff && i + 1 < text.size() && text[i + 1] >= 0xdc00 &&
            text[i + 1] <= 0xdfff)
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (text[++i] - 0xdc00);
        characters.push_back(SplitKey(data.Key(codepoint)));
    }
    Levels result;
    for (size_t i = 0; i < characters.size();) {
        if (Decimal(characters[i]) >= 0) {
            // NLS numeric collation consumes at most 19 decimal digits per element.
            const size_t start = i;
            while (i < characters.size() && i - start < 19 && Decimal(characters[i]) >= 0 &&
                   characters[i][1] == characters[start][1] && characters[i][2] == characters[start][2] &&
                   characters[i][3] == characters[start][3])
                ++i;
            size_t significant = start;
            while (significant < i && Decimal(characters[significant]) == 0)
                ++significant;
            result[0] += "\x0d\x02";
            result[0].push_back(static_cast<char>(i - significant + 2));
            size_t digit = significant;
            if ((i - significant) % 2 != 0) result[0].push_back(static_cast<char>(0x22 + Decimal(characters[digit++])));
            while (digit < i) {
                result[0].push_back(
                    static_cast<char>(((Decimal(characters[digit]) + 2) << 4) + Decimal(characters[digit + 1]) + 2));
                digit += 2;
            }
            result[0].push_back(static_cast<char>(254 - (significant - start)));
            for (size_t index = start; index < i; ++index)
                AppendLevels(result, characters[index]);
        } else {
            AppendLevels(result, characters[i]);
            result[0] += characters[i][0];
            ++i;
        }
    }
    std::string key = std::move(result[0]);
    for (size_t i = 1; i < result.size(); ++i) {
        while (!result[i].empty() && result[i].back() == '\2')
            result[i].pop_back();
        key.push_back('\1');
        key += result[i];
    }
    return key;
}
} // namespace
#endif

OriginalLogicalKey::OriginalLogicalKey(std::wstring_view text)
#ifdef _WIN32
    : text_(text){}
#else
    : key_(LogicalKey(text)) {
}
#endif

      int OriginalLogicalKey::Compare(const OriginalLogicalKey& other) const {
#ifdef _WIN32
    return StrCmpLogicalW(text_.c_str(), other.text_.c_str());
#else
    return key_.compare(other.key_);
#endif
}

size_t OriginalExtensionOffset(const std::wstring& text) {
#ifdef _WIN32
    return static_cast<size_t>(PathFindExtensionW(text.c_str()) - text.c_str());
#else
    size_t extension = text.size();
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\\' || text[i] == L' ')
            extension = text.size();
        else if (text[i] == L'.')
            extension = i;
    }
    return extension;
#endif
}
