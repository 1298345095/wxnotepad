// Substitute only the two OS locale queries, as in the existing SPI contrast seam.
#include <windows.h>
#include <string>
namespace {
std::wstring userLocale = L"en-US", systemLocale = L"en-US";
}
void SetNativeStyleLocaleDefaults(const wchar_t* user, const wchar_t* system) {
    userLocale = user;
    systemLocale = system;
}
int WINAPI ControlledUserLocale(LPWSTR result, int capacity) {
    return wcscpy_s(result, capacity, userLocale.c_str()) == 0 ? static_cast<int>(userLocale.size() + 1) : 0;
}
int WINAPI ControlledSystemLocale(LPWSTR result, int capacity) {
    return wcscpy_s(result, capacity, systemLocale.c_str()) == 0 ? static_cast<int>(systemLocale.size() + 1) : 0;
}
#define GetUserDefaultLocaleName ControlledUserLocale
#define GetSystemDefaultLocaleName ControlledSystemLocale
#include "editor/font_settings.cpp"
