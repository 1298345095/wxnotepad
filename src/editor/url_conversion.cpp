#include "editor/editor_control.h"
#include <algorithm>
#include <string_view>
#include <vector>
#ifdef __WXMSW__
#include <windows.h>
#include <shlwapi.h>
#endif

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace {
constexpr std::string_view trimSet = " \a\b\f\n\r\t\v";
size_t OriginalAllocation(size_t length) {
    return ((length + 1 + 15) & ~size_t{15}) * 3;
}
#ifdef __WXMSW__
std::string Convert(std::string_view text, UrlConversion command, size_t length) {
    // Keep the original API/count boundary, including its zero-filled result
    // when UrlUnescapeW rejects a decoded NUL (see captured fixtures).
    std::vector<wchar_t> input(text.size() + 1);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), input.data(),
                        static_cast<int>(input.size()));
    DWORD count = static_cast<DWORD>(OriginalAllocation(length) * (command == UrlConversion::Decode ? 1 : 3));
    std::vector<wchar_t> output(count);
    if (command == UrlConversion::Decode) {
        UrlUnescapeW(input.data(), output.data(), &count, URL_UNESCAPE_AS_UTF8);
    } else {
        const DWORD flags = command == UrlConversion::ComponentEncode
                                ? URL_ESCAPE_AS_UTF8 | URL_ESCAPE_ASCII_URI_COMPONENT | URL_ESCAPE_SEGMENT_ONLY
                                : URL_ESCAPE_AS_UTF8;
        UrlEscapeW(input.data(), output.data(), &count, flags);
    }
    std::string result(static_cast<size_t>(count) * 3, '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, output.data(), static_cast<int>(count), result.data(),
                                            static_cast<int>(result.size()), nullptr, nullptr);
    result.resize(static_cast<size_t>(written));
    return result;
}
#else
// The selection getter already maps NUL to space. Match the two original UTF-8
// conversion boundaries: Win32 input conversion and UrlUnescapeW's byte scanner.
std::string ValidUtf8(std::string_view text, bool unescape) {
    std::string result;
    for (size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index]);
        const size_t width = first < 0x80                     ? 1
                             : first >= 0xc2 && first <= 0xdf ? 2
                             : first >= 0xe0 && first <= 0xef ? 3
                             : first >= 0xf0 && first <= 0xf4 ? 4
                                                              : 0;
        size_t consumed = 1;
        bool valid = width != 0;
        while (valid && consumed < width) {
            if (index + consumed == text.size()) {
                valid = false;
                break;
            }
            const auto ch = static_cast<unsigned char>(text[index + consumed]);
            if (ch < 0x80 || ch > 0xbf) {
                valid = false;
                break;
            }
            if (consumed == 1 && ((first == 0xe0 && ch < 0xa0) || (first == 0xed && ch >= 0xa0) ||
                                  (first == 0xf0 && ch < 0x90) || (first == 0xf4 && ch >= 0x90))) {
                if (unescape) ++consumed;
                valid = false;
                break;
            }
            ++consumed;
        }
        if (valid)
            result.append(text.substr(index, consumed));
        else
            result += "\xef\xbf\xbd";
        index += consumed;
    }
    return result;
}
std::string Escape(std::string_view text, bool component, bool slash = false, bool extras = false) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char ch : text) {
        const bool unreserved = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                                std::string_view("-._~").find(static_cast<char>(ch)) != std::string_view::npos;
        const bool unsafe = ch <= ' ' || ch >= 127 ||
                            std::string_view("\"<>&\\^`{}|[]").find(static_cast<char>(ch)) != std::string_view::npos;
        if (component ? !unreserved : unsafe || (slash && ch == '/') || (extras && (ch == '?' || ch == '#'))) {
            result += '%';
            result += hex[ch >> 4];
            result += hex[ch & 15];
        } else {
            result += static_cast<char>(ch);
        }
    }
    return result;
}
bool SchemeCharacter(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '+' || ch == '-' ||
           ch == '.';
}
std::string Encode(std::string text, bool component) {
    if (component) return Escape(text, true);
    const auto colon = text.find(':');
    const auto prefix = std::string_view(text).substr(0, colon);
    size_t prefixLength = 0;
    std::string scheme;
    if (colon != std::string::npos && colon > 1 && std::all_of(prefix.begin(), prefix.end(), SchemeCharacter)) {
        prefixLength = colon + 1;
        scheme = text.substr(0, colon);
        for (auto& ch : scheme)
            if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    }
    std::string result = scheme.empty() ? "" : scheme + ':';
    auto rest = std::string_view(text).substr(prefixLength);
    if (scheme == "mailto" || scheme == "about" || scheme == "javascript" || scheme == "vbscript" || scheme == "shell")
        return result + Escape(rest, false, true, true);
    const auto query = rest.find('?');
    const auto fragment = rest.find('#');
    std::string path(rest.substr(0, std::min(query, fragment)));
    const bool normalize = scheme == "http" || scheme == "https" || scheme == "ftp" || scheme == "file" ||
                           scheme == "news" || scheme == "nntp" || scheme == "gopher" || scheme == "telnet";
    if (normalize) std::replace(path.begin(), path.end(), '\\', '/');
    if (scheme.empty() && !path.empty() && path.front() == '\\') path.front() = '/';
    const bool authority = path.starts_with("//") && (prefixLength || !text.starts_with("\\"));
    const bool drive = scheme == "file" && path.size() >= 4 && path[3] == ':';
    if (drive && authority) {
        result += "///";
        result += Escape(std::string_view(path).substr(2), false);
    } else if (authority) {
        const auto end = path.find('/', 2);
        result += path.substr(0, end);
        result += end == std::string::npos ? "/" : Escape(std::string_view(path).substr(end), false);
    } else {
        const bool slash = !scheme.empty() && scheme != "file" && !path.starts_with('/');
        result += Escape(path, false, slash);
    }
    if (query != std::string_view::npos)
        result += rest.substr(query, fragment > query ? fragment - query : std::string_view::npos);
    if (fragment != std::string_view::npos)
        result += rest.substr(fragment, query > fragment ? query - fragment : std::string_view::npos);
    return result;
}
int HexDigit(unsigned char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
std::string Convert(std::string_view text, UrlConversion command, size_t length) {
    const auto input = ValidUtf8(text, false);
    if (command != UrlConversion::Decode) return Encode(input, command == UrlConversion::ComponentEncode);
    std::string bytes;
    for (size_t index = 0; index < input.size(); ++index) {
        if (input[index] == '%' && index + 2 < input.size()) {
            const int high = HexDigit(input[index + 1]);
            const int low = HexDigit(input[index + 2]);
            if (high >= 0 && low >= 0) {
                const auto ch = static_cast<char>(high * 16 + low);
                if (!ch) return std::string(OriginalAllocation(length), '\0');
                bytes += ch;
                index += 2;
                continue;
            }
        }
        bytes += input[index];
    }
    return ValidUtf8(bytes, true);
}
#endif
} // namespace

bool EditorControl::ConvertUrl(UrlConversion command) {
    using Scintilla::Message;
    // Original 89b8b36 EditURLEncode/EditURLDecode and EditReplaceMainSelection.
    const auto length = Send(Message::GetSelText);
    if (!length) return true;
    if (Send(Message::SelectionIsRectangle)) return false;
    std::string text(static_cast<size_t>(length) + 1, '\0');
    Send(Message::GetSelText, 0, reinterpret_cast<intptr_t>(text.data()));
    text.resize(static_cast<size_t>(length));
    auto input = std::string_view(text);
    if (command != UrlConversion::Decode) {
        const auto first = input.find_first_not_of(trimSet);
        if (first == std::string_view::npos) return true;
        input = input.substr(first, input.find_last_not_of(trimSet) - first + 1);
    }
    const auto output = Convert(input, command, static_cast<size_t>(length));
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
