// Host the untouched original selection getter/action and first-argument helper.
// The ShellExecuteEx boundary records requests so default providers cannot open a browser.
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <cstdint>
#include <ScintillaTypes.h>
#include <ScintillaMessages.h>
#include <Sci_Position.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

HANDLE g_hDefaultHeap = GetProcessHeap();
UINT g_uSystemDPI = USER_DEFAULT_SCREEN_DPI;
unsigned int dwUrlThreshold = 0;
WCHAR defaultTextFontName[LF_FACESIZE] = L"Consolas";
char* EditMapTextCase(int, const char*, size_t&, UINT) noexcept {
    std::abort();
}
extern "C" int Scintilla_RegisterClasses(void*);
extern "C" int Scintilla_ReleaseResources();

namespace {
using Scintilla::Message;
HWND editor;
WCHAR szIniFile[MAX_PATH * 2]{};
WCHAR szCurFile[MAX_PATH * 2]{};
int language;
std::string caseName;
std::ofstream output;
int launchCount;
intptr_t Send(Message message, uintptr_t first = 0, intptr_t second = 0) {
    return SendMessageW(editor, static_cast<UINT>(message), first, second);
}
std::string Hex(const wchar_t* value) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, bytes.data(), length, nullptr, nullptr);
    std::string hex;
    constexpr char digits[] = "0123456789abcdef";
    for (size_t index = 0; index + 1 < bytes.size(); ++index) {
        const auto byte = static_cast<unsigned char>(bytes[index]);
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    return hex.empty() ? "-" : hex;
}
BOOL RecordLaunch(SHELLEXECUTEINFO* request) {
    ++launchCount;
    output << caseName << '\t' << Hex(request->lpFile) << '\t' << Hex(request->lpParameters) << '\t'
           << Hex(request->lpDirectory) << '\n';
    return TRUE;
}
#include "defaults.inc"
int GetString(int id, wchar_t* value, size_t count) {
    lstrcpynW(value, defaults[language][id], static_cast<int>(count));
    return lstrlenW(value);
}
void TrimString(wchar_t* value) {
    StrTrimW(value, L" ");
}
constexpr int CMD_ONLINE_SEARCH_GOOGLE = 40562;
constexpr int IDS_GOOGLE_SEARCH_URL = 0;
#define INI_SECTION_NAME_FLAGS L"Settings2"
#define COUNTOF(value) std::size(value)
#define NP2_align_up(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
#define NP2HeapAlloc(size) HeapAlloc(g_hDefaultHeap, HEAP_ZERO_MEMORY, size)
#define NP2HeapFree(value) HeapFree(g_hDefaultHeap, 0, value)
#define SciCall_GetSelTextLength() Send(Message::GetSelText)
#define SciCall_GetSelText(value) Send(Message::GetSelText, 0, reinterpret_cast<intptr_t>(value))
#define SciCall_GetCodePage() static_cast<UINT>(Send(Message::GetCodePage))
#define StrIsEmpty(value) (*(value) == L'\0')
#define StrNotEmpty(value) (*(value) != L'\0')
#define SetStrEmpty(value) (*(value) = L'\0')
#define kMaxMultiByteCount 3
#define IniGetString(section, key, fallback, value, count)                                                             \
    GetPrivateProfileStringW(section, key, fallback, value, count, szIniFile)
#include "Helpers.cpp.inc"
#include "Helpers.h.inc"
#undef ShellExecuteEx
#define ShellExecuteEx RecordLaunch
#include "Edit.cpp.inc"
#undef ShellExecuteEx

void Run(const char* name, const std::string& text, int action, int shape = 0, bool named = false) {
    caseName = name;
    Send(Message::SetSelectionMode, 0);
    Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
    Send(Message::SetSel, 0, static_cast<intptr_t>(text.size()));
    if (shape == 1) {
        Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        Send(Message::SetRectangularSelectionAnchor, 0);
        Send(Message::SetRectangularSelectionCaret, 7);
    } else if (shape == 2) {
        Send(Message::SetMultipleSelection, 1);
        Send(Message::SetSelection, 2, 0);
        Send(Message::AddSelection, 8, 6);
    } else if (shape == 3) {
        Send(Message::SetSel, 1, 1);
    }
    lstrcpyW(szCurFile, named ? L"C:\\owned\\documents\\source.txt" : L"");
    const auto before = launchCount;
    EditSelectionAction(CMD_ONLINE_SEARCH_GOOGLE + action);
    if (launchCount == before) output << caseName << "\tNO-LAUNCH\t-\t-\n";
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    std::filesystem::create_directories(argv[1]);
    const auto directory = std::filesystem::path(argv[1]);
    lstrcpyW(szIniFile, (directory / L"original-actions.ini").c_str());
    output.open(directory / L"original-selection-actions.tsv", std::ios::binary);
    output
        << "# Notepad4 89b8b36e0ee2c23f73be922315f17261ccfbbe1a; native original control, extracted verbatim helpers\n"
           "# name target_utf8_hex arguments_utf8_hex directory_utf8_hex\n";
    Scintilla_RegisterClasses(GetModuleHandleW(nullptr));
    const auto frame = CreateWindowExW(0, L"STATIC", L"Owned original selection actions", WS_OVERLAPPEDWINDOW, 0, 0,
                                       640, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    editor = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD | WS_VISIBLE, 0, 0, 620, 360, frame, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    if (!editor) return 3;
    Send(Message::SetCodePage, CP_UTF8);
    for (language = 0; language < 3; ++language)
        for (int action = 0; action < 3; ++action) {
            const auto name = "default-" + std::to_string(language) + "-" + std::to_string(action);
            Run(name.c_str(), "  A 中文 &?#/  ", action);
        }
    language = 0;
    const wchar_t* keys[] = {L"GoogleSearchUrl", L"BingSearchUrl", L"WikiSearchUrl", L"CustomAction1",
                             L"CustomAction2"};
    for (int action = 0; action < 5; ++action) {
        WritePrivateProfileStringW(L"Settings2", keys[action], L"wx265://owned/?query=%s", szIniFile);
        const auto name = "key-" + std::to_string(action);
        Run(name.c_str(), " a b &?#/中\r\nline 2\t", action);
    }
    Run("trim-control", "\a\b\f\n\r\t\v A B \a\b\f\n\r\t\v", 0);
    Run("reserved", "a:b/c?d=e&f#tail", 0);
    Run("rectangle", "abXX\r\ncdYY", 0, 1);
    Run("multiple", "abXX\r\ncdYY", 0, 2);
    Run("empty", "", 0);
    Run("caret", "word", 0, 3);
    Run("trim-empty", " \a\b\f\n\r\t\v", 0);
    WritePrivateProfileStringW(L"Settings2", L"CustomAction2", L"", szIniFile);
    Run("custom-unconfigured", "query", 4);
    WritePrivateProfileStringW(
        L"Settings2", L"CustomAction1",
        L"\"C:\\owned\\program folder\\record 中文.exe\" \"%s\" \"%%WX265_VALUE%%\" literal=%%%%", szIniFile);
    SetEnvironmentVariableW(L"WX265_VALUE", L"expanded 中文 value");
    Run("quoted-executable", " one two 中 ", 3);
    Run("named-directory", " one two 中 ", 3, 0, true);
    DestroyWindow(frame);
    Scintilla_ReleaseResources();
    return output ? 0 : 4;
}
