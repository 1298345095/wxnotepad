#pragma once
#include <windows.h>
#include <vector>

struct NativeFontSelection {
    DWORD thread;
    HDC dc;
    HFONT font;
    LOGFONTW description;
};
void BeginNativeFontObservation();
std::vector<NativeFontSelection> EndNativeFontObservation();
