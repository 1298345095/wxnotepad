#pragma once

#ifdef __WXMSW__
#include <windows.h>
#include <shlwapi.h>
#include <wx/stdpaths.h>
#include <stdexcept>

// Shell dispatch can resolve associations outside this process. Register only
// fresh owned names so the real shell can reach the recorder without changing
// any existing protocol, extension or protected UserChoice association.
class OwnedTargetAssociations final {
    HKEY protocol_ = nullptr;
    HKEY extension_ = nullptr;
    std::wstring scheme_;
    std::wstring protocolKey_;
    std::wstring extensionKey_;
    std::wstring command_;
    bool protocolCreated_ = false;
    bool extensionCreated_ = false;

    static std::wstring RecorderCommand() {
        const std::wstring executable = wxStandardPaths::Get().GetExecutablePath().ToStdWstring();
        wxString recorderDirectory;
        if (!wxGetEnv("WXNOTEPAD_OPEN_TARGET_ROOT", &recorderDirectory))
            throw std::runtime_error("owned recorder directory must be configured");
        return L"\"" + executable + L"\" --desktop-target \"%1\" \"" + recorderDirectory.ToStdWstring() + L"\"";
    }
    static void Check(LSTATUS result, const char* operation) {
        if (result != ERROR_SUCCESS)
            throw std::runtime_error(std::string(operation) + " failed with Windows error " + std::to_string(result));
    }
    static void Create(const std::wstring& key, HKEY& handle, bool& created) {
        DWORD disposition = 0;
        Check(RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &handle,
                              &disposition),
              "create fresh owned association");
        created = disposition == REG_CREATED_NEW_KEY;
        if (!created) throw std::runtime_error("owned association name must not already exist");
    }
    static void Value(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& value) {
        HKEY entry = nullptr;
        Check(RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &entry, nullptr),
              "create association command");
        const auto result = RegSetValueExW(entry, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                           static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(entry);
        Check(result, "set association value");
    }

public:
    OwnedTargetAssociations() {
        try {
            scheme_ = L"wxnotepadopentarget" + std::to_wstring(GetCurrentProcessId()) + L"x" +
                      std::to_wstring(GetTickCount64());
            protocolKey_ = LR"(Software\Classes\)" + scheme_;
            extensionKey_ = LR"(Software\Classes\.)" + scheme_;
            Create(protocolKey_, protocol_, protocolCreated_);
            Create(extensionKey_, extension_, extensionCreated_);
            const std::wstring executable = wxStandardPaths::Get().GetExecutablePath().ToStdWstring();
            command_ = RecorderCommand();
            Value(protocol_, L"", nullptr, L"URL:owned target recorder");
            Value(protocol_, L"", L"URL Protocol", L"");
            Value(protocol_, LR"(shell\open\command)", nullptr, command_);
            Value(extension_, L"", nullptr, scheme_);
            for (const auto& association : {scheme_, L"." + scheme_}) {
                wchar_t selected[32768]{};
                DWORD length = std::size(selected);
                const auto result =
                    AssocQueryStringW(association == scheme_ ? ASSOCF_IS_PROTOCOL : ASSOCF_NONE, ASSOCSTR_COMMAND,
                                      association.c_str(), L"open", selected, &length);
                if (FAILED(result) || std::wstring(selected).find(executable) == std::wstring::npos)
                    throw std::runtime_error("native association lookup must resolve the fresh owned recorder");
            }
        } catch (...) {
            Remove();
            throw;
        }
    }
    OwnedTargetAssociations(const OwnedTargetAssociations&) = delete;
    OwnedTargetAssociations& operator=(const OwnedTargetAssociations&) = delete;
    ~OwnedTargetAssociations() { Remove(); }
    wxString Scheme() const { return wxString(scheme_); }
    wxString Extension() const { return "." + Scheme(); }
    static void RequireMailHandler() {
        wchar_t selected[32768]{};
        DWORD length = std::size(selected);
        const auto result =
            AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_COMMAND, L"mailto", L"open", selected, &length);
        if (FAILED(result) || std::wstring(selected) != RecorderCommand())
            throw std::runtime_error("mailto must already resolve to this exact owned recorder");
    }
    void RequireStandardHandlers() const {
        for (const auto* scheme : {L"http", L"https", L"mailto"}) {
            wchar_t selected[32768]{};
            DWORD length = std::size(selected);
            const auto result =
                AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_COMMAND, scheme, L"open", selected, &length);
            if (FAILED(result) || std::wstring(selected) != command_)
                throw std::runtime_error("standard schemes must already resolve to this exact owned recorder");
        }
    }
    bool Remove() {
        if (protocol_) RegCloseKey(protocol_);
        if (extension_) RegCloseKey(extension_);
        protocol_ = extension_ = nullptr;
        bool removed = true;
        if (extensionCreated_) {
            removed = RegDeleteTreeW(HKEY_CURRENT_USER, extensionKey_.c_str()) == ERROR_SUCCESS;
            extensionCreated_ = !removed;
        }
        if (protocolCreated_) {
            const bool protocolRemoved = RegDeleteTreeW(HKEY_CURRENT_USER, protocolKey_.c_str()) == ERROR_SUCCESS;
            protocolCreated_ = !protocolRemoved;
            removed = removed && protocolRemoved;
        }
        return removed;
    }
};
#endif
