#pragma once

#ifdef __WXMSW__
#include <windows.h>
#include <shlwapi.h>
#include <wx/stdpaths.h>
#include <stdexcept>

// Process-local registry views isolate native ShellExecute association lookup.
// The live user Classes/UserChoice trees are never modified.
class OwnedTargetAssociations final {
    HKEY storage_ = nullptr;
    HKEY classes_ = nullptr;
    HKEY user_ = nullptr;
    std::wstring key_;

    static void Check(LSTATUS result) {
        if (result != ERROR_SUCCESS) throw std::runtime_error("owned registry association fixture must be available");
    }
    void Value(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
        HKEY entry = nullptr;
        Check(RegCreateKeyExW(classes_, key.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &entry, nullptr));
        const auto result = RegSetValueExW(entry, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                           static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(entry);
        Check(result);
    }

public:
    OwnedTargetAssociations() {
        try {
            key_ = L"Software\\wxnotepad-open-targets-" + std::to_wstring(GetCurrentProcessId());
            Check(RegCreateKeyExW(HKEY_CURRENT_USER, key_.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &storage_,
                                  nullptr));
            Check(RegCreateKeyExW(storage_, L"Classes", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &classes_, nullptr));
            Check(RegCreateKeyExW(storage_, L"User", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &user_, nullptr));
            const std::wstring executable = wxStandardPaths::Get().GetExecutablePath().ToStdWstring();
            const auto command = L"\"" + executable + L"\" --desktop-target \"%1\"";
            for (const wchar_t* scheme : {L"http", L"https", L"mailto", L"wx242"}) {
                Value(scheme, nullptr, L"URL:owned target recorder");
                Value(scheme, L"URL Protocol", L"");
                Value(std::wstring(scheme) + L"\\shell\\open\\command", nullptr, command);
            }
            Value(L".wx242", nullptr, L"wx242file");
            Value(L"wx242file\\shell\\open\\command", nullptr, command);
            Check(RegOverridePredefKey(HKEY_CURRENT_USER, user_));
            Check(RegOverridePredefKey(HKEY_CLASSES_ROOT, classes_));
            for (const wchar_t* scheme : {L"http", L"https", L"mailto", L"wx242"}) {
                wchar_t selected[32768]{};
                DWORD length = std::size(selected);
                const auto result =
                    AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_COMMAND, scheme, L"open", selected, &length);
                if (FAILED(result) || std::wstring(selected).find(executable) == std::wstring::npos)
                    throw std::runtime_error(
                        "native protocol lookup must resolve the owned recorder before any launch");
            }
        } catch (...) {
            Restore();
            throw;
        }
    }
    OwnedTargetAssociations(const OwnedTargetAssociations&) = delete;
    OwnedTargetAssociations& operator=(const OwnedTargetAssociations&) = delete;
    ~OwnedTargetAssociations() { Restore(); }

private:
    void Restore() {
        RegOverridePredefKey(HKEY_CLASSES_ROOT, nullptr);
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        if (user_) RegCloseKey(user_);
        if (classes_) RegCloseKey(classes_);
        if (storage_) RegCloseKey(storage_);
        RegDeleteTreeW(HKEY_CURRENT_USER, key_.c_str());
    }
};
#endif
