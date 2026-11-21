// Independent one-call shell controls. The original helper is extracted verbatim
// from the pinned Git object without changing its bytes.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>
#include <shldisp.h>
#include <shlguid.h>
#include <wrl/client.h>
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
using Microsoft::WRL::ComPtr;
std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
void PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
bool IsReadyFolder(IWebBrowser2& browser, const std::filesystem::path& directory) {
    READYSTATE state{};
    VARIANT_BOOL busy = VARIANT_TRUE;
    if (FAILED(browser.get_ReadyState(&state)) || state != READYSTATE_COMPLETE || FAILED(browser.get_Busy(&busy)) ||
        busy != VARIANT_FALSE)
        return false;
    ComPtr<IDispatch> document;
    ComPtr<IShellFolderViewDual> view;
    ComPtr<Folder> folder;
    ComPtr<Folder2> folderWithSelf;
    ComPtr<FolderItem> self;
    if (FAILED(browser.get_Document(&document)) || !document || FAILED(document.As(&view)) ||
        FAILED(view->get_Folder(&folder)) || FAILED(folder.As(&folderWithSelf)) ||
        FAILED(folderWithSelf->get_Self(&self)))
        return false;
    BSTR path = nullptr;
    if (FAILED(self->get_Path(&path))) return false;
    const bool matches = path && _wcsicmp(path, directory.c_str()) == 0;
    SysFreeString(path);
    if (!matches) return false;
    ComPtr<IServiceProvider> services;
    ComPtr<IShellBrowser> shellBrowser;
    ComPtr<IShellView> shellView;
    ComPtr<IFolderView2> nativeView;
    DWORD flags = 0;
    HRESULT result = browser.QueryInterface(IID_PPV_ARGS(&services));
    if (SUCCEEDED(result)) result = services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&shellBrowser));
    if (SUCCEEDED(result)) result = shellBrowser->QueryActiveShellView(&shellView);
    if (SUCCEEDED(result)) result = shellView.As(&nativeView);
    if (SUCCEEDED(result)) result = nativeView->GetCurrentFolderFlags(&flags);
    std::printf("Independent prepared folder ready=4 busy=0 flags_hresult=%08lx folder_flags=%08lx\n",
                static_cast<unsigned long>(result), flags);
    return true;
}
bool PrepareReadyFolder(const std::filesystem::path& directory) {
    auto* pidl = ILCreateFromPathW(directory.c_str());
    if (!pidl) return false;
    SHELLEXECUTEINFOW request{sizeof(request)};
    request.fMask = SEE_MASK_IDLIST;
    request.lpVerb = L"open";
    request.lpIDList = pidl;
    request.nShow = SW_SHOW;
    const bool opened = ShellExecuteExW(&request) != FALSE;
    CoTaskMemFree(pidl);
    if (!opened) return false;
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        long count = 0;
        if (SUCCEEDED(windows->get_Count(&count))) {
            for (long index = 0; index < count; ++index) {
                VARIANT position{};
                position.vt = VT_I4;
                position.lVal = index;
                ComPtr<IDispatch> dispatch;
                ComPtr<IWebBrowser2> browser;
                if (SUCCEEDED(windows->Item(position, &dispatch)) && dispatch && SUCCEEDED(dispatch.As(&browser)) &&
                    IsReadyFolder(*browser.Get(), directory))
                    return true;
            }
        }
        PumpMessages();
        Sleep(10);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
bool StartExplorerSelection(const std::filesystem::path& item) {
    wchar_t windowsDirectory[MAX_PATH]{};
    const auto length = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;
    const auto explorer = std::filesystem::path(windowsDirectory) / L"explorer.exe";
    // Pinned Helpers.cpp's disabled native fallback owns this Explorer-specific
    // grammar: quote the target after /select, rather than quote a whole argv.
    const auto parameters = L"/select,\"" + item.wstring() + L"\"";
    const auto directory = item.parent_path();
    SHELLEXECUTEINFOW request{sizeof(request)};
    request.fMask = SEE_MASK_FLAG_NO_UI;
    request.lpVerb = L"open";
    request.lpFile = explorer.c_str();
    request.lpParameters = parameters.c_str();
    request.lpDirectory = directory.c_str();
    request.nShow = SW_SHOWNORMAL;
    const bool started = ShellExecuteExW(&request) != FALSE;
    std::printf("Independent Explorer startup launched=%d last_error=%lu\n", started, GetLastError());
    return started;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const std::filesystem::path root = argv[1];
    const std::wstring mode = argv[2];
    if (mode != L"original" && mode != L"single-item" && mode != L"single-item-ready" && mode != L"explorer-startup")
        return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 3;
    const auto directory = root / "documents";
    const auto item = directory / "parent.txt";
    const bool prepared = mode != L"single-item-ready" || PrepareReadyFolder(directory);
    std::ofstream(root / "folder-request-1", std::ios::binary) << Utf8(directory) << '\n'
                                                               << Utf8(item) << '\n'
                                                               << GetCurrentProcessId() << '\n';
    std::printf("Independent shell control mode=%ls pid=%lu target=%s\n", mode.c_str(), GetCurrentProcessId(),
                Utf8(item).c_str());
    // Also publish a failed preparation so the observer captures and closes
    // any newly created owned view, without issuing a selection request.
    if (!prepared) {
        std::fputs("FAIL: independent folder must be ready before its one selection request\n", stderr);
        CoUninitialize();
        return 4;
    }
    if (mode == L"original") {
        OpenContainingFolder(nullptr, item.c_str(), true);
    } else if (mode == L"explorer-startup") {
        StartExplorerSelection(item);
    } else {
        auto* pidl = ILCreateFromPathW(item.c_str());
        const auto result = pidl ? SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0) : E_FAIL;
        std::printf("Independent single-item HRESULT=%08lx\n", static_cast<unsigned long>(result));
        CoTaskMemFree(pidl);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    const auto done = root / "folder-done-1";
    while (!std::filesystem::exists(done) && std::chrono::steady_clock::now() < deadline) {
        PumpMessages();
        Sleep(10);
    }
    std::ifstream response(done, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(response), {}};
    const bool selected = bytes == "ok\n";
    std::printf("%s: independent %ls exact Explorer selection\n", selected ? "PASS" : "FAIL", mode.c_str());
    CoUninitialize();
    return selected ? 0 : 1;
}
