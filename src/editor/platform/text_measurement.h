#pragma once

#include <memory>
#include <string_view>

class wxDC;
class wxFont;
#ifdef _WIN32
struct _devicemodeW;
#else
struct _GtkPrintContext;
#endif

namespace Scintilla::Internal {
// Capture native font data on the GUI thread. Measurements never access wxFont.
class MeasurementFont final {
public:
    explicit MeasurementFont(const wxFont& font, std::string_view locale = {});
    ~MeasurementFont();
    MeasurementFont(const MeasurementFont&) = delete;
    MeasurementFont& operator=(const MeasurementFont&) = delete;

private:
    friend class TextMeasurement;
    struct Data;
    std::unique_ptr<Data> data_;
};

// Snapshot the drawing DC on the GUI thread; own separate native measurement
// resources so workers neither select fonts into it nor access wx GUI objects.
class TextMeasurement final {
public:
    explicit TextMeasurement(wxDC& dc);
#ifdef _WIN32
    TextMeasurement(wxDC& dc, std::wstring_view printer, const _devicemodeW& mode);
#else
    TextMeasurement(_GtkPrintContext& printer, int fontDpi);
#endif
    ~TextMeasurement();
    TextMeasurement(const TextMeasurement&) = delete;
    TextMeasurement& operator=(const TextMeasurement&) = delete;

    void MeasureWidths(const MeasurementFont& font, std::string_view text, bool utf8, double* positions) const;
    double WidthText(const MeasurementFont& font, std::string_view text, bool utf8) const;
#ifdef __WXGTK__
    // Draw with the same private Pango font/language snapshot used for widths.
    void DrawText(wxDC& dc, const MeasurementFont& font, std::string_view text, bool utf8, int x, int y) const;
#endif

private:
    struct Data;
    std::unique_ptr<Data> data_;
};

} // namespace Scintilla::Internal
