// Independent one-call shell controls. The original helper is extracted verbatim
// from the pinned Git object without changing its bytes.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#define COUNTOF(array) std::size(array)
#include "pinned-open-folder.inc"
#undef COUNTOF

namespace {
std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const std::filesystem::path root = argv[1];
    const std::wstring mode = argv[2];
    if (mode != L"original" && mode != L"single-item") return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 3;
    const auto directory = root / "documents";
    const auto item = directory / "parent.txt";
    std::ofstream(root / "folder-request-1", std::ios::binary) << Utf8(directory) << '\n'
                                                               << Utf8(item) << '\n'
                                                               << GetCurrentProcessId() << '\n';
    std::printf("Independent shell control mode=%ls pid=%lu target=%s\n", mode.c_str(), GetCurrentProcessId(),
                Utf8(item).c_str());
    if (mode == L"original") {
        OpenContainingFolder(nullptr, item.c_str(), true);
    } else {
        auto* pidl = ILCreateFromPathW(item.c_str());
        const auto result = pidl ? SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0) : E_FAIL;
        std::printf("Independent single-item HRESULT=%08lx\n", static_cast<unsigned long>(result));
        CoTaskMemFree(pidl);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    const auto done = root / "folder-done-1";
    while (!std::filesystem::exists(done) && std::chrono::steady_clock::now() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    std::ifstream response(done, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(response), {}};
    const bool selected = bytes == "ok\n";
    std::printf("%s: independent %ls exact Explorer selection\n", selected ? "PASS" : "FAIL", mode.c_str());
    CoUninitialize();
    return selected ? 0 : 1;
}
