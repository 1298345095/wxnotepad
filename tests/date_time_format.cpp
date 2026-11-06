#include "editor/date_time.h"
#include <wx/init.h>
#include <cstdio>
#include <cstdlib>
#include <string_view>

int main(int argc, char**) {
    wxInitializer initialize;
    if (!initialize) return 1;
    // 2024-01-02 03:04:05.1234567Z; local UTC-8 rolls into the previous day.
    constexpr uint64_t ticks = 17041646451234567;
    const auto check = [](DateTimeInsertion command, uint64_t value, std::string_view expected,
                          const wxString& format = {}) {
        std::tm local{};
        local.tm_year = 124;
        local.tm_mon = 0;
        local.tm_mday = 1;
        local.tm_wday = 1;
        local.tm_hour = 19;
        local.tm_min = 4;
        local.tm_sec = 5;
        const auto actual = FormatDateTimeInsertion(command, {value, local}, format);
        if (actual == wxString::FromUTF8(expected.data(), expected.size())) return true;
        std::fprintf(stderr, "Expected <%.*s>, got <%s>\n", static_cast<int>(expected.size()), expected.data(),
                     actual.utf8_str().data());
        return false;
    };
    bool ok = check(DateTimeInsertion::LocalDate, ticks, "2024-01-01");
    ok &= check(DateTimeInsertion::LocalDateTime, ticks, "2024-01-01 19:04:05");
    ok &= check(DateTimeInsertion::UtcDateTime, ticks, "2024-01-02T03:04:05Z");
    ok &= check(DateTimeInsertion::UnixSeconds, ticks, "1704164645");
    ok &= check(DateTimeInsertion::UnixMilliseconds, ticks, "1704164645123");
    ok &= check(DateTimeInsertion::UnixMicroseconds, ticks, "1704164645123456");
    ok &= check(DateTimeInsertion::UnixNanoseconds, ticks, "1704164645123456700");
    ok &= check(DateTimeInsertion::Short, ticks, "日期 2024年01月01日 19:04:05",
                wxString::FromUTF8("日期 %Y年%m月%d日 %H:%M:%S"));
    if (argc > 1) {
        ok &= check(DateTimeInsertion::Short, ticks, "19时04分 2024年01月01日");
        ok &= check(DateTimeInsertion::Long, ticks, "19时04分 2024年01月01日 星期一");
        ok &= check(DateTimeInsertion::Long, ticks, "星期一 一月", "%A %B");
    } else {
        ok &= check(DateTimeInsertion::Long, ticks, "Monday 1", "%A %w");
#ifndef __WXMSW__
        ok &= check(DateTimeInsertion::Short, ticks, "19:04 01/01/24");
        ok &= check(DateTimeInsertion::Long, ticks, "19:04 Mon Jan  1 2024");
#endif
    }
    ok &= check(DateTimeInsertion::UtcDateTime, 0, "1970-01-01T00:00:00Z");
    if (ok) std::puts("PASS: pinned insertion formats, local rollover, Unicode templates and 100ns precision");
    return ok ? 0 : 1;
}
