// Observe language on the actual private layout immediately before native rasterization.
#include <pango/pangocairo.h>
#include <string>
#include <vector>
namespace {
std::vector<std::string> locales;
void ObservePangoShowLayout(cairo_t* cairo, PangoLayout* layout) {
    auto* attributes = pango_layout_get_attributes(layout);
    if (attributes) {
        auto* iterator = pango_attr_list_get_iterator(attributes);
        auto* language = pango_attr_iterator_get(iterator, PANGO_ATTR_LANGUAGE);
        if (language)
            locales.emplace_back(pango_language_to_string(reinterpret_cast<PangoAttrLanguage*>(language)->value));
        pango_attr_iterator_destroy(iterator);
    }
    pango_cairo_show_layout(cairo, layout);
}
}
void ClearNativeStyleLocales() { locales.clear(); }
std::vector<std::string> NativeStyleLocales() { return locales; }
#define pango_cairo_show_layout ObservePangoShowLayout
#include "editor/platform/text_measurement_gtk.cpp"
