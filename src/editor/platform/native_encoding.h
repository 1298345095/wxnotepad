#pragma once

#include <optional>

#include <string>
#include <string_view>
#include "editor/native_byte_history.h"
#include <wx/string.h>
#include <wx/strconv.h>
#ifdef __WXMSW__
#include <windows.h>
#endif

// The migrated core stores UTF-8, retaining undecodable native bytes as raw
// invalid UTF-8 bytes. These two boundaries preserve the original ANSI buffer.
std::string NativeDefaultEncodingBytes(std::string_view text);
std::string NativeDefaultEncodingBuffer(std::string_view bytes, NativeByteRanges& raw,
                                        std::optional<unsigned> codePage = std::nullopt);
uint32_t NativeDefaultEncodingCharacter(std::string_view bytes);
// Original EditConvertText(CP_ACP, CP_UTF8) boundary after an accepted encoding change.
std::string NativeDefaultEncodingUnicode(std::string_view bytes);

inline wxString NativeDefaultEncodingText(const wxString& text, size_t* nativeLength = nullptr) {
#ifdef __WXMSW__
    // Original WideCharToMultiByte(cpEdit, 0) insertion boundary. The migrated
    // core keeps UTF-8, including when the document uses system-default ANSI.
    if (!text.empty()) {
        const auto wide = text.ToStdWstring();
        const int size =
            WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string legacy(size, '\0');
        WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()), legacy.data(), size, nullptr,
                            nullptr);
        if (nativeLength) *nativeLength = legacy.size();
        return wxString(legacy.data(), wxConvLocal, legacy.size());
    }
#endif
    if (nativeLength) *nativeLength = text.ToStdString(wxConvUTF8).size();
    return text;
}
