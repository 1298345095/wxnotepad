// Host the untouched pinned Win32 Scintilla control independently of wxnotepad.
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <ScintillaTypes.h>
#include <ScintillaMessages.h>
#include <SciLexer.h>

HANDLE g_hDefaultHeap = GetProcessHeap();
UINT g_uSystemDPI = USER_DEFAULT_SCREEN_DPI;
unsigned int dwUrlThreshold = 0;
WCHAR defaultTextFontName[LF_FACESIZE] = L"Consolas";
// The application-supplied case-conversion callback is outside this scene.
// Fail explicitly if the diagnostic accidentally starts exercising it.
char* EditMapTextCase(int, const char*, size_t&, UINT) noexcept {
    std::abort();
}
extern "C" int Scintilla_RegisterClasses(void* instance);
extern "C" int Scintilla_ReleaseResources();

namespace {
using Scintilla::Message;
constexpr size_t MinimumFixtureBytes = size_t{1024} * 1024;
constexpr size_t FixtureBytes = 1048597;
constexpr int ViewportHeight = 598;
constexpr int ReferenceDpi = 144;
constexpr int ReferencePointSizeFractional = 1200;
constexpr int StyleCount = 256;
constexpr int PositionCacheEntries = 1024;
constexpr int IndentColumns = 2;
constexpr auto DefaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
constexpr std::array Widths{992, 872, 1172, 872, 1172, 872, 1172, 872, 1172, 872, 1172, 872, 1172};
HWND editor = nullptr;
intptr_t Send(Message message, uintptr_t first = 0, intptr_t second = 0) {
    return SendMessageW(editor, static_cast<UINT>(message), first, second);
}
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
LRESULT CALLBACK FrameProc(HWND window, UINT message, WPARAM first, LPARAM second) {
    if (message == WM_SIZE && editor) {
        RECT client{};
        GetClientRect(window, &client);
        MoveWindow(editor, 0, 0, client.right, client.bottom, TRUE);
    }
    return DefWindowProcW(window, message, first, second);
}
void Paint() {
    for (int pass = 0; pass < 2; ++pass) {
        RedrawWindow(editor, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}
void Resize(HWND frame, int width, bool setup) {
    for (int attempt = 0; attempt < (setup ? 3 : 1); ++attempt) {
        RECT client{}, outer{};
        GetClientRect(editor, &client);
        GetWindowRect(frame, &outer);
        Require(SetWindowPos(frame, nullptr, 0, 0, outer.right - outer.left + width - client.right,
                             outer.bottom - outer.top + ViewportHeight - client.bottom,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE),
                "resize original frame");
        Paint();
        GetClientRect(editor, &client);
        if (client.right == width && client.bottom == ViewportHeight) return;
    }
    throw std::runtime_error("original control reaches the exact physical viewport");
}
std::string Fixture() {
    std::string text = "name = [";
    for (int item = 0; text.size() < MinimumFixtureBytes; ++item)
        text += "\"item" + std::to_string(item) + " 中文😀 text words \", ";
    text += "0]\n";
    Require(text.size() == FixtureBytes, "original fixture byte count is unchanged");
    return text;
}
void Scene(const std::string& text) {
    Send(Message::SetCodePage, Scintilla::CpUtf8);
    Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
    Send(Message::EmptyUndoBuffer);
    Send(Message::SetSavePoint);
    Send(Message::SetTechnology, static_cast<uintptr_t>(Scintilla::Technology::Default));
    Send(Message::SetWrapMode, static_cast<uintptr_t>(Scintilla::Wrap::None));
    Send(Message::SetLexer, SCLEX_TOML);
    Send(Message::SetFontQuality, static_cast<uintptr_t>(Scintilla::FontQuality::QualityDefault));
    Send(Message::SetFontLocale, 0, reinterpret_cast<intptr_t>(""));
    for (int style = 0; style < StyleCount; ++style) {
        Send(Message::StyleSetFont, style, reinterpret_cast<intptr_t>("Consolas"));
        Send(Message::StyleSetWeight, style, static_cast<uintptr_t>(Scintilla::FontWeight::Normal));
        Send(Message::StyleSetStretch, style, static_cast<uintptr_t>(Scintilla::FontStretch::Normal));
        Send(Message::StyleSetCharacterSet, style, static_cast<uintptr_t>(Scintilla::CharacterSet::Default));
        Send(Message::StyleSetItalic, style, 0);
        Send(Message::StyleSetSizeFractional, style,
             ReferencePointSizeFractional * ReferenceDpi / GetDpiForWindow(editor));
    }
    Send(Message::SetZoom, 100);
    Send(Message::SetLayoutCache, static_cast<uintptr_t>(Scintilla::LineCache::Caret));
    Send(Message::SetPositionCache, PositionCacheEntries);
    Send(Message::SetEdgeMode, static_cast<uintptr_t>(Scintilla::EdgeVisualStyle::None));
    Send(Message::SetViewWS, static_cast<uintptr_t>(Scintilla::WhiteSpace::Invisible));
    Send(Message::SetViewEOL, 0);
    Send(Message::SetTabWidth, IndentColumns);
    Send(Message::SetIndent, IndentColumns);
    Send(Message::SetIndentationGuides, static_cast<uintptr_t>(Scintilla::IndentView::None));
    Send(Message::SetPhasesDraw, static_cast<uintptr_t>(Scintilla::PhasesDraw::Two));
    Send(Message::SetWrapVisualFlags, 0);
    Send(Message::SetWrapVisualFlagsLocation, 0);
    Send(Message::SetWrapStartIndent, 0);
    Send(Message::SetWrapIndentMode, static_cast<uintptr_t>(Scintilla::WrapIndentMode::Fixed));
    Send(Message::SetExtraAscent, 0);
    Send(Message::SetExtraDescent, 0);
    Send(Message::Colourise, 0, -1);
    Paint();
    constexpr std::array Margins{25, 0, 26, 0, 0};
    for (size_t index = 0; index < Margins.size(); ++index)
        Send(Message::SetMarginWidthN, index, Margins[index]);
    Send(Message::SetMarginLeft, 0, 1);
    Send(Message::SetMarginRight, 0, 1);
    Require(Send(Message::GetTechnology) == static_cast<intptr_t>(Scintilla::Technology::Default) &&
                Send(Message::GetZoom) == 100 &&
                Send(Message::GetFontQuality) == static_cast<intptr_t>(Scintilla::FontQuality::QualityDefault) &&
                Send(Message::GetMarginLeft) == 1 && Send(Message::GetMarginRight) == 1,
            "original scene renderer, zoom, quality and edge margins match");
    for (size_t index = 0; index < Margins.size(); ++index)
        Require(Send(Message::GetMarginWidthN, index) == Margins[index], "original scene margins match");
    for (int style = 0; style < StyleCount; ++style) {
        std::string face(static_cast<size_t>(Send(Message::StyleGetFont, style)) + 1, char{});
        Send(Message::StyleGetFont, style, reinterpret_cast<intptr_t>(face.data()));
        Require(std::string(face.c_str()) == "Consolas" &&
                    Send(Message::StyleGetSizeFractional, style) ==
                        ReferencePointSizeFractional * ReferenceDpi / GetDpiForWindow(editor),
                "original scene font family and fractional size match for every style");
    }
    Send(Message::SetSel, FixtureBytes - 1, FixtureBytes - 1);
    Send(Message::SetWrapMode, static_cast<uintptr_t>(Scintilla::Wrap::Word));
    Paint();
}
void Record(std::ostream& output, int width) {
    RECT client{};
    GetClientRect(editor, &client);
    Require(client.right == width && client.bottom == ViewportHeight,
            "original record has the exact requested viewport");
    Require(Send(Message::GetAnchor) == FixtureBytes - 1 && Send(Message::GetCurrentPos) == FixtureBytes - 1,
            "original resizing preserves the exact selection");
    output << client.right << '\t' << client.bottom << '\t' << GetDpiForWindow(editor) << '\t'
           << Send(Message::WrapCount, 0) << '\t' << Send(Message::GetFirstVisibleLine) << '\t'
           << Send(Message::TextHeight, 0) << '\t' << Send(Message::LinesOnScreen) << '\n';
    Require(static_cast<bool>(output), "write original reference row");
}
} // namespace

int wmain(int count, wchar_t** arguments) {
    HWND frame = nullptr;
    try {
        Require(count == 2, "provide the original reference TSV output path");
        const auto instance = GetModuleHandleW(nullptr);
        Require(Scintilla_RegisterClasses(instance), "register original native control");
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = FrameProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = L"OriginalViewportReference";
        Require(RegisterClassW(&windowClass), "register original reference host");
        frame = CreateWindowExW(0, windowClass.lpszClassName, L"Pinned original GDI viewport reference",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1050, 750, nullptr, nullptr,
                                instance, nullptr);
        Require(frame != nullptr, "create original reference host");
        editor = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL, 0, 0, 900, 600,
                                 frame, nullptr, instance, nullptr);
        Require(editor != nullptr, "create untouched original control");
        ShowWindow(frame, SW_SHOW);
        const auto text = Fixture();
        Scene(text);
        std::ofstream output{std::filesystem::path(arguments[1])};
        Require(static_cast<bool>(output), "open original reference output");
        // Version identifies this fixed native scene; the CLI verifies the archive hash.
        output << "notepad4-gdi-viewport-v1\t89b8b36e0ee2c23f73be922315f17261ccfbbe1a\t1048597\tConsolas\t24\n";
        output << "width\theight\tdpi\twrap\tfirst_visible\tline_height\tscreen_lines\n";
        Resize(frame, Widths.front(), true);
        Send(Message::GotoPos, FixtureBytes - 1);
        Paint();
        Record(output, Widths.front());
        for (size_t index = 1; index < Widths.size(); ++index) {
            Resize(frame, Widths[index], false);
            Record(output, Widths[index]);
        }
        std::string actual(FixtureBytes + 1, char{});
        Send(Message::GetText, actual.size(), reinterpret_cast<intptr_t>(actual.data()));
        actual.pop_back();
        Require(actual == text && Send(Message::GetLength) == FixtureBytes && !Send(Message::GetModify),
                "original reference preserves every fixture byte and save point");
        std::printf("original GDI: dpi=%u size_fractional=%lld W=%lld Chinese=%lld emoji=%lld; all13rows and exact "
                    "bytes retained\n",
                    GetDpiForWindow(editor),
                    static_cast<long long>(Send(Message::StyleGetSizeFractional, DefaultStyle)),
                    static_cast<long long>(Send(Message::TextWidth, DefaultStyle, reinterpret_cast<intptr_t>("W"))),
                    static_cast<long long>(Send(Message::TextWidth, DefaultStyle, reinterpret_cast<intptr_t>("中文"))),
                    static_cast<long long>(Send(Message::TextWidth, DefaultStyle, reinterpret_cast<intptr_t>("😀"))));
        DestroyWindow(frame);
        editor = nullptr;
        Scintilla_ReleaseResources();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL original reference: %s\n", error.what());
        if (frame) DestroyWindow(frame);
        return 1;
    }
}
