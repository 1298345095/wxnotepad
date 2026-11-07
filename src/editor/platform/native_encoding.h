#pragma once

#include <string>
#include <wx/string.h>
#include <wx/strconv.h>
#ifdef __WXMSW__
#include <windows.h>
#endif

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
