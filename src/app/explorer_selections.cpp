#include "app/explorer_selections.h"
#include "app/desktop_launch.h"
#include "diagnostics/trace.h"
#include <wx/window.h>
#include <wx/weakref.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/scopeguard.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shldisp.h>
#include <wrl/client.h>
#include <chrono>
#include <filesystem>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
constexpr auto FolderReadyTimeout = std::chrono::seconds(10);
constexpr int FolderReadyPollMilliseconds = 10;

bool IsReadyFolder(IWebBrowser2& browser, const std::filesystem::path& expected) {
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
    // Explorer expands short DOS names; compare native filesystem identity.
    std::error_code error;
    const bool matches = path && std::filesystem::equivalent(std::filesystem::path(path), expected, error);
    SysFreeString(path);
    return matches;
}

bool HasReadyFolder(IShellWindows& windows, const wxString& directory) {
    long count = 0;
    if (FAILED(windows.get_Count(&count))) return false;
    const std::filesystem::path expected(directory.ToStdWstring());
    for (long index = 0; index < count; ++index) {
        VARIANT position{};
        position.vt = VT_I4;
        position.lVal = index;
        ComPtr<IDispatch> dispatch;
        ComPtr<IWebBrowser2> browser;
        if (SUCCEEDED(windows.Item(position, &dispatch)) && dispatch && SUCCEEDED(dispatch.As(&browser)) &&
            IsReadyFolder(*browser.Get(), expected))
            return true;
    }
    return false;
}

bool SelectItem(const wxString& path) {
    auto* item = ILCreateFromPathW(path.wc_str());
    const HRESULT result = item ? SHOpenFolderAndSelectItems(item, 0, nullptr, 0) : E_FAIL;
#ifdef WXNOTEPAD_DIAGNOSTICS
    const auto nativeError = GetLastError();
    if (DiagnosticTrace::Enabled()) {
        constexpr size_t NativePathCharacters = 32768;
        wchar_t nativePath[NativePathCharacters]{};
        const bool decoded = item && SHGetPathFromIDListEx(item, nativePath, NativePathCharacters, GPFIDL_DEFAULT);
        DiagnosticTrace::Write(
            "shell.select-item",
            "\texpected=%s\tnative=%s\tdecoded=%d\tpidl_bytes=%u\tcidl=0\tflags=0\thresult=%08lx\tlast_error=%lu",
            path.utf8_str().data(), wxString(nativePath).utf8_str().data(), decoded, item ? ILGetSize(item) : 0,
            static_cast<unsigned long>(result), nativeError);
    }
    SetLastError(nativeError);
#endif
    CoTaskMemFree(item);
    return SUCCEEDED(result);
}
} // namespace

struct ExplorerSelections::State {
    struct Request {
        wxString path;
        wxString directory;
        std::chrono::steady_clock::time_point deadline;
    };
    ComPtr<IShellWindows> windows;
    std::vector<Request> pending;
};

ExplorerSelections::ExplorerSelections(wxWindow& owner) : state_(std::make_unique<State>()), owner_(owner) {
    Bind(wxEVT_TIMER, &ExplorerSelections::Poll, this);
}

ExplorerSelections::~ExplorerSelections() {
    Close();
}

void ExplorerSelections::Cancel() {
    timer_.Stop();
    state_->pending.clear();
    state_->windows.Reset();
}

void ExplorerSelections::Close() {
    closing_ = true;
    Cancel();
}

bool ExplorerSelections::Start(const wxString& path) {
    if (closing_) return false;
    auto windows = state_->windows;
    if (!windows && FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows))))
        return false;
    if (closing_) return true;
    const auto directory = wxFileName(path).GetPath();
    auto* folder = ILCreateFromPathW(directory.wc_str());
    if (!folder) return false;
    SHELLEXECUTEINFOW request{sizeof(request)};
    request.fMask = SEE_MASK_IDLIST | SEE_MASK_FLAG_NO_UI;
    request.hwnd = static_cast<HWND>(owner_.GetHandle());
    request.lpVerb = L"open";
    request.lpIDList = folder;
    request.nShow = SW_SHOW;
    const bool opened = ShellExecuteExW(&request) != FALSE;
    CoTaskMemFree(folder);
    if (closing_) return true;
    if (!opened) return false;
    state_->pending.push_back({path, directory, std::chrono::steady_clock::now() + FolderReadyTimeout});
    state_->windows = std::move(windows);
    if (!polling_ && !timer_.IsRunning() && !timer_.StartOnce(FolderReadyPollMilliseconds)) {
        state_->pending.pop_back();
        if (state_->pending.empty()) state_->windows.Reset();
        return false;
    }
    return true;
}

void ExplorerSelections::QueueFallback(const wxString& directory) {
    owner_.CallAfter([request = wxWeakRef<ExplorerSelections>(this), owner = wxWeakRef<wxWindow>(&owner_), directory] {
        if (!request || request->closing_ || !owner || owner->IsBeingDeleted()) return;
        wxString error;
        if (!LaunchDesktopTarget(owner.get(), directory, directory, error))
            wxMessageBox(_("Could not open the selected target."), _("Could not open target"), wxOK | wxICON_ERROR,
                         owner.get());
    });
}

void ExplorerSelections::Poll(wxTimerEvent&) {
    if (polling_) return;
    polling_ = true;
    wxON_BLOCK_EXIT_SET(polling_, false);
    if (closing_ || owner_.IsBeingDeleted()) {
        Cancel();
        return;
    }
    // Native STA calls can dispatch sent messages. Keep the in-flight COM
    // reference and request stable if another command appends or closes us.
    const auto windows = state_->windows;
    for (size_t index = 0; index < state_->pending.size();) {
        const auto request = state_->pending[index];
        const bool ready = HasReadyFolder(*windows.Get(), request.directory);
        if (closing_) return;
        if (!ready && std::chrono::steady_clock::now() < request.deadline) {
            ++index;
            continue;
        }
        const bool selected = ready && SelectItem(request.path);
        if (closing_) return;
        if (!selected) QueueFallback(request.directory);
        state_->pending.erase(state_->pending.begin() + static_cast<ptrdiff_t>(index));
    }
    if (state_->pending.empty()) {
        Cancel();
    } else if (!timer_.StartOnce(FolderReadyPollMilliseconds)) {
        const auto pending = std::move(state_->pending);
        Cancel();
        for (const auto& request : pending)
            QueueFallback(request.directory);
    }
}
