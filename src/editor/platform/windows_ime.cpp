// Inline composition adapted from Notepad4 89b8b36 ScintillaWin.cxx.
// Copyright 1998-2003 by Neil Hodgson. See licenses/Scintilla.txt
// and licenses/Notepad4.txt for redistribution terms.
#include "editor/editor_core.h"
#include <imm.h>
#include <cwchar>

namespace core = Scintilla::Internal;

namespace {
// Adapted from the pinned ScintillaWin IMContext and inline composition path.
class InputContext {
public:
    explicit InputContext(HWND window) : window_(window), context_(ImmGetContext(window)) {}
    ~InputContext() {
        if (context_) ImmReleaseContext(window_, context_);
    }
    InputContext(const InputContext&) = delete;
    InputContext& operator=(const InputContext&) = delete;
    explicit operator bool() const noexcept { return context_ != nullptr; }
    HIMC Get() const noexcept { return context_; }
    std::wstring Text(DWORD field) const {
        const LONG bytes = ImmGetCompositionStringW(context_, field, nullptr, 0);
        if (bytes <= 0) return {};
        std::wstring text(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
        const LONG copied =
            ImmGetCompositionStringW(context_, field, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)));
        if (copied <= 0) return {};
        text.resize(static_cast<size_t>(copied) / sizeof(wchar_t));
        return text;
    }
    std::vector<BYTE> Attributes(size_t units) const {
        const LONG bytes = ImmGetCompositionStringW(context_, GCS_COMPATTR, nullptr, 0);
        std::vector<BYTE> attributes(static_cast<size_t>(std::max<LONG>(0, bytes)));
        if (bytes > 0) ImmGetCompositionStringW(context_, GCS_COMPATTR, attributes.data(), static_cast<DWORD>(bytes));
        attributes.resize(units, ATTR_INPUT);
        return attributes;
    }
    void Candidate(core::Point point, int height, int width, int caretHeight) const noexcept {
        const int x = static_cast<int>(point.x);
        int y = static_cast<int>(point.y);
        const auto language = PRIMARYLANGID(LOWORD(reinterpret_cast<uintptr_t>(GetKeyboardLayout(0))));
        if (language == LANG_CHINESE) {
            CANDIDATEFORM position{0, CFS_CANDIDATEPOS, {x, y}, {0, 0, 0, 0}};
            ImmSetCandidateWindow(context_, &position);
        }
        if (language == LANG_CHINESE || language == LANG_JAPANESE) SetCaretPos(x, y);
        if (language == LANG_KOREAN) ++y;
        CANDIDATEFORM exclude{0, CFS_EXCLUDE, {x, y + std::max(4, height / 4)}, {x, y, x + width, y + caretHeight}};
        ImmSetCandidateWindow(context_, &exclude);
    }

private:
    HWND window_;
    HIMC context_;
};

unsigned MapIndicators(std::vector<BYTE>& attributes) {
    unsigned mask = 0;
    constexpr int indicators[] = {core::IndicatorInput, core::IndicatorTarget, core::IndicatorConverted,
                                  core::IndicatorTarget};
    for (BYTE& attribute : attributes) {
        attribute =
            static_cast<BYTE>(attribute <= ATTR_TARGET_NOTCONVERTED ? indicators[attribute] : core::IndicatorUnknown);
        mask |= 1U << static_cast<unsigned>(attribute - core::IndicatorInput);
    }
    return mask;
}
} // namespace

bool EditorControl::Core::UsesInlineIme() const noexcept {
    const auto language = PRIMARYLANGID(LOWORD(reinterpret_cast<uintptr_t>(GetKeyboardLayout(0))));
    return imeInteraction == Scintilla::IMEInteraction::Inline || language == LANG_KOREAN;
}

std::optional<intptr_t> EditorControl::Core::ImeMessage(unsigned message, uintptr_t wParam, intptr_t& lParam) {
    const auto flags = static_cast<uintptr_t>(lParam);
    try {
        switch (message) {
        case WM_IME_STARTCOMPOSITION:
            UpdateSystemCaret();
            imeComposing_ = true;
            if (UsesInlineIme()) return 0;
            DropCaret();
            break;
        case WM_IME_ENDCOMPOSITION:
            imeComposing_ = false;
            view.imeCaretBlockOverride = false;
            pdoc->TentativeUndo();
            ShowCaretAtCurrentPosition();
            UpdateSystemCaret();
            break;
        case WM_IME_COMPOSITION:
            if (UsesInlineIme()) return InlineComposition(flags);
            if (flags & GCS_RESULTSTR) {
                if (const InputContext context(static_cast<HWND>(window_.GetHandle())); context)
                    InsertImeText(context.Text(GCS_RESULTSTR), Scintilla::CharacterSource::ImeResult);
                UpdateSystemCaret();
                return 0;
            }
            UpdateSystemCaret();
            break;
        case WM_IME_SETCONTEXT:
            if (wParam && UsesInlineIme())
                lParam = static_cast<intptr_t>(flags & ~static_cast<uintptr_t>(ISC_SHOWUICOMPOSITIONWINDOW));
            UpdateSystemCaret();
            break;
        case WM_INPUTLANGCHANGE:
            UpdateSystemCaret();
            break;
        case WM_LBUTTONDOWN:
        case WM_KILLFOCUS:
            // Original MouseMessage/FocusMessage complete preedit before the
            // click changes the selection or the editor loses native focus.
            if (const InputContext context(static_cast<HWND>(window_.GetHandle())); context)
                ImmNotifyIME(context.Get(), NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
            break;
        default:
            break;
        }
    } catch (const std::bad_alloc&) {
        errorStatus = Scintilla::Status::BadAlloc;
        return 0;
    } catch (const core::Failure& failure) {
        errorStatus = failure.status;
        return 0;
    }
    return std::nullopt;
}

void EditorControl::Core::InsertImeText(std::wstring_view text, Scintilla::CharacterSource source,
                                        std::span<const unsigned char> indicators) {
    for (size_t index = 0; index < text.size();) {
        const auto character = text.substr(index, core::UTF16CharLength(text[index]));
        char encoded[16]{};
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, character.data(), static_cast<int>(character.size()), encoded,
                                              sizeof(encoded) - 1, nullptr, nullptr);
        InsertCharacter(std::string_view(encoded, bytes), source);
        if (!indicators.empty()) DrawImeIndicator(indicators[index], bytes);
        index += character.size();
    }
}

intptr_t EditorControl::Core::InlineComposition(uintptr_t flags) {
    const InputContext context(static_cast<HWND>(window_.GetHandle()));
    if (!context) return 0;
    if (pdoc->IsReadOnly() || SelectionContainsProtected()) {
        ImmNotifyIME(context.Get(), NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        return 0;
    }
    const core::DelaySavePoint delay(pdoc);
    const bool tentative = pdoc->TentativeActive();
    if (tentative) pdoc->TentativeUndo();
    view.imeCaretBlockOverride = false;
    if (flags & GCS_RESULTSTR) InsertImeText(context.Text(GCS_RESULTSTR), Scintilla::CharacterSource::ImeResult);
    if (flags & GCS_COMPSTR) {
        const auto text = context.Text(GCS_COMPSTR);
        if (text.empty()) {
            ShowCaretAtCurrentPosition();
            return 0;
        }
        if (!tentative) ClearBeforeTentativeStart();
        context.Candidate(PointMainCaret(), vs.lineHeight, systemCaretWidth_, systemCaretHeight_);
        pdoc->TentativeStart();
        auto attributes = context.Attributes(text.size());
        const auto mask = MapIndicators(attributes);
        InsertImeText(text, Scintilla::CharacterSource::TentativeInput, attributes);
        constexpr unsigned targetMask = 1U << (core::IndicatorTarget - core::IndicatorInput);
        if (mask != targetMask) {
            Sci::Position offset = -static_cast<Sci::Position>(text.size());
            if (!(flags & CS_NOMOVECARET) && (flags & GCS_CURSORPOS))
                offset += ImmGetCompositionStringW(context.Get(), GCS_CURSORPOS, nullptr, 0);
            if (offset) {
                const auto position = CurrentPosition();
                MoveImeCarets(pdoc->GetRelativePositionUTF16(position, offset) - position);
                if (mask & targetMask)
                    context.Candidate(PointMainCaret(), vs.lineHeight, systemCaretWidth_, systemCaretHeight_);
            }
        }
        view.imeCaretBlockOverride =
            PRIMARYLANGID(LOWORD(reinterpret_cast<uintptr_t>(GetKeyboardLayout(0)))) == LANG_KOREAN;
    }
    EnsureCaretVisible();
    ShowCaretAtCurrentPosition();
    return 0;
}

void EditorControl::Core::UpdateSystemCaret() {
    if (!hasFocus) {
        DestroySystemCaret();
        return;
    }
    const auto point = PointMainCaret();
    const int height = std::max(1, vs.lineHeight);
    const auto window = static_cast<HWND>(window_.GetHandle());
    const int width = std::max(1, vs.caret.width);
    if (!systemCaretBitmap_ || width != systemCaretWidth_ || height != systemCaretHeight_) {
        DestroySystemCaret();
        const std::vector<BYTE> bits(static_cast<size_t>((width + 15) / 16) * 2 * height);
        systemCaretBitmap_ = CreateBitmap(width, height, 1, 1, bits.data());
        if (systemCaretBitmap_ && ::CreateCaret(window, static_cast<HBITMAP>(systemCaretBitmap_), width, height)) {
            systemCaretWidth_ = width;
            systemCaretHeight_ = height;
            ::ShowCaret(window);
        }
    }
    ::SetCaretPos(static_cast<int>(point.x), static_cast<int>(point.y));
    if (imeComposing_ && UsesInlineIme()) return;
    const InputContext context(window);
    if (!context) return;
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = {static_cast<LONG>(point.x), static_cast<LONG>(point.y)};
    ::GetClientRect(window, &composition.rcArea);
    ImmSetCompositionWindow(context.Get(), &composition);
    CANDIDATEFORM candidate{};
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = {static_cast<LONG>(point.x), static_cast<LONG>(point.y) + height};
    candidate.rcArea = {static_cast<LONG>(point.x), static_cast<LONG>(point.y), static_cast<LONG>(point.x) + 1,
                        static_cast<LONG>(point.y) + height};
    ImmSetCandidateWindow(context.Get(), &candidate);
    LOGFONTW font{};
    // Use the original ScintillaWin character-height calculation, not line leading.
    const auto& style = vs.styles[pdoc->StyleIndexAt(sel.MainCaret())];
    const int sizeZoomed = core::GetFontSizeZoomed(style.size, vs.zoomLevel);
    font.lfHeight = -MulDiv(sizeZoomed, window_.GetDPI().y, 72 * 100);
    font.lfWeight = static_cast<LONG>(style.weight);
    font.lfItalic = style.italic;
    font.lfCharSet = DEFAULT_CHARSET;
    const auto name =
        wxString::FromUTF8(style.fontName ? style.fontName : core::Platform::DefaultFont()).ToStdWstring();
    std::wcsncpy(font.lfFaceName, name.c_str(), LF_FACESIZE - 1);
    ImmSetCompositionFontW(context.Get(), &font);
}

void EditorControl::Core::DestroySystemCaret() {
    if (!systemCaretBitmap_) return;
    ::DestroyCaret();
    DeleteObject(static_cast<HBITMAP>(systemCaretBitmap_));
    systemCaretBitmap_ = nullptr;
}
