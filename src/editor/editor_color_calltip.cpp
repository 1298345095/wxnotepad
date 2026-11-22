#include "editor/editor_core.h"
#include "editor/ascii_character.h"
#include <GraphicUtils.h>
#include <charconv>
#include <format>
#include <wx/colordlg.h>
#include <wx/utils.h>

namespace {
constexpr intptr_t ScanRadiusBytes = 10;
constexpr size_t ScanHeadroom = 8;
constexpr uint32_t RgbMask = 0xFFFFFF;

uint32_t PreviewColor(uint32_t value, ColorCallTipMode mode, ptrdiff_t digits) noexcept {
    using enum ColorCallTipMode;
    if (digits == 6) return mode == RGBA || mode == ARGB ? ColorFromRGBHex(value) : value;
    if (digits == 8) {
        if (mode == RGBA) return ColorFromRGBAHex(value);
        if (mode == ARGB) return ColorFromARGBHex(value);
        if (mode == BGRA) return ColorFromBGRAHex(value);
        return value;
    }
    uint32_t alpha = 0;
    if (digits == 4) {
        if (mode == ARGB)
            alpha = value >> 12;
        else {
            alpha = value & 15;
            value >>= 4;
        }
    }
    const uint32_t red = (value >> 8) & 15;
    const uint32_t green = (value >> 4) & 15;
    const uint32_t blue = value & 15;
    return (red | (green << 8) | (blue << 16) | (alpha << 24)) * 0x11;
}

uint32_t ReplacementColor(uint32_t color, ColorCallTipMode mode, int digits) noexcept {
    using enum ColorCallTipMode;
    if (digits == 6) return mode == RGBA || mode == ARGB ? ColorToRGBHex(color) : color;
    if (mode == RGBA) return ColorToRGBAHex(color);
    if (mode == ARGB) return ColorToARGBHex(color);
    if (mode == BGRA) return ColorToBGRAHex(color);
    return color;
}

bool ShiftHeld() noexcept {
#ifdef __WXMSW__
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
#else
    return wxGetKeyState(WXK_SHIFT);
#endif
}
} // namespace

void EditorControl::ShowColorCallTip(intptr_t position) {
    using enum Scintilla::Message;
    if (colorCallTip_) {
        if (Send(CallTipActive) && position >= colorCallTip_->start && position < colorCallTip_->end) return;
        colorCallTip_.reset();
        Send(CallTipCancel);
    }
    if (position < 0 || colorCallTipMode_ == ColorCallTipMode::Off) return;

    std::array<char, 32> text{};
    const auto start = std::max<intptr_t>(0, position - ScanRadiusBytes);
    const auto end = std::min(position + ScanRadiusBytes, Send(GetLength));
    const Scintilla::TextRangeFull range{{start, end}, text.data() + ScanHeadroom};
    Send(GetTextRangeFull, 0, reinterpret_cast<intptr_t>(&range));
    const auto* hover = range.lpstrText + (position - start);
    auto* prefix = range.lpstrText + (position - start);
    while (Ascii::HexDigit(*prefix) >= 0)
        --prefix;
    const bool hash = *prefix == '#';
    if (!hash && !((*prefix == 'x' || *prefix == 'X') && prefix[-1] == '0')) return;
    auto* tail = range.lpstrText + (position - start) + 1;
    while (Ascii::HexDigit(*tail) >= 0)
        ++tail;
    const auto digits = tail - prefix - 1;
    if (!(hash && (digits == 3 || digits == 4)) && digits != 6 && digits != 8) return;

    uint32_t value = 0;
    if (std::from_chars(prefix + 1, tail, value, 16).ec != std::errc{}) return;
    const auto color = PreviewColor(value, colorCallTipMode_, digits);
    const auto anchor = position - (hover - prefix);
    colorCallTip_ = ColorCallTipRange{anchor, anchor + digits + (hash ? 1 : 2), color};
    *tail++ = '\n';
    *tail = '\0';
    auto* label = prefix - (hash ? 1 : 2);
    *label = '\n';
    Send(CallTipSetBack, color);
    Send(CallTipSetFore, ~color);
    Send(CallTipShow, anchor, reinterpret_cast<intptr_t>(label));
}

void EditorControl::QueueColorCallTipEdit() {
    using enum Scintilla::Message;
    if (!Send(CallTipActive)) return;
    const auto range = std::exchange(colorCallTip_, std::nullopt);
    Send(CallTipCancel);
    if (range) CallAfter([this, range = *range] { core_->RunNativeOperation([&] { EditColorCallTip(range); }); });
}

void EditorControl::EditColorCallTip(ColorCallTipRange range) {
    using enum Scintilla::Message;
    const auto rgb = range.color & RgbMask;
    wxColour initial;
    initial.SetRGB(rgb);
    wxColourData data;
    data.SetChooseFull(true);
    data.SetChooseAlpha(false);
    data.SetColour(initial);
    wxColourDialog chooser(wxGetTopLevelParent(this), &data);
    if (chooser.ShowModal() != wxID_OK) return;
    const auto chosen = chooser.GetColourData().GetColour().GetRGB();
    if (chosen == rgb) return;

    const auto color = chosen | (range.color & ~RgbMask);
    const int digits = color > RgbMask ? 8 : 6;
    const auto replacement = ReplacementColor(color, colorCallTipMode_, digits);
    const auto text =
        ShiftHeld() ? std::format("{:0{}X}", replacement, digits) : std::format("{:0{}x}", replacement, digits);
    Send(SetSel, range.start + 1, range.end);
    Send(ReplaceSel, 0, reinterpret_cast<intptr_t>(text.c_str()));
}
