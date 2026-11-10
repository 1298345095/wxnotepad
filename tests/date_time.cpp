#include "native_key_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <wx/datetime.h>
#include <ctime>
#include <cstring>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Menu(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    Require(id != wxNOT_FOUND, "original date/time command exists in the native Insert menu");
    NativeSearch::Command(frame, id);
}
void History(EditorControl* editor, const char* before, const char* after) {
    Require(TestEditor::Text(editor) == after && editor->Send(Message::GetModify), "insertion replaces selected bytes");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == before && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
            "one Undo restores original bytes and save point");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == after, "one Redo restores inserted bytes");
}
void SelectionCases(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto text = wxString::FromUTF8("时刻😀");
    for (const auto& range : {std::pair{1, 3}, std::pair{3, 1}, std::pair{1, 1}}) {
        TestEditor::ResetStream(editor, "abcd", range.first, range.second);
        editor->InsertDateTime(DateTimeInsertion::Short, text);
        const char* expected = range.first == range.second ? "a时刻😀bcd" : "a时刻😀d";
        TestEditor::RequireSelection(editor, 11, 11, "replacement collapses to the inserted UTF-8 end");
        History(editor, "abcd", expected);
    }
    TestEditor::ResetStream(editor, "abcd\nefgh", 1, 2);
    editor->Send(Message::AddSelection, 7, 6);
    editor->InsertDateTime(DateTimeInsertion::Short, text);
    Require(editor->Send(Message::GetSelections) == 1, "ReplaceSel collapses multiple ranges to one caret");
    History(editor, "abcd\nefgh", "acd\ne时刻😀gh");
    TestEditor::ResetStream(editor, "abcd\nefgh");
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 7);
    editor->InsertDateTime(DateTimeInsertion::Short, text);
    History(editor, "abcd\nefgh", "acd\ne时刻😀gh");
    TestEditor::ResetStream(editor, "abcd", 3, 1);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 3, 1);
    editor->Send(Message::SetReadOnly, 1);
    Menu(frame, "Time/Date (Short Form)");
    Require(TestEditor::Text(editor) == "abcd" && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
            "native read-only invocation retains original bytes and history");
    TestEditor::RequireSelection(editor, 1, 1, "read-only ReplaceSel retains original selection-collapse behavior");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", "Time/Date (Short Form)")),
            "original insertion menu remains enabled on read-only text");
    editor->Send(Message::SetReadOnly, 0);
    Require(editor->Send(Message::CanRedo), "releasing read-only mode restores pending redo availability");
    std::puts("PASS: empty/reversed/multiple/rectangular selections, Unicode, read-only and grouped history");
}
uint64_t NanosecondsNow() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
void WithinClock(const std::string& text, const char* prefix, bool utc, uint64_t before, uint64_t after) {
    Require(text.starts_with(prefix) && text.size() == std::strlen(prefix) + (utc ? 20 : 19) &&
                (!utc || text.back() == 'Z'),
            "native insertion uses exactly the original text format and configured prefix");
    std::tm calendar{};
    const auto value = text.substr(std::strlen(prefix));
    Require(std::sscanf(value.c_str(), utc ? "%d-%d-%dT%d:%d:%dZ" : "%d-%d-%d %d:%d:%d", &calendar.tm_year,
                        &calendar.tm_mon, &calendar.tm_mday, &calendar.tm_hour, &calendar.tm_min,
                        &calendar.tm_sec) == 6,
            "native insertion has the original fixed datetime shape");
    calendar.tm_year -= 1900;
    --calendar.tm_mon;
    calendar.tm_isdst = -1;
    time_t seconds;
    if (utc) {
#ifdef __WXMSW__
        seconds = _mkgmtime(&calendar);
#else
        seconds = timegm(&calendar);
#endif
    } else {
        seconds = std::mktime(&calendar);
    }
    Require(static_cast<uint64_t>(seconds) >= before / 1000000000 &&
                static_cast<uint64_t>(seconds) <= after / 1000000000,
            "native insertion represents a clock value inside the sampled interval");
}
void LiveCommands(EditorFrame* frame) {
    struct Command {
        const char* label;
        uint64_t divisor;
    };
    const Command commands[] = {{"Current Date", 0},           {"Current Date Time", 0},
                                {"UTC Date Time", 0},          {"Unix Timestamp", 1000000000},
                                {"Millisecond (ms)", 1000000}, {"Microsecond (us)", 1000},
                                {"Nanosecond (ns)", 1},        {"Time/Date (Short Form)", 0},
                                {"Time/Date (Long Form)", 0}};
    for (const auto& command : commands) {
        TestEditor::ResetStream(frame->Editor(), "");
        const auto before = NanosecondsNow();
        Menu(frame, command.label);
        const auto after = NanosecondsNow();
        const auto text = TestEditor::Text(frame);
        if (command.divisor) {
            const auto timestamp = std::stoull(text);
            // The original clock first truncates to 100ns ticks.
            Require(timestamp >= (before / 100 * 100) / command.divisor && timestamp <= after / command.divisor,
                    "native timestamp falls within independent system-clock samples in the selected unit");
            if (command.divisor == 1) Require(timestamp % 100 == 0, "original nanoseconds retain 100ns precision");
        } else if (std::strcmp(command.label, "Current Date") == 0) {
            const auto first = wxDateTime(static_cast<time_t>(before / 1000000000)).FormatISODate().ToStdString();
            const auto last = wxDateTime(static_cast<time_t>(after / 1000000000)).FormatISODate().ToStdString();
            Require(text == first || text == last, "native date falls within the sampled local date interval");
        } else {
            const bool shortDate = std::strcmp(command.label, "Time/Date (Short Form)") == 0;
            const bool longDate = std::strcmp(command.label, "Time/Date (Long Form)") == 0;
            const bool utc = std::strcmp(command.label, "UTC Date Time") == 0;
            WithinClock(text, shortDate ? "short:" : longDate ? "long:" : "", utc, before, after);
        }
        frame->Editor()->Send(Message::Undo);
        Require(TestEditor::Text(frame).empty() && !frame->Editor()->Send(Message::GetModify),
                "each native insertion is one undoable action");
    }
    for (const bool shift : {false, true}) {
        TestEditor::ResetStream(frame->Editor(), "");
        const auto before = NanosecondsNow();
        NativeKeys::Shortcut(frame, WXK_F5, true, false, shift);
        WithinClock(TestEditor::Text(frame), shift ? "long:" : "short:", false, before, NanosecondsNow());
    }
    const auto text = TestEditor::Text(frame);
    NativeKeys::Shortcut(frame, WXK_F5, true, true);
    Require(TestEditor::Text(frame) == text, "extra Alt does not fall through to Ctrl+F5");
    std::puts("PASS: all nine native insertion menus and Ctrl+F5/Ctrl+Shift+F5 use sampled clock intervals");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("date-time");
    std::filesystem::create_directory(directory);
    const TestFiles::EncodedFixture fixtures[] = {{"utf8.txt", FileEncoding::Utf8, "AoldZ", "A时刻Z"},
                                                  {"utf16.txt", FileEncoding::Utf16Le,
                                                   std::string("\xff\xfe\x41\0o\0l\0d\0Z\0", 12),
                                                   std::string("\xff\xfe\x41\0\xf6\x65\x3b\x52Z\0", 10)},
                                                  {"cp936.txt", FileEncoding::Gbk, "AoldZ", "A\xca\xb1\xbf\xccZ"}};
    for (const auto& fixture : fixtures) {
        const auto path = directory / fixture.name;
        TestFiles::WriteBytes(path, fixture.before);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "encoded insertion fixture opens through the actual file command");
        editor->Send(Message::SetSel, 1, 4);
        editor->InsertDateTime(DateTimeInsertion::Short, wxString::FromUTF8("时刻"));
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"Save writes independent encoded insertion bytes",
                                         "Undo Save restores source bytes",
                                         "Redo Save restores independent encoded insertion bytes"});
    }
    const auto path = directory / "cp936.txt";
    editor->Send(Message::SetSavePoint);
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
            "CP936 warning fixture reopens");
    editor->Send(Message::SetSel, 1, 7);
    editor->InsertDateTime(DateTimeInsertion::Short, wxString::FromUTF8("😀"));
    const auto unchanged = TestFiles::Bytes(path);
    Require(frame->Save([] { return EncodingLossChoice::Cancel; }).status == SaveStatus::Cancelled &&
                TestFiles::Bytes(path) == unchanged && editor->Send(Message::GetModify),
            "unrepresentable insertion retains edit and original disk bytes on encoding-warning cancellation");
    editor->Send(Message::SetSavePoint);
    std::filesystem::remove_all(directory);
    std::puts("PASS: UTF-8/UTF-16LE/CP936 saved bytes, Undo/Redo saves and existing lossy-save cancellation");
}
class DateTimeApp final : public wxApp {
public:
    bool OnInit() override {
        settings_.values.dateTimeShort = "short:%Y-%m-%d %H:%M:%S";
        settings_.values.dateTimeLong = "long:%Y-%m-%d %H:%M:%S";
        auto* frame = new EditorFrame(&settings_);
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                SelectionCases(frame);
                LiveCommands(frame);
                SavedBytes(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Editor()->Send(Message::SetSavePoint);
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    SettingsStore settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(DateTimeApp);
