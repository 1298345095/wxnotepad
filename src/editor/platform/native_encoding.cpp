#include "editor/platform/native_encoding.h"
#include "UniConversion.h"
#include "ScintillaTypes.h"
#include "ILexer.h"
#include "CharClassify.h"
#include <algorithm>
#include <optional>

namespace {
#ifdef __WXMSW__
std::optional<std::string> Convert(std::string_view input, UINT source, UINT target, DWORD flags) {
    if (input.empty()) return std::string();
    const auto length = static_cast<int>(input.size());
    const int units = MultiByteToWideChar(source, flags, input.data(), length, nullptr, 0);
    if (!units) return std::nullopt;
    std::wstring wide(static_cast<size_t>(units), L'\0');
    MultiByteToWideChar(source, flags, input.data(), length, wide.data(), units);
    const int size = WideCharToMultiByte(target, 0, wide.data(), units, nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(target, 0, wide.data(), units, output.data(), size, nullptr, nullptr);
    return output;
}
#endif
} // namespace

std::string NativeDefaultEncodingBytes(std::string_view text) {
#ifdef __WXMSW__
    if (GetACP() != CP_UTF8) {
        std::string result;
        size_t run = 0;
        for (size_t offset = 0; offset < text.size();) {
            const auto status = Scintilla::Internal::UTF8Classify(text.substr(offset));
            if (status & Scintilla::Internal::UTF8MaskInvalid) {
                if (offset != run) result += *Convert(text.substr(run, offset - run), CP_UTF8, CP_ACP, 0);
                result += text[offset++];
                run = offset;
            } else {
                offset += static_cast<size_t>(status & Scintilla::Internal::UTF8MaskWidth);
            }
        }
        if (run != text.size()) result += *Convert(text.substr(run), CP_UTF8, CP_ACP, 0);
        return result;
    }
#endif
    return std::string(text);
}

std::string NativeDefaultEncodingBuffer(std::string_view bytes, NativeByteRanges* raw,
                                        [[maybe_unused]] std::optional<unsigned> codePage) {
    if (raw) raw->clear();
#ifdef __WXMSW__
    if (const auto native = codePage.value_or(GetACP()); native != CP_UTF8) {
        // Most buffers take the bulk conversion. Check round-trip identity too:
        // distinct native byte sequences can name the same Unicode character.
        if (const auto text = Convert(bytes, native, CP_UTF8, MB_ERR_INVALID_CHARS);
            text && Convert(*text, CP_UTF8, native, 0) == bytes)
            return *text;
        constexpr size_t maxLegacyCharacterBytes = 4; // GB18030 is the widest supported legacy encoding.
        std::string result;
        const auto appendRaw = [&](std::string_view part) {
            const auto start = static_cast<intptr_t>(result.size());
            const auto end = start + static_cast<intptr_t>(part.size());
            if (raw) {
                if (!raw->empty() && raw->back().end == start)
                    raw->back().end = end;
                else
                    raw->push_back({start, end});
            }
            result += part;
        };
        for (size_t offset = 0; offset < bytes.size();) {
            size_t consumed = 0;
            const auto maximum = std::min(maxLegacyCharacterBytes, bytes.size() - offset);
            for (size_t width = 1; width <= maximum; ++width) {
                const auto part = bytes.substr(offset, width);
                if (const auto text = Convert(part, native, CP_UTF8, MB_ERR_INVALID_CHARS)) {
                    if (Convert(*text, CP_UTF8, native, 0) == part)
                        result += *text;
                    else
                        appendRaw(part);
                    consumed = width;
                    break;
                }
            }
            if (!consumed) {
                appendRaw(bytes.substr(offset, 1));
                consumed = 1;
            }
            offset += consumed;
        }
        return result;
    }
#endif
    return std::string(bytes);
}

uint32_t NativeDefaultEncodingCharacter(std::string_view bytes) {
    if (bytes.empty()) return 0;
    const auto first = static_cast<unsigned char>(bytes.front());
    if (first <= 0x7f) return first;
#ifdef __WXMSW__
    const auto page = GetACP();
    if (page != CP_UTF8) {
        // Same character-width rule as the pinned Document::GetCharacterAndWidth.
        const int width = bytes.size() > 1 && Scintilla::Internal::DBCSIsLeadByte(page, first) &&
                                  Scintilla::Internal::DBCSIsTrailByte(page, static_cast<unsigned char>(bytes[1]))
                              ? 2
                              : 1;
        wchar_t wide[2]{};
        MultiByteToWideChar(page, 0, bytes.data(), width, wide, 2);
        if (wide[0] >= 0xd800 && wide[0] <= 0xdbff && wide[1] >= 0xdc00 && wide[1] <= 0xdfff)
            return 0x10000 + ((static_cast<uint32_t>(wide[0]) - 0xd800) << 10) + wide[1] - 0xdc00;
        return static_cast<uint32_t>(wide[0]);
    }
#endif
    const auto status = Scintilla::Internal::UTF8Classify(bytes);
    if (status & Scintilla::Internal::UTF8MaskInvalid) return 0xdc80 + first;
    return static_cast<uint32_t>(
        Scintilla::Internal::UnicodeFromUTF8(reinterpret_cast<const unsigned char*>(bytes.data())));
}

std::string NativeDefaultEncodingUnicode(std::string_view bytes) {
#ifdef __WXMSW__
    if (GetACP() != CP_UTF8) return *Convert(bytes, CP_ACP, CP_UTF8, 0);
#endif
    return std::string(bytes);
}
