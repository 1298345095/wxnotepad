#pragma once

#include <windows.h>
#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace ExplorerAcknowledgement {
inline std::runtime_error Failure(const std::filesystem::path& path, const char* operation, DWORD error) {
    const auto utf8 = path.u8string();
    return std::runtime_error(std::string("Explorer acknowledgement ") + operation + " failed (Win32 " +
                              std::to_string(error) + "): " + std::string(utf8.begin(), utf8.end()));
}

inline std::string Read(const std::filesystem::path& path) {
    // Rename can publish the name before its DELETE-access handle closes.
    // Share DELETE for this immutable marker; ordinary saved-file reads keep
    // their existing TestFiles::Bytes semantics.
    const auto raw = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw == INVALID_HANDLE_VALUE) throw Failure(path, "open", GetLastError());
    const std::unique_ptr<void, decltype(&CloseHandle)> handle(raw, &CloseHandle);
    std::array<char, 64> buffer{};
    std::string bytes;
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(handle.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
            throw Failure(path, "read", GetLastError());
        if (count == 0) return bytes;
        bytes.append(buffer.data(), count);
    }
}
} // namespace ExplorerAcknowledgement
