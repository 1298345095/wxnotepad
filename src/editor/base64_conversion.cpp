#include "editor/editor_control.h"
#include <cstdint>
#include <string>
#include <string_view>
#ifdef __WXMSW__
#include <windows.h>
#include <shlwapi.h>
#endif

// Adapted from Notepad4 89b8b36 Helpers.cpp::Base64Encode and
// Helpers.cpp::Base64Decode and Edit.cpp::EditBase64Encode/EditBase64Decode/
// EditReplaceMainSelection, including the original Hex formatting loop.
// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace {
std::string Encode(std::string_view input, bool urlSafe) {
    char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (urlSafe) {
        table[62] = '-';
        table[63] = '_';
    }
    std::string output;
    output.reserve((input.size() + 2) / 3 * 4);
    size_t index = 0;
    while (index + 3 <= input.size()) {
        const auto first = static_cast<uint8_t>(input[index++]);
        const auto second = static_cast<uint8_t>(input[index++]);
        const auto third = static_cast<uint8_t>(input[index++]);
        output += table[first >> 2];
        output += table[((first & 3) << 4) | (second >> 4)];
        output += table[((second & 15) << 2) | (third >> 6)];
        output += table[third & 63];
    }
    if (index < input.size()) {
        const auto first = static_cast<uint8_t>(input[index++]);
        const auto second = index < input.size() ? static_cast<uint8_t>(input[index]) : uint8_t{0};
        output += table[first >> 2];
        output += table[((first & 3) << 4) | (second >> 4)];
        output += index < input.size() ? table[(second & 15) << 2] : '=';
        output += '=';
    }
    return output;
}
std::string Decode(std::string_view input) {
    // Original generated decoding table accepts both Standard and URL Safe.
    static constexpr uint8_t table[128] = {
        128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
        128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 62,
        128, 62,  128, 63,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  128, 128, 128, 128, 128, 128, 128, 0,
        1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,
        23,  24,  25,  128, 128, 128, 128, 63,  128, 26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,
        39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  128, 128, 128, 128, 128,
    };
    uint32_t value = 0;
    std::string output;
    size_t index = 0;
    while (index < input.size()) {
        const auto byte = static_cast<uint8_t>(input[index]);
        if (byte >= std::size(table) || table[byte] == 128) break;
        value = (value << 6) | table[byte];
        if ((++index & 3) == 0) {
            output += static_cast<char>(value >> 16);
            output += static_cast<char>(value >> 8);
            output += static_cast<char>(value);
            value = 0;
        }
    }
    index &= 3;
    if (index == 3) {
        value >>= 2;
        output += static_cast<char>(value >> 8);
        output += static_cast<char>(value);
    } else if (index != 0) {
        // The original deliberately emits a byte for one leftover sextet too.
        output += static_cast<char>(value >> 4);
    }
    return output;
}
std::string Hex(std::string_view bytes, Scintilla::EndOfLine eol) {
    constexpr char digits[] = "0123456789ABCDEF";
    constexpr size_t bytesPerLine = 16;
    std::string output;
    for (size_t index = 0; index < bytes.size(); ++index) {
        const auto byte = static_cast<uint8_t>(bytes[index]);
        output += digits[byte >> 4];
        output += digits[byte & 15];
        if ((index + 1) % bytesPerLine == 0) {
            if (eol != Scintilla::EndOfLine::Lf) output += '\r';
            if (eol != Scintilla::EndOfLine::Cr) output += '\n';
        } else if (index + 1 != bytes.size()) {
            output += ' ';
        }
    }
    return output;
}
std::string ImageExtension(const wxString& path) {
    const auto wide = path.ToStdWstring();
#ifdef __WXMSW__
    const wchar_t* suffix = PathFindExtensionW(wide.c_str());
    if (*suffix == L'.') ++suffix;
#else
    // PathFindExtensionW: a separator or a space discards an earlier dot.
    size_t extension = wide.size();
    for (size_t index = 0; index < wide.size(); ++index) {
        if (wide[index] == L'/' || wide[index] == L'\\' || wide[index] == L' ')
            extension = wide.size();
        else if (wide[index] == L'.')
            extension = index + 1;
    }
    const wchar_t* suffix = wide.c_str() + extension;
#endif
    std::string result;
    for (; *suffix; ++suffix)
        result += static_cast<char>(*suffix >= L'A' && *suffix <= L'Z' ? *suffix + (L'a' - L'A') : *suffix);
    return result;
}
std::string NativeRepresentation(std::string_view input, bool toNative) {
#ifdef __WXMSW__
    if (input.empty()) return {};
    const UINT source = toNative ? CP_UTF8 : CP_ACP;
    const UINT target = toNative ? CP_ACP : CP_UTF8;
    const int length = static_cast<int>(input.size());
    std::wstring wide(MultiByteToWideChar(source, 0, input.data(), length, nullptr, 0), L'\0');
    MultiByteToWideChar(source, 0, input.data(), length, wide.data(), static_cast<int>(wide.size()));
    const int units = static_cast<int>(wide.size());
    std::string output(WideCharToMultiByte(target, 0, wide.data(), units, nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(target, 0, wide.data(), units, output.data(), static_cast<int>(output.size()), nullptr,
                        nullptr);
    return output;
#else
    // As for NativeDefaultEncodingText, the Linux editor's native boundary is UTF-8.
    (void)toNative;
    return std::string(input);
#endif
}
} // namespace

bool EditorControl::ConvertBase64(Base64Conversion command, const wxString& path, bool defaultEncoding) {
    using Scintilla::Message;
    const auto length = Send(Message::GetSelText);
    if (!length) return true;
    if (Send(Message::SelectionIsRectangle)) return false;
    std::string input(static_cast<size_t>(length) + 1, '\0');
    const bool encode = command == Base64Conversion::Encode || command == Base64Conversion::UrlSafeEncode ||
                        command == Base64Conversion::HtmlEmbeddedImage;
    Send(Message::GetSelText, encode, reinterpret_cast<intptr_t>(input.data()));
    input.resize(static_cast<size_t>(length));
    if (defaultEncoding) input = NativeRepresentation(input, true);
    std::string output;
    if (encode) {
        output = Encode(input, command == Base64Conversion::UrlSafeEncode);
        if (command == Base64Conversion::HtmlEmbeddedImage)
            output = "<img src=\"data:image/" + ImageExtension(path) + ";base64," + output + "\" />";
    } else {
        output = Decode(input);
        if (output.empty()) return true;
        if (command == Base64Conversion::DecodeAsHex)
            output = Hex(output, static_cast<Scintilla::EndOfLine>(Send(Message::GetEOLMode)));
        else if (defaultEncoding)
            output = NativeRepresentation(output, false);
    }
    const auto start = Send(Message::GetSelectionStart);
    const auto end = Send(Message::GetSelectionEnd);
    const bool backwards = Send(Message::GetAnchor) > Send(Message::GetCurrentPos);
    Send(Message::SetTargetStart, start);
    Send(Message::SetTargetEnd, end);
    Send(Message::ReplaceTarget, output.size(), reinterpret_cast<intptr_t>(output.data()));
    const auto outputEnd = start + static_cast<intptr_t>(output.size());
    Send(Message::SetSel, backwards ? outputEnd : start, backwards ? start : outputEnd);
    return true;
}
