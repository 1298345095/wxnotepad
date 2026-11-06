# Original date/time insertion

Edit > Insert restores the nine date/time entries from Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. The menu labels, Ctrl+F5 short-form
shortcut and Ctrl+Shift+F5 long-form shortcut follow `src/Notepad4.rc`.
The commands remain enabled on read-only documents, matching the original
native routing; the original core decides the resulting edit and selection.

`Notepad4.cpp`'s local date, local datetime and UTC branches supply
`YYYY-MM-DD`, `YYYY-MM-DD HH:MM:SS` and `YYYY-MM-DDTHH:MM:SSZ` respectively.
Unix timestamp commands retain its unsigned conversion from 100ns ticks:
divide by 10,000,000 / 10,000 / 10 for seconds / milliseconds / microseconds,
or multiply by 100 for nanoseconds. Nanosecond output therefore ends in two
zeroes. Windows uses the original precise FILETIME clock and epoch offset.
Linux samples its system clock and truncates to the same 100ns tick boundary.

`Edit.cpp::EditInsertDateTime` supplies the two local time/date forms and
`[Settings2] DateTimeShort` / `DateTimeLong`. Windows retains its
`GetLocalTime`, `GetDateFormatEx` and `GetTimeFormatEx(TIME_NOSECONDS)` paths,
including user locale overrides. Custom templates use the original wide CRT
formatter. Windows local time follows the operating system timezone, including
when a process sets the CRT `TZ` variable.

Linux uses its POSIX `LC_TIME` locale and local timezone. Custom templates use
the host wide locale formatter. The default short date comes from `D_FMT`;
the long date is the date portion of `D_T_FMT`, removing time fields and their
unit suffixes. The default time retains `T_FMT`'s hour cycle and AM/PM marker
while omitting its seconds field. Linux and Windows locale databases can differ
in date order, weekday/month names, padding, punctuation and supported CRT
directives. These are host-locale results, while the three fixed formats and
timestamp unit conversions are shared. Missing Linux locales fall back to C.

All commands convert the generated Unicode text into the document's UTF-8
editing representation and call the pinned core's `SCI_REPLACESEL` once.
That core removes all selected ranges, inserts once at the main caret, then
collapses to one caret. Rectangular selections follow that same operation.
Save continues to use the existing selected file encoder and conversion warning.

The approved formatting seam accepts a fixed 100ns timestamp and local calendar
fields. `original_date_time_literals` checks independent literal expectations
for UTC-8 date rollover, the epoch, all four units and Unicode templates.
The same executable's `--chinese` fixture checks literal Chinese host-locale
default dates, time suffixes and month/weekday names when run with a private
`zh_CN.UTF-8` locale. Its ordinary CTest case fixes `LC_ALL=C.UTF-8` on Linux.

`original_date_time_insertion` drives all nine native menu entries and the two
native shortcuts, checking independently sampled clock intervals. It also checks
empty, reversed, multiple and rectangular selections, UTF-8 caret positions,
read-only protection and grouped Undo/Redo. Actual Open/Save commands produce
independently expected UTF-8, UTF-16LE and CP936 disk bytes, including Undo/Redo
saves and cancellation of an unrepresentable CP936 insertion.
`original_date_time_settings` launches separate application processes and checks
both imported fields, missing/empty/quoted defaults, the original UTF-16 input
limit and load-only preservation after explicit and exit saving.
