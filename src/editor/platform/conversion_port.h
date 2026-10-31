// Portability support for the pinned Notepad4 core on Linux.
#pragma once
#include <wx/strconv.h>
#include <wx/fontenc.h>
inline int MultiByteToWideChar(unsigned codepage, unsigned, const char* input, int length, wchar_t* output,
                               int capacity) {
    wxCSConv codec(codepage == 65001 ? wxFONTENCODING_UTF8
                   : codepage == 936 ? wxFONTENCODING_CP936
                                     : wxFONTENCODING_ISO8859_1);
    const auto count = codec.ToWChar(output, output ? capacity : 0, input, length);
    return count == wxCONV_FAILED ? 0 : static_cast<int>(count);
}
