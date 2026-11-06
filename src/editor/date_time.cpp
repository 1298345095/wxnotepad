#include "editor/date_time.h"
#include "editor/editor_control.h"
#include <chrono>
#include <ctime>
#include <cwchar>
#include <cwctype>
#ifdef __WXMSW__
#include <windows.h>
#else
#include <langinfo.h>
#include <locale.h>
#endif

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace {
#ifndef __WXMSW__
std::wstring LongDateFormat(const std::wstring& format) {
    std::wstring date;
    bool time = false;
    for (size_t index = 0; index < format.size();) {
        const size_t start = index++;
        if (format[start] == L'%' && index < format.size()) {
            if ((format[index] == L'E' || format[index] == L'O') && index + 1 < format.size()) ++index;
            const auto specifier = format[index++];
            if (std::wcschr(L"HIklMpPrRsSTXzZ", specifier)) {
                while (!date.empty() && std::wcschr(L" :./-", date.back()))
                    date.pop_back();
                time = true;
                continue;
            }
            if (time && !date.empty()) date += L' ';
            time = false;
        }
        if (!time) date.append(format, start, index - start);
    }
    return date;
}

// POSIX locales expose a time pattern including seconds. Retain its hour cycle,
// minute separators and AM/PM marker while removing the seconds field.
std::wstring MinuteFormat(std::wstring format, locale_t locale) {
    for (size_t index = 0; index + 1 < format.size(); ++index) {
        if (format[index] != L'%') continue;
        const wchar_t specifier = format[++index];
        if (specifier == L'%') continue;
        if (specifier == L'r') {
            const auto expanded = wxString::FromUTF8(nl_langinfo_l(T_FMT_AMPM, locale)).ToStdWstring();
            format.replace(index - 1, 2, expanded.empty() ? L"%I:%M %p" : expanded);
            index -= 1;
        } else if (specifier == L'T' || specifier == L'X') {
            format.replace(index - 1, 2, L"%H:%M");
        } else if (specifier == L'S' || ((specifier == L'E' || specifier == L'O') && index + 1 < format.size() &&
                                         format[index + 1] == L'S')) {
            const size_t start = index - 1;
            const size_t end = index + (specifier == L'S' ? 1 : 2);
            // The separator immediately before seconds belongs to that field.
            size_t separator = start;
            while (separator && format[separator - 1] != L'%' && !iswalnum_l(format[separator - 1], locale))
                --separator;
            format.erase(separator, end - separator);
            // Drop locale suffixes such as the Chinese seconds character.
            const size_t next = format.find(L'%', separator);
            const size_t literalEnd = next == std::wstring::npos ? format.size() : next;
            const auto literal = format.substr(separator, literalEnd - separator);
            if (literal.find_first_not_of(L" \t") != std::wstring::npos)
                format.replace(separator, literal.size(), next == std::wstring::npos ? L"" : L" ");
            index = separator;
        }
    }
    return format;
}
#endif

wxString LocalizedDateTime(const std::tm& time, bool shortDate, const wxString& format) {
    wchar_t output[256]{};
#ifdef __WXMSW__
    if (!format.empty()) {
        if (!std::wcsftime(output, std::size(output), format.wc_str(), &time)) return {};
    } else {
        const SYSTEMTIME local{static_cast<WORD>(time.tm_year + 1900), static_cast<WORD>(time.tm_mon + 1),
                               static_cast<WORD>(time.tm_wday),        static_cast<WORD>(time.tm_mday),
                               static_cast<WORD>(time.tm_hour),        static_cast<WORD>(time.tm_min),
                               static_cast<WORD>(time.tm_sec),         0};
        wchar_t date[128]{};
        wchar_t clock[128]{};
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, shortDate ? DATE_SHORTDATE : DATE_LONGDATE, &local, nullptr, date,
                        static_cast<int>(std::size(date)), nullptr);
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, clock,
                        static_cast<int>(std::size(clock)));
        return wxString(clock) + " " + date;
    }
#else
    locale_t locale = newlocale(LC_TIME_MASK | LC_CTYPE_MASK, "", nullptr);
    if (!locale) locale = newlocale(LC_TIME_MASK | LC_CTYPE_MASK, "C", nullptr);
    wxString pattern = format;
    if (pattern.empty()) {
        const auto dateFormat = shortDate
                                    ? wxString::FromUTF8(nl_langinfo_l(D_FMT, locale)).ToStdWstring()
                                    : LongDateFormat(wxString::FromUTF8(nl_langinfo_l(D_T_FMT, locale)).ToStdWstring());
        pattern = wxString(MinuteFormat(wxString::FromUTF8(nl_langinfo_l(T_FMT, locale)).ToStdWstring(), locale)) +
                  " " + wxString(dateFormat);
    }
    const auto length = wcsftime_l(output, std::size(output), pattern.wc_str(), &time, locale);
    freelocale(locale);
    if (!length) return {};
#endif
    return wxString(output);
}
} // namespace

DateTimeInsertionValue DateTimeInsertionClock() {
    DateTimeInsertionValue value{};
#ifdef __WXMSW__
    FILETIME time;
    GetSystemTimePreciseAsFileTime(&time);
    value.unixTicks =
        ((static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime) - UINT64_C(116444736000000000);
    SYSTEMTIME local;
    GetLocalTime(&local);
    value.local.tm_year = local.wYear - 1900;
    value.local.tm_mon = local.wMonth - 1;
    value.local.tm_mday = local.wDay;
    value.local.tm_wday = local.wDayOfWeek;
    value.local.tm_hour = local.wHour;
    value.local.tm_min = local.wMinute;
    value.local.tm_sec = local.wSecond;
    DYNAMIC_TIME_ZONE_INFORMATION zone;
    value.local.tm_isdst = static_cast<int>(GetDynamicTimeZoneInformation(&zone)) - 1;
    const auto date = std::chrono::year(local.wYear) / local.wMonth / local.wDay;
    value.local.tm_yday = static_cast<int>(
        (std::chrono::sys_days(date) - std::chrono::sys_days(std::chrono::year(local.wYear) / 1 / 1)).count());
#else
    using Tick = std::chrono::duration<uint64_t, std::ratio<1, 10000000>>;
    value.unixTicks = std::chrono::duration_cast<Tick>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto seconds = static_cast<time_t>(value.unixTicks / 10000000);
    localtime_r(&seconds, &value.local);
#endif
    return value;
}

wxString FormatDateTimeInsertion(DateTimeInsertion command, const DateTimeInsertionValue& value,
                                 const wxString& format) {
    uint64_t timestamp = value.unixTicks;
    switch (command) {
    case DateTimeInsertion::UnixSeconds:
        timestamp /= 10000000;
        break;
    case DateTimeInsertion::UnixMilliseconds:
        timestamp /= 10000;
        break;
    case DateTimeInsertion::UnixMicroseconds:
        timestamp /= 10;
        break;
    case DateTimeInsertion::UnixNanoseconds:
        timestamp *= 100;
        break;
    default:
        const auto seconds = static_cast<time_t>(value.unixTicks / 10000000);
        std::tm time = value.local;
#ifdef __WXMSW__
        if (command == DateTimeInsertion::UtcDateTime) gmtime_s(&time, &seconds);
#else
        if (command == DateTimeInsertion::UtcDateTime) gmtime_r(&seconds, &time);
#endif
        if (command == DateTimeInsertion::Short || command == DateTimeInsertion::Long)
            return LocalizedDateTime(time, command == DateTimeInsertion::Short, format);
        return wxString::Format(command == DateTimeInsertion::LocalDate     ? "%04d-%02d-%02d"
                                : command == DateTimeInsertion::UtcDateTime ? "%04d-%02d-%02dT%02d:%02d:%02dZ"
                                                                            : "%04d-%02d-%02d %02d:%02d:%02d",
                                time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min,
                                time.tm_sec);
    }
    return wxString::Format("%llu", static_cast<unsigned long long>(timestamp));
}

void EditorControl::InsertDateTime(DateTimeInsertion command, const wxString& format) {
    const auto bytes = FormatDateTimeInsertion(command, DateTimeInsertionClock(), format).ToStdString(wxConvUTF8);
    // Original Notepad4 calls SCI_REPLACESEL once, also for rectangles and multiple selections.
    Send(Scintilla::Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(bytes.c_str()));
}
