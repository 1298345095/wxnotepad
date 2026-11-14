// Native measurement follows Notepad4 89b8b36 SurfaceGDI.cxx. Font scaling and
// italic bounds retain wxWidgets 3.3.3's MSW DC/text-measurement conventions.
#include "text_measurement.h"
#include <wx/dc.h>
#include <wx/font.h>
#include <wx/fontutil.h>
#include <windows.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace Scintilla::Internal {
namespace {
constexpr double PointsPerInch = 72.0;
[[noreturn]] void ThrowLastError(const char* operation) {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}
struct DeleteGdiObject {
    void operator()(HGDIOBJ object) const noexcept { DeleteObject(object); }
};
using OwnedFont = std::unique_ptr<std::remove_pointer_t<HFONT>, DeleteGdiObject>;
using OwnedBitmap = std::unique_ptr<std::remove_pointer_t<HBITMAP>, DeleteGdiObject>;
struct DeleteContext {
    void operator()(HDC dc) const noexcept { DeleteDC(dc); }
};
using OwnedContext = std::unique_ptr<std::remove_pointer_t<HDC>, DeleteContext>;

std::wstring Characters(std::string_view text, bool utf8) {
    if (text.size() > static_cast<size_t>(INT_MAX))
        throw std::length_error("GDI text measurement exceeds its native length limit");
    std::wstring result(text.size(), L'\0');
    if (utf8 && !text.empty()) {
        // wxString::FromUTF8 returns an empty string for invalid UTF-8 too.
        const int length =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                result.data(), static_cast<int>(result.size()));
        if (!length && GetLastError() != ERROR_NO_UNICODE_TRANSLATION) ThrowLastError("MultiByteToWideChar");
        result.resize(static_cast<size_t>(length));
    } else {
        std::transform(text.begin(), text.end(), result.begin(), [](unsigned char ch) { return wchar_t(ch); });
    }
    return result;
}

class SelectedFont final {
public:
    SelectedFont(HDC dc, HFONT font) : dc_(dc), previous_(SelectObject(dc, font)) {
        if (!previous_ || previous_ == HGDI_ERROR) ThrowLastError("SelectObject measurement font");
    }
    ~SelectedFont() {
        SelectObject(dc_, previous_);
        // Complete this thread's GDI queue before font ownership can be
        // released by the GUI thread after the core joins its workers.
        GdiFlush();
    }
    SelectedFont(const SelectedFont&) = delete;
    SelectedFont& operator=(const SelectedFont&) = delete;

private:
    HDC dc_;
    HGDIOBJ previous_;
};

int TextWidth(HDC dc, const std::wstring& text, bool italic) {
    SIZE size{};
    if (!GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &size))
        ThrowLastError("GetTextExtentPoint32W");
    if (italic && !text.empty()) {
        ABC bounds{};
        const auto first = text.front();
        const auto last = text.back();
        if (GetCharABCWidthsW(dc, first, first, &bounds)) {
            if (bounds.abcA < 0) size.cx -= bounds.abcA;
            if (text.size() > 1) GetCharABCWidthsW(dc, last, last, &bounds);
            if (bounds.abcC < 0) size.cx -= bounds.abcC;
        }
    }
    return size.cx;
}
} // namespace

struct MeasurementFont::Data {
    LOGFONTW description{};
    double points = 0;
    mutable std::mutex mutex;
    mutable std::map<std::pair<DWORD, int>, OwnedFont> fonts;

    HFONT Handle(int fontDpi) const {
        auto logical = description;
        logical.lfHeight = -static_cast<LONG>(std::lround(points * fontDpi / PointsPerInch));
        const auto key = std::pair{GetCurrentThreadId(), static_cast<int>(logical.lfHeight)};
        const std::lock_guard lock(mutex);
        if (const auto found = fonts.find(key); found != fonts.end()) return found->second.get();
        OwnedFont font(CreateFontIndirectW(&logical));
        if (!font) ThrowLastError("CreateFontIndirectW");
        return fonts.emplace(key, std::move(font)).first->second.get();
    }
};

MeasurementFont::MeasurementFont(const wxFont& font) : data_(std::make_unique<Data>()) {
    const auto* native = font.GetNativeFontInfo();
    if (!native) throw std::runtime_error("Native text measurement needs a valid font");
    data_->description = native->lf;
    data_->points = font.GetFractionalPointSize();
}
MeasurementFont::~MeasurementFont() = default;

namespace {
struct WorkerContext {
    OwnedBitmap bitmap;
    OwnedContext dc;
    int planes;
    int bitsPerPixel;
    explicit WorkerContext(const BITMAP& target)
        : dc(CreateCompatibleDC(nullptr)), planes(target.bmPlanes), bitsPerPixel(target.bmBitsPixel) {
        if (!dc) ThrowLastError("CreateCompatibleDC");
        bitmap.reset(CreateBitmap(1, 1, planes, bitsPerPixel, nullptr));
        if (!bitmap) ThrowLastError("CreateBitmap");
        const auto previous = SelectObject(dc.get(), bitmap.get());
        if (!previous || previous == HGDI_ERROR) ThrowLastError("SelectObject measurement bitmap");
    }
};
} // namespace

struct TextMeasurement::Data {
    BITMAP target{};
    int fontDpi;
    int mapMode;
    SIZE window{}, viewport{};
    explicit Data(wxDC& source) : fontDpi(source.GetPPI().y) {
        const auto sourceDC = static_cast<HDC>(source.GetHDC());
        // Capture the compatible target's format on the GUI thread. Workers
        // create native objects from this value, without touching the source DC.
        const OwnedBitmap bitmap(CreateCompatibleBitmap(sourceDC, 1, 1));
        if (!bitmap) ThrowLastError("CreateCompatibleBitmap");
        if (!GetObjectW(bitmap.get(), sizeof(target), &target)) ThrowLastError("GetObject measurement bitmap");
        target.bmBits = nullptr;
        mapMode = GetMapMode(sourceDC);
        GetWindowExtEx(sourceDC, &window);
        GetViewportExtEx(sourceDC, &viewport);
    }
    HDC ThreadContext() const {
        static thread_local std::unique_ptr<WorkerContext> worker;
        if (!worker || worker->planes != target.bmPlanes || worker->bitsPerPixel != target.bmBitsPixel)
            worker = std::make_unique<WorkerContext>(target);
        SetMapMode(worker->dc.get(), mapMode);
        SetWindowExtEx(worker->dc.get(), window.cx, window.cy, nullptr);
        SetViewportExtEx(worker->dc.get(), viewport.cx, viewport.cy, nullptr);
        return worker->dc.get();
    }
};

TextMeasurement::TextMeasurement(wxDC& dc) : data_(std::make_unique<Data>(dc)) {
}
TextMeasurement::~TextMeasurement() = default;

void TextMeasurement::MeasureWidths(const MeasurementFont& font, std::string_view text, bool utf8,
                                    double* positions) const {
    const auto characters = Characters(text, utf8);
    std::vector<int> extents(characters.size());
    if (!characters.empty()) {
        const auto dc = data_->ThreadContext();
        const SelectedFont selected(dc, font.data_->Handle(data_->fontDpi));
        SIZE size{};
        int fitted = 0;
        if (!GetTextExtentExPointW(dc, characters.data(), static_cast<int>(characters.size()), INT_MAX, &fitted,
                                   extents.data(), &size))
            ThrowLastError("GetTextExtentExPointW");
        // Match wxTextMeasure's tab correction; the editor normally represents
        // tabs separately, but the Surface interface also accepts them directly.
        int offset = 0;
        const auto tab = characters.find(L'\t');
        const int tabWidth = tab == std::wstring::npos ? 0 : TextWidth(dc, L"\t", font.data_->description.lfItalic);
        for (size_t index = 0; index < characters.size(); ++index) {
            if (characters[index] == L'\t') offset += tabWidth;
            extents[index] += offset;
        }
    }
    // Expand UTF-16 advances to the original UTF-8 byte positions, including
    // surrogate pairs, following the original SurfaceGDI measurement path.
    size_t character = 0;
    for (size_t byte = 0; byte < text.size();) {
        const unsigned char lead = text[byte];
        size_t width = 1;
        if (utf8 && lead >= 0xc0) width = lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        width = std::min(width, text.size() - byte);
        character += utf8 && width == 4 ? 2 : 1;
        const double x = character <= extents.size() ? extents[character - 1] : 0;
        std::fill_n(positions + byte, width, x);
        byte += width;
    }
}

double TextMeasurement::WidthText(const MeasurementFont& font, std::string_view text, bool utf8) const {
    const auto characters = Characters(text, utf8);
    const auto dc = data_->ThreadContext();
    const SelectedFont selected(dc, font.data_->Handle(data_->fontDpi));
    return TextWidth(dc, characters, font.data_->description.lfItalic);
}
} // namespace Scintilla::Internal
