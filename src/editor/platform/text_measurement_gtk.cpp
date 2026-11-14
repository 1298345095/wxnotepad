#include "editor/platform/text_measurement.h"
#include <wx/dc.h>
#include <wx/font.h>
#include <wx/fontutil.h>
#include <wx/graphics.h>
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Scintilla::Internal {
namespace {
constexpr double DefaultFontDpi = 96;
constexpr size_t Utf8MaximumBytes = 6;
template <typename T> struct ObjectUnref {
    void operator()(T* object) const { g_object_unref(object); }
};
template <typename T> using ObjectPtr = std::unique_ptr<T, ObjectUnref<T>>;
using FontDescription = std::unique_ptr<PangoFontDescription, decltype(&pango_font_description_free)>;
using FontOptions = std::unique_ptr<cairo_font_options_t, decltype(&cairo_font_options_destroy)>;
using LayoutIterator = std::unique_ptr<PangoLayoutIter, decltype(&pango_layout_iter_free)>;

struct WorkerState {
    ObjectPtr<PangoFontMap> map;
    ObjectPtr<PangoContext> context;
    ObjectPtr<PangoLayout> layout;
    std::unique_ptr<WorkerState> next;
    uint64_t snapshot = 0;

    WorkerState(cairo_font_type_t type, double resolution) : map(pango_cairo_font_map_new_for_font_type(type)) {
        if (!map) throw std::runtime_error("The native Pango measurement font map is unavailable");
        pango_cairo_font_map_set_resolution(PANGO_CAIRO_FONT_MAP(map.get()), resolution);
        context.reset(pango_font_map_create_context(map.get()));
        layout.reset(pango_layout_new(context.get()));
    }
};

struct WorkerPool {
    const cairo_font_type_t type;
    const double resolution;
    std::mutex mutex;
    std::unique_ptr<WorkerState> idle;

    WorkerPool(cairo_font_type_t type_, double resolution_) : type(type_), resolution(resolution_) {}
    std::unique_ptr<WorkerState> Acquire() {
        {
            const std::lock_guard lock(mutex);
            if (idle) {
                auto state = std::move(idle);
                idle = std::move(state->next);
                return state;
            }
        }
        return std::make_unique<WorkerState>(type, resolution);
    }
    void Release(std::unique_ptr<WorkerState> state) {
        const std::lock_guard lock(mutex);
        state->next = std::move(idle);
        idle = std::move(state);
    }
};

std::shared_ptr<WorkerPool> MeasurementPool(PangoFontMap* source) {
    // Only GUI-thread construction inspects the source. Each borrowed worker
    // state owns a separate native font map/context/layout with exclusive use.
    struct Cache {
        PangoFontMap* source = nullptr;
        guint serial = 0;
        double resolution = 0;
        std::shared_ptr<WorkerPool> pool;
    };
    static thread_local Cache cache;
    const auto serial = pango_font_map_get_serial(source);
    const auto resolution = pango_cairo_font_map_get_resolution(PANGO_CAIRO_FONT_MAP(source));
    if (cache.source != source || cache.serial != serial || cache.resolution != resolution) {
        auto pool =
            std::make_shared<WorkerPool>(pango_cairo_font_map_get_font_type(PANGO_CAIRO_FONT_MAP(source)), resolution);
        cache = {source, serial, resolution, std::move(pool)};
    }
    return cache.pool;
}

WorkerState& ThreadState(const std::shared_ptr<WorkerPool>& pool) {
    struct Lease {
        std::shared_ptr<WorkerPool> owner;
        std::unique_ptr<WorkerState> state;
        explicit Lease(std::shared_ptr<WorkerPool> pool_) : owner(std::move(pool_)), state(owner->Acquire()) {}
        ~Lease() { owner->Release(std::move(state)); }
    };
    // Linux's original async workers are short-lived. Return their states to
    // the GUI-owned pool at thread exit so subsequent jobs keep native caches.
    static thread_local std::unique_ptr<Lease> lease;
    if (!lease || lease->owner != pool) {
        lease.reset();
        lease = std::make_unique<Lease>(pool);
    }
    return *lease->state;
}

struct Characters {
    std::string text;
    size_t count = 0;
};

Characters Decode(std::string_view text, bool utf8) {
    if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("Pango text measurement exceeds its native length limit");
    Characters characters;
    if (utf8) {
        // wxString::FromUTF8 rejects invalid input and preserves embedded NULs.
        // Validate each NUL-delimited piece because GLib's bounded validator
        // treats an embedded NUL as invalid UTF-8.
        for (size_t start = 0; start < text.size();) {
            const auto nul = text.find('\0', start);
            const auto end = nul == std::string_view::npos ? text.size() : nul;
            const auto length = static_cast<gssize>(end - start);
            if (!g_utf8_validate(text.data() + start, length, nullptr)) return {};
            characters.count += static_cast<size_t>(g_utf8_strlen(text.data() + start, length));
            if (nul == std::string_view::npos) break;
            ++characters.count;
            start = end + 1;
        }
        characters.text.assign(text);
    } else {
        // wxString::From8BitData maps each byte directly to its Latin-1 value.
        characters.count = text.size();
        characters.text.reserve(text.size());
        for (const unsigned char character : text) {
            char encoded[Utf8MaximumBytes];
            const auto length = g_unichar_to_utf8(character, encoded);
            characters.text.append(encoded, static_cast<size_t>(length));
        }
    }
    if (characters.text.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("Pango text measurement exceeds its native length limit");
    return characters;
}

// Match wxCairoContext::GetPartialTextExtents in wxWidgets 3.3.3, including
// its compact cursor-cluster array and padding to the Unicode character count.
std::vector<int> PartialExtents(PangoLayout* layout, size_t characterCount) {
    std::vector<int> extents;
    int width = 0;
    if (pango_layout_get_character_count(layout)) {
        int attributeCount = 0;
        const auto* attributes = pango_layout_get_log_attrs_readonly(layout, &attributeCount);
        LayoutIterator iterator(pango_layout_get_iter(layout), pango_layout_iter_free);
        int byteIndex = 0;
        PangoRectangle rectangle;
        for (int character = 1; pango_layout_iter_next_char(iterator.get()); ++character) {
            if (character >= attributeCount)
                throw std::runtime_error("Pango layout character attributes do not match its text");
            byteIndex = pango_layout_iter_get_index(iterator.get());
            if (!attributes[character].is_cursor_position) continue;
            pango_layout_index_to_pos(layout, byteIndex, &rectangle);
            width = rectangle.width < 0 ? rectangle.x + rectangle.width : rectangle.x;
            extents.push_back(PANGO_PIXELS(width));
        }
        pango_layout_index_to_pos(layout, byteIndex, &rectangle);
        width = rectangle.width < 0 ? rectangle.x : rectangle.x + rectangle.width;
        extents.push_back(PANGO_PIXELS(width));
    }
    extents.resize(characterCount, PANGO_PIXELS(width));
    return extents;
}
} // namespace

struct MeasurementFont::Data {
    FontDescription description{nullptr, pango_font_description_free};
};

MeasurementFont::MeasurementFont(const wxFont& font) : data_(std::make_unique<Data>()) {
    const auto* info = font.GetNativeFontInfo();
    if (!info || !info->description) throw std::runtime_error("The native measurement font is unavailable");
    data_->description.reset(pango_font_description_copy(info->description));
}

MeasurementFont::~MeasurementFont() = default;

struct TextMeasurement::Data {
    std::shared_ptr<WorkerPool> pool;
    FontOptions options{nullptr, cairo_font_options_destroy};
    std::optional<PangoMatrix> matrix;
    double resolution = 0;
    PangoDirection direction{};
    PangoGravity gravity{};
    PangoGravityHint gravityHint{};
    gboolean rounded = true;
    PangoLanguage* language = nullptr;
    float fontScale = 1;
    const uint64_t serial = NextSerial();

    static uint64_t NextSerial() {
        static std::atomic<uint64_t> next{1};
        return next.fetch_add(1, std::memory_order_relaxed);
    }
    WorkerState& Context() const {
        auto& worker = ThreadState(pool);
        if (worker.snapshot != serial) {
            pango_cairo_context_set_resolution(worker.context.get(), resolution);
            pango_context_set_matrix(worker.context.get(), matrix ? &*matrix : nullptr);
            pango_context_set_base_dir(worker.context.get(), direction);
            pango_context_set_base_gravity(worker.context.get(), gravity);
            pango_context_set_gravity_hint(worker.context.get(), gravityHint);
            pango_context_set_round_glyph_positions(worker.context.get(), rounded);
            pango_context_set_language(worker.context.get(), language);
            pango_cairo_context_set_font_options(worker.context.get(), options.get());
            pango_layout_context_changed(worker.layout.get());
            worker.snapshot = serial;
        }
        return worker;
    }

    void SetText(WorkerState& worker, const MeasurementFont::Data& font, const Characters& characters) const {
        FontDescription scaled(nullptr, pango_font_description_free);
        const auto* description = font.description.get();
        if (fontScale != 1) {
            scaled.reset(pango_font_description_copy(description));
            // wxFont::Scaled(float) scales fractional points, then rounds back
            // to Pango units with wxRound(), even for an absolute-size font.
            const double points = static_cast<double>(pango_font_description_get_size(description)) / PANGO_SCALE;
            pango_font_description_set_size(scaled.get(),
                                            static_cast<int>(std::lround(points * fontScale * PANGO_SCALE)));
            description = scaled.get();
        }
        pango_layout_set_font_description(worker.layout.get(), description);
        pango_layout_set_text(worker.layout.get(), characters.text.data(), static_cast<int>(characters.text.size()));
    }
};

TextMeasurement::TextMeasurement(wxDC& dc) : data_(std::make_unique<Data>()) {
    auto* graphics = dc.GetGraphicsContext();
    auto* cairo = graphics ? static_cast<cairo_t*>(graphics->GetNativeContext()) : nullptr;
    if (!cairo || cairo_status(cairo) != CAIRO_STATUS_SUCCESS)
        throw std::runtime_error("The native Cairo measurement context is unavailable");

    ObjectPtr<PangoContext> source(pango_cairo_create_context(cairo));
    data_->pool = MeasurementPool(pango_context_get_font_map(source.get()));
    data_->resolution = pango_cairo_context_get_resolution(source.get());
    if (const auto* matrix = pango_context_get_matrix(source.get())) data_->matrix = *matrix;
    data_->direction = pango_context_get_base_dir(source.get());
    data_->gravity = pango_context_get_base_gravity(source.get());
    data_->gravityHint = pango_context_get_gravity_hint(source.get());
    data_->rounded = pango_context_get_round_glyph_positions(source.get());
    data_->language = pango_context_get_language(source.get());

    // pango_cairo_context_get_font_options omits the target surface options.
    // Merge both sources exactly as pango_cairo_update_context does internally.
    FontOptions options(cairo_font_options_create(), cairo_font_options_destroy);
    FontOptions explicitOptions(cairo_font_options_create(), cairo_font_options_destroy);
    cairo_surface_get_font_options(cairo_get_target(cairo), options.get());
    cairo_get_font_options(cairo, explicitOptions.get());
    cairo_font_options_merge(options.get(), explicitOptions.get());
    if (cairo_font_options_status(options.get()) != CAIRO_STATUS_SUCCESS)
        throw std::runtime_error("The native Cairo measurement font options are unavailable");
    data_->options = std::move(options);

    auto* screen = gdk_screen_get_default();
    data_->fontScale = screen ? static_cast<float>(gdk_screen_get_resolution(screen) / DefaultFontDpi) : 1;
}

TextMeasurement::~TextMeasurement() = default;

void TextMeasurement::MeasureWidths(const MeasurementFont& font, std::string_view text, bool utf8,
                                    double* positions) const {
    const auto characters = Decode(text, utf8);
    auto& worker = data_->Context();
    data_->SetText(worker, *font.data_, characters);
    const auto extents = PartialExtents(worker.layout.get(), characters.count);
    std::vector<PangoLogAttr> cursorPositions;
    if (utf8 && !extents.empty() &&
        std::any_of(text.begin(), text.end(), [](unsigned char ch) { return ch >= 0x80; })) {
        cursorPositions.resize(characters.count + 1);
        pango_get_log_attrs(characters.text.data(), static_cast<int>(characters.text.size()), 0, data_->language,
                            cursorPositions.data(), static_cast<int>(cursorPositions.size()));
    }
    size_t character = 0;
    size_t cursorExtent = 0;
    for (size_t byte = 0; byte < text.size();) {
        const unsigned char lead = text[byte];
        size_t length = 1;
        if (utf8 && lead >= 0xc0) length = lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        length = std::min(length, text.size() - byte);
        ++character;
        size_t extent = character - 1;
        if (!cursorPositions.empty()) {
            extent = cursorExtent;
            if (character < cursorPositions.size() && cursorPositions[character].is_cursor_position) ++cursorExtent;
        }
        const double width = extent < extents.size() ? extents[extent] : 0;
        std::fill_n(positions + byte, length, width);
        byte += length;
    }
}

double TextMeasurement::WidthText(const MeasurementFont& font, std::string_view text, bool utf8) const {
    const auto characters = Decode(text, utf8);
    auto& worker = data_->Context();
    data_->SetText(worker, *font.data_, characters);
    int width = 0;
    pango_layout_get_pixel_size(worker.layout.get(), &width, nullptr);
    return width;
}
} // namespace Scintilla::Internal
