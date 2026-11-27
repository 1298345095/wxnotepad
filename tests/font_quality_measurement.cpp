// Observe real native resources at the OS boundary; forward every operation.
#include "font_quality_measurement.h"
#include <mutex>

namespace {
std::mutex observationMutex;
bool observing = false;
std::vector<NativeFontSelection> selections;
HGDIOBJ WINAPI ObserveSelectObject(HDC dc, HGDIOBJ object) {
    const auto previous = ::SelectObject(dc, object);
    if (GetObjectType(object) == OBJ_FONT) {
        const std::lock_guard lock(observationMutex);
        if (observing) {
            LOGFONTW description{};
            if (GetObjectW(object, sizeof(description), &description))
                selections.push_back({GetCurrentThreadId(), dc, static_cast<HFONT>(object), description});
        }
    }
    return previous;
}
} // namespace
void BeginNativeFontObservation() {
    const std::lock_guard lock(observationMutex);
    selections.clear();
    observing = true;
}
std::vector<NativeFontSelection> EndNativeFontObservation() {
    const std::lock_guard lock(observationMutex);
    observing = false;
    return selections;
}
#define SelectObject ObserveSelectObject
#include "editor/platform/text_measurement_msw.cpp"
