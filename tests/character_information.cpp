#include "editor_test_helpers.h"
#include <print>
#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "character_information_fixtures.h"
#include "encoded_file_helpers.h"
#include <wx/wx.h>
#include <wx/clipbrd.h>
#include <wx/stattext.h>
#include <wx/modalhook.h>
#include <algorithm>
#include <chrono>
#include <memory>
#ifdef __WXMSW__
#include "clipboard_read_error.h"
#endif

namespace {
using enum Scintilla::Message;
constexpr char PriorClipboardOffer[] = "owned previous offer";
#ifdef __WXMSW__
struct ClipboardObservation {
    HWND owner = nullptr;
    HWND openHolder = nullptr;
    DWORD ownerProcess = 0;
    DWORD ownerThread = 0;
    DWORD openHolderProcess = 0;
    DWORD openHolderThread = 0;
    DWORD sequence = 0;
    BOOL unicodeAvailable = false;
};
ClipboardObservation ObserveClipboard() {
    ClipboardObservation state;
    state.owner = GetClipboardOwner();
    state.ownerThread = GetWindowThreadProcessId(state.owner, &state.ownerProcess);
    state.openHolder = GetOpenClipboardWindow();
    state.openHolderThread = GetWindowThreadProcessId(state.openHolder, &state.openHolderProcess);
    state.sequence = GetClipboardSequenceNumber();
    state.unicodeAvailable = IsClipboardFormatAvailable(CF_UNICODETEXT);
    return state;
}
void ReportClipboard(const ClipboardObservation& state, const char* fixture, const char* phase) {
    std::fprintf(stderr,
                 "Character Info clipboard [%s] phase=%s owner=%p ownerPid=%lu ownerThread=%lu "
                 "openHolder=%p openHolderPid=%lu openHolderThread=%lu sequence=%lu unicodeAvailable=%d\n",
                 fixture, phase, static_cast<void*>(state.owner), state.ownerProcess, state.ownerThread,
                 static_cast<void*>(state.openHolder), state.openHolderProcess, state.openHolderThread, state.sequence,
                 state.unicodeAvailable);
}
bool ReadOwnedClipboard(wxTextDataObject& data, HWND editor, const ClipboardObservation& publication,
                        const char* fixture) {
    // This new local read-availability ceiling borrows SettleClipboard's
    // existing test policy. The original CTest 90-second guard is unchanged.
    constexpr auto ReadAvailabilityTimeout = std::chrono::milliseconds(250);
    constexpr auto ReadPollInterval = std::chrono::milliseconds(10);
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + ReadAvailabilityTimeout;
    const auto unchanged = [&](const ClipboardObservation& state) {
        return publication.owner == editor && publication.ownerProcess == GetCurrentProcessId() &&
               publication.unicodeAvailable && state.owner == publication.owner &&
               state.ownerProcess == publication.ownerProcess && state.sequence == publication.sequence &&
               state.unicodeAvailable;
    };
    unsigned attempts = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto before = ObserveClipboard();
        if (!unchanged(before)) {
            ReportClipboard(before, fixture, "changed-before-read");
            return false;
        }
        bool received = false, busyOnly = false, hasError = false;
        DWORD code = 0;
        {
            ClipboardReadErrors errors;
            if (std::chrono::steady_clock::now() >= deadline) return false;
            ++attempts;
            received = wxTheClipboard->GetData(data);
            busyOnly = errors.BusyOnly();
            hasError = errors.HasError();
            code = errors.LastCode();
        }
        const auto after = ObserveClipboard();
        const auto elapsed = std::chrono::steady_clock::now() - started;
        if (!received || hasError || attempts > 1) {
            std::fprintf(
                stderr,
                "Character Info clipboard [%s] attempt=%u received=%d busyOnly=%d error=0x%08lx "
                "elapsedMs=%lld\n",
                fixture, attempts, received, busyOnly, code,
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
            ReportClipboard(before, fixture, "read-attempt-before");
            ReportClipboard(after, fixture, "read-attempt-after");
            std::fflush(stderr);
        }
        if (!unchanged(after) || std::chrono::steady_clock::now() >= deadline) return false;
        if (received) return !hasError;
        if (!busyOnly) return false;
        wxYield();
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() > 0)
            wxMilliSleep(static_cast<unsigned long>(std::min(ReadPollInterval, remaining).count()));
    }
    return false;
}
const char* ClassifyClipboard(bool received, const wxString& text, const wxString& expected) {
    if (!received) return "read-failed";
    if (text == expected) return "expected";
    if (text == PriorClipboardOffer) return "owned-prior-offer";
    for (const auto& fixture : CharacterInfoFixtures) {
        const auto payload = wxString::FromUTF8(fixture.payload.data(), fixture.payload.size());
        const auto withoutMetadata = fixture.payload.substr(0, fixture.payload.find("\nName:"));
        if (text == payload || text == wxString::FromUTF8(withoutMetadata.data(), withoutMetadata.size()))
            return fixture.name;
    }
    return "other";
}
#endif
void Progress(const char* phase, const char* fixture = "suite") {
    std::println(stderr, "Character Info [{}]: {}", fixture, phase);
    std::fflush(stderr);
}
void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}
void ShowHex(EditorFrame* frame) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Show Hex Code");
    Require(id != wxNOT_FOUND, "Edit Special exposes original Show Hex Code");
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "AB", 0, 2);
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == "AB[41 42]", "Show Hex appends original selected byte representation");
    TestEditor::RequireSelection(editor, 2, 9, "Show Hex selects only appended representation");
    Require(editor->Send(GetModify), "Show Hex marks document modified");
    NativeSearch::Command(frame, wxID_UNDO);
    Require(TestEditor::Text(editor) == "AB" && !editor->Send(GetModify), "one Undo restores bytes and save point");
    NativeSearch::Command(frame, wxID_REDO);
    Require(TestEditor::Text(editor) == "AB[41 42]", "one Redo restores appended representation");
}
void Reset(EditorControl* editor, std::string_view text, intptr_t anchor, intptr_t caret) {
    TestEditor::ResetStream(editor, "");
    editor->Send(AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    editor->Send(SetSel, anchor, caret);
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
}
void HexSamples(EditorFrame* frame) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Show Hex Code");
    auto* editor = frame->Editor();
    for (const auto& fixture : ShowHexFixtures) {
        Reset(editor, fixture.before, fixture.anchor, fixture.caret);
        NativeSearch::Command(frame, id);
        Require(TestEditor::Text(editor) == fixture.after, "Show Hex matches independently captured original bytes");
        TestEditor::RequireSelection(editor, fixture.resultAnchor, fixture.resultCaret,
                                     "Show Hex selects independently captured result range");
        NativeSearch::Command(frame, wxID_UNDO);
        Require(TestEditor::Text(editor) == fixture.before && !editor->Send(GetModify),
                "one native Undo restores captured original bytes and save point");
        TestEditor::RequireSelection(editor, fixture.anchor, fixture.caret,
                                     "Undo restores original selection direction");
        NativeSearch::Command(frame, wxID_REDO);
        Require(TestEditor::Text(editor) == fixture.after && editor->Send(GetModify),
                "one native Redo restores captured output and modified state");
    }
    TestEditor::ResetStream(editor, "AB CD", 0, 2, Scintilla::EndOfLine::CrLf);
    editor->Send(SetMultipleSelection, 1);
    editor->Send(AddSelection, 5, 3);
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == "AB CD[41 42 0D 0A 43 44]" && editor->Send(GetSelections) == 1,
            "multiple selections use original copied EOL separator and insert once at original end");
    TestEditor::RequireSelection(editor, 5, 24, "multiple selection result matches original range");
    editor->Send(SetMultipleSelection, 0);
    TestEditor::ResetStream(editor, "A\nB", 0, 2);
    editor->Send(SetReadOnly, 1);
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == "A\nB" && !editor->Send(GetModify) && !editor->Send(CanUndo),
            "read-only Show Hex performs no edit and preserves history");
    TestEditor::RequireSelection(editor, 2, 3, "read-only command retains original clamped selection movement");
    TestEditor::ResetStream(editor, "A", 1, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(id), "Show Hex retains original empty-selection disablement");
    std::puts("PASS: original Show Hex Unicode/control/NUL, direction, multiple selection, read-only and history");
}
class RectangleWarning final : public wxTimer, public wxModalDialogHook {
public:
    RectangleWarning() {
        Register();
        Start(20);
    }
    int observed = 0;
    wxString message;
    int Enter(wxDialog* dialog) override {
        if (auto* warning = wxDynamicCast(dialog, wxMessageDialog)) message = warning->GetMessage();
        return wxID_NONE;
    }
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<RectangleWarning*>(data);
                wchar_t title[128]{};
                GetWindowTextW(window, title, 128);
                if (IsWindowVisible(window) && wxString(title) == "wxnotepad") {
                    driver.Stop();
                    ++driver.observed;
                    SendMessageW(window, TDM_CLICK_BUTTON, IDOK, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        auto* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget) || !GTK_IS_MESSAGE_DIALOG(widget)) continue;
            Stop();
            ++observed;
            gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_OK);
        }
        g_list_free(windows);
#endif
    }
};
void Rectangles(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Show Hex Code");
    TestEditor::ResetStream(editor, "A\nB");
    editor->Send(SetSelectionMode, std::to_underlying(Scintilla::SelectionMode::Rectangle));
    editor->Send(SetRectangularSelectionAnchor, 0);
    editor->Send(SetRectangularSelectionCaret, 3);
    RectangleWarning warning;
    NativeSearch::Command(frame, id);
    Require(warning.observed == 1 &&
                warning.message == "This operation can't be performed within a rectangular selection.",
            "native rectangle warning is actually displayed and accepted");
    Require(TestEditor::Text(editor) == "A\nB" && !editor->Send(GetModify) && !editor->Send(CanUndo) &&
                editor->Send(SelectionIsRectangle) && editor->Send(GetSelections) == 2,
            "rectangle rejection preserves original document, selections and history");
    TestEditor::RequireSelection(editor, 0, 3, "rectangle rejection retains original endpoints");
}
class Feedback final : public wxTimer {
public:
    Feedback() { Start(20); }
#ifdef __WXMSW__
    ClipboardObservation visibleClipboard;
#endif
    wxString payload;
    int shown = 0;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsShown() || !dialog->IsModal()) continue;
            auto* text = wxDynamicCast(dialog->FindWindowByName("Character information", dialog), wxStaticText);
            if (!text) continue;
            Stop();
            ++shown;
            payload = text->GetLabelText();
#ifdef __WXMSW__
            visibleClipboard = ObserveClipboard();
#endif
            NativeSearch::Click(dialog, wxID_OK);
            return;
        }
    }
};
void DisplayCharacterInfo(EditorFrame* frame, std::string_view captured, const char* fixture) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Show Character Info");
    Require(id != wxNOT_FOUND, "Edit Special exposes original Show Character Info");
    Progress("publish prior clipboard offer", fixture);
    {
        wxClipboardLocker lock;
        if (!lock) throw TestEditor::Failure("open native clipboard for owned fixture");
        Require(wxTheClipboard->SetData(new wxTextDataObject(PriorClipboardOffer)),
                "own a distinct native clipboard offer before the command");
    }
#ifdef __WXMSW__
    // Materialize our seed before testing Copy, so delayed rendering on this
    // same GUI thread cannot become part of the command's clipboard write.
    Require(wxTheClipboard->Flush(), "materialize the owned clipboard offer before the command");
    const auto seededClipboard = ObserveClipboard();
#endif
    Feedback feedback;
    Progress("dispatch menu command and accept native feedback", fixture);
    NativeSearch::Command(frame, id);
    if (wxGetEnv("WXNOTEPAD_EXPECT_NO_CHARACTER_METADATA", nullptr))
        captured = captured.substr(0, captured.find("\nName:"));
    const auto expected = wxString::FromUTF8(captured.data(), captured.size());
    Require(feedback.shown == 1 && feedback.payload == expected,
            "visible native feedback matches independently captured original technical payload");
    Progress("read copied payload from native clipboard", fixture);
    wxTextDataObject clipboard;
    {
        wxClipboardLocker lock;
        if (!lock) throw TestEditor::Failure("open native clipboard to observe command result");
#ifdef __WXMSW__
        const auto beforeRead = ObserveClipboard();
        const bool received = ReadOwnedClipboard(clipboard, static_cast<HWND>(frame->Editor()->GetHandle()),
                                                 feedback.visibleClipboard, fixture);
#else
        const bool received = wxTheClipboard->GetData(clipboard);
#endif
        const bool matches = received && clipboard.GetText() == expected;
#ifdef __WXMSW__
        const auto afterRead = ObserveClipboard();
        if (!matches) {
            std::fprintf(stderr,
                         "Character Info clipboard [%s] received=%d classification=%s expectedOwner=%p "
                         "expectedOwnerPid=%lu\n",
                         fixture, received, ClassifyClipboard(received, clipboard.GetText(), expected),
                         static_cast<void*>(frame->Editor()->GetHandle()), GetCurrentProcessId());
            ReportClipboard(seededClipboard, fixture, "owned-seed");
            ReportClipboard(feedback.visibleClipboard, fixture, "dialog-visible");
            ReportClipboard(beforeRead, fixture, "before-read");
            ReportClipboard(afterRead, fixture, "after-read");
            std::fflush(stderr);
        }
#endif
        Require(matches, "real system clipboard replaces previous offer with the same original technical payload");
    }
}
void CharacterInfo(EditorFrame* frame, const CharacterInfoFixture& fixture, bool readonly = false,
                   bool rectangle = false) {
    auto* editor = frame->Editor();
    Reset(editor, fixture.text, fixture.anchor, fixture.caret);
    editor->Send(SetReadOnly, readonly);
    if (rectangle) editor->Send(SetSelectionMode, std::to_underlying(Scintilla::SelectionMode::Rectangle));
    const auto selections = editor->Send(GetSelections);
    DisplayCharacterInfo(frame, fixture.payload, fixture.name);
    Require(TestEditor::Text(editor) == fixture.text && !editor->Send(GetModify) && !editor->Send(CanUndo),
            "Character Info leaves original document, save point and history unchanged");
    TestEditor::RequireSelection(editor, fixture.anchor, fixture.caret, "Character Info preserves selection direction");
    Require(editor->Send(GetSelections) == selections && bool(editor->Send(SelectionIsRectangle)) == rectangle &&
                bool(editor->Send(GetReadOnly)) == readonly,
            "Character Info remains available in original read-only and rectangular modes");
}
void EncodedDocuments(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("character-information-encoding");
    std::filesystem::create_directory(directory);
    const std::array<TestFiles::EncodedFixture, 4> fixtures{
        {{"utf8", FileEncoding::Utf8, "中", "中[E4 B8 AD]"},
         {"gbk", FileEncoding::Gbk, "\x{d6}\x{d0}", "\x{d6}\x{d0}[E4 B8 AD]"},
         {"windows1252", FileEncoding::Windows1252, "\x{e9}", "\x{e9}[C3 A9]"},
         {"utf16le", FileEncoding::Utf16Le, std::string("\x{ff}\x{fe}\x{2d}\x{4e}", 4),
          std::string("\x{ff}\x{fe}\x{2d}\x{4e}[\0E\0"
                      "4\0 \0B\0"
                      "8\0 \0A\0D\0]\0",
                      24)}}};
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Show Hex Code");
    for (const auto& fixture : fixtures) {
        editor->Send(SetReadOnly, 0);
        editor->Send(SetSavePoint);
        const auto path = directory / fixture.name;
        TestFiles::WriteBytes(path, fixture.before);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "explicit encoded document opens through application entrypoint");
        const auto text = TestEditor::Text(editor);
        const auto eol = editor->Send(GetEOLMode);
        const auto bom = frame->DocumentHasBom();
        editor->Send(SetSel, 0, editor->Send(GetLength));
        // Also frozen in original-03's explicit Windows-1252 sample.
        constexpr std::string_view acuteInformation =
            "Unicode: U+00E9\nUTF-16: 00E9\nUTF-8: C3 A9\nHTML: &#233;\n"
            "Name: LATIN SMALL LETTER E WITH ACUTE\nCategory: Ll Lowercase_Letter";
        DisplayCharacterInfo(
            frame, fixture.encoding == FileEncoding::Windows1252 ? acuteInformation : CharacterInfoFixtures[1].payload,
            fixture.name);
        Require(TestEditor::Text(editor) == text && !editor->Send(GetModify),
                "Character Info reports original decoded scalar without changing explicit-encoding document");
        TestEditor::RequireSelection(editor, 0, text.size(), "encoded Character Info preserves selection");
        NativeSearch::Command(frame, id);
        Require(frame->DocumentEncoding() == fixture.encoding && frame->DocumentHasBom() == bom &&
                    editor->Send(GetEOLMode) == eol,
                "Show Hex retains current encoding, BOM and EOL metadata");
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"Save writes original legacy/Unicode encoding bytes after Show Hex",
                                         "Undo Save restores original disk bytes",
                                         "Redo Save restores original representation bytes"});
    }
#ifdef __WXMSW__
    if (GetACP() == 936) {
        const auto path = directory / "native-ansi";
        TestFiles::WriteBytes(path, "é中");
        editor->Send(SetSavePoint);
        Require(frame->OpenRestartDocument(TestFiles::Filename(path), NativeEncodingChoice(), std::nullopt, false, 0, 0)
                        .status == OpenStatus::Opened,
                "original ANSI startup route establishes native encoding identity");
        editor->Send(SetSel, 0, editor->Send(GetLength));
        NativeSearch::Command(frame, id);
        Require(TestEditor::Text(editor) == "é中[A8 A6 D6 D0]",
                "native ANSI identity shows independently captured ACP bytes instead of UTF-8 source bytes");
        NativeSearch::Command(frame, wxID_UNDO);
    }
#endif
    std::filesystem::remove_all(directory);
    std::puts("PASS: original UTF-8, explicit GBK/Windows-1252 and UTF-16 editor-byte interpretation and disk bytes");
}
class CharacterInformationApp final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        // A clipboard error queued by wxLogGui can open a second modal
        // dialog at shutdown. Keep the original error in the CTest output.
        std::unique_ptr<wxLog> previousLog{wxLog::SetActiveTarget(new wxLogStderr)};
        previousLog.reset();
#endif
        Progress("create and present native frame");
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native frame presentation completed");
                Progress("Show Hex fixtures");
                ShowHex(frame);
                HexSamples(frame);
                Progress("accept native rectangle warning");
                Rectangles(frame);
                Progress("prepare native clipboard input");
                // A native modifier gesture establishes the private Wayland
                // input serial before changing system clipboard ownership.
                NativeKeys::Shortcut(frame, WXK_F10, false);
                NativeKeys::Shortcut(frame, WXK_F10, false);
                for (const auto& fixture : CharacterInfoFixtures)
                    CharacterInfo(frame, fixture);
                CharacterInfo(frame, CharacterInfoFixtures.front(), true);
                CharacterInfo(frame, CharacterInfoFixtures.front(), false, true);
                CharacterInfo(frame, {"empty", "", 0, 0, CharacterInfoFixtures[4].payload});
                Reset(frame->Editor(), "A", 0, 1);
                frame->Editor()->Send(InsertText, 1, reinterpret_cast<intptr_t>("!"));
                frame->Editor()->Send(Undo);
                frame->Editor()->Send(SetSel, 1, 0);
                DisplayCharacterInfo(frame, CharacterInfoFixtures.front().payload, "pending redo");
                Require(frame->Editor()->Send(CanRedo) && !frame->Editor()->Send(CanUndo) &&
                            !frame->Editor()->Send(GetModify),
                        "Character Info preserves pending redo and save point");
                TestEditor::RequireSelection(frame->Editor(), 1, 0,
                                             "Character Info preserves pending history selection");
                EncodedDocuments(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::println(stderr, "FAIL: {}", error.what());
            }
            Progress("destroy native frame");
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(CharacterInformationApp);
