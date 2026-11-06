#pragma once

#include <wx/string.h>
#include <cstdint>
#include <ctime>

enum class DateTimeInsertion : uint8_t {
    LocalDate,
    LocalDateTime,
    UtcDateTime,
    UnixSeconds,
    UnixMilliseconds,
    UnixMicroseconds,
    UnixNanoseconds,
    Short,
    Long
};

struct DateTimeInsertionValue {
    // The original clock's Unix-epoch ticks are 100ns, including on nanosecond output.
    uint64_t unixTicks;
    std::tm local;
};

DateTimeInsertionValue DateTimeInsertionClock();
wxString FormatDateTimeInsertion(DateTimeInsertion command, const DateTimeInsertionValue& value,
                                 const wxString& format = {});
