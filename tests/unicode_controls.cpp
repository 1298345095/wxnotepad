#include "diagnostics/print.h"
#include "native_search_helpers.h"
#include "native_fold_helpers.h"
#include "settings/settings_store.h"
#include "encoded_file_helpers.h"
#include "native_renderer_helpers.h"
#include "native_key_helpers.h"
#include <array>
#include <string>
#include <tuple>
#include <cstdio>
#include <print>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
// Exact independent display expectations from Notepad4 Edit.cpp at 89b8b36e.
struct Representation {
    const char* utf8;
    const char* name;
    bool builtIn = false;
};
constexpr std::array<Representation, 27> OriginalRepresentations{{
    {"\xe2\x80\x8e", "LRM"}, {"\xe2\x80\x8f", "RLM"},      {"\xe2\x80\x8d", "ZWJ"},      {"\xe2\x80\x8c", "ZWNJ"},
    {"\xe2\x80\xaa", "LRE"}, {"\xe2\x80\xab", "RLE"},      {"\xe2\x80\xad", "LRO"},      {"\xe2\x80\xae", "RLO"},
    {"\xe2\x80\xac", "PDF"}, {"\xe2\x81\xae", "NADS"},     {"\xe2\x81\xaf", "NODS"},     {"\xe2\x81\xab", "ASS"},
    {"\xe2\x81\xaa", "ISS"}, {"\xe2\x81\xad", "AAFS"},     {"\xe2\x81\xac", "IAFS"},     {"\x1e", "RS", true},
    {"\x1f", "US", true},    {"\xe2\x80\xa8", "LS", true}, {"\xe2\x80\xa9", "PS", true}, {"\xe2\x80\x8b", "ZWSP"},
    {"\xe2\x81\xa0", "WJ"},  {"\xe2\x81\xa6", "LRI"},      {"\xe2\x81\xa7", "RLI"},      {"\xe2\x81\xa8", "FSI"},
    {"\xe2\x81\xa9", "PDI"}, {"\xd8\x9c", "ALM"},          {"\xc2\xad", "SHY"},
}};
std::string DisplayName(EditorControl* editor, const char* utf8) {
    char name[16]{};
    editor->Send(Scintilla::Message::GetRepresentation, reinterpret_cast<uintptr_t>(utf8),
                 reinterpret_cast<intptr_t>(name));
    return name;
}
auto DocumentState(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    return std::tuple(NativeSearch::Text(editor), editor->Send(GetAnchor), editor->Send(GetCurrentPos),
                      editor->Send(GetModify), editor->Send(CanUndo), editor->Send(CanRedo), frame->DocumentEncoding(),
                      frame->DocumentHasBom());
}
void Table(EditorFrame* frame, int command) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor,
                            "A\xe2\x81\xa0"
                            "B",
                            4, 1);
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Undo);
    const auto before = DocumentState(frame);
    for (const auto& entry : OriginalRepresentations) {
        if (DisplayName(editor, entry.utf8) != entry.name) {
            std::println(stderr, "Representation {}: '{}'", entry.name, DisplayName(editor, entry.utf8).c_str());
            Require(false, "display uses every exact original table name and retains the four core representations");
        }
    }
    NativeFold::Paint(editor);
    const auto visibleWidth = editor->Send(PointXFromPosition, 0, 4) - editor->Send(PointXFromPosition, 0, 1);
    Require(visibleWidth > 0, "the named representation occupies painted editor space");
    NativeSearch::Command(frame, command);
    NativeFold::Paint(editor);
    for (const auto& entry : OriginalRepresentations)
        Require(DisplayName(editor, entry.utf8) == (entry.builtIn ? entry.name : ""),
                "hiding clears original overrides while retaining RS/US/LS/PS core representations");
    const auto hiddenWidth = editor->Send(PointXFromPosition, 0, 4) - editor->Send(PointXFromPosition, 0, 1);
    std::println(stderr, "Extent: visible={:d} hidden={:d}; state_equal={:d}", static_cast<long long>(visibleWidth),
                 static_cast<long long>(hiddenWidth), DocumentState(frame) == before);
    Require(visibleWidth > hiddenWidth && DocumentState(frame) == before,
            "show/hide changes painted representation extent while preserving text, selection, save point and Redo");
    editor->Send(Redo);
    Require(NativeSearch::Text(editor) == "A\xe2\x81\xa0"
                                          "B!",
            "representation toggles preserve pending Redo");
    editor->Send(Undo);
    Require(!editor->Send(GetModify), "Undo returns to the original save point");
    NativeSearch::Command(frame, command);
    std::puts("PASS: exact original representation table, core exclusions, painted extent and unchanged history");
}
void Geometry(EditorFrame* frame, int command) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    frame->SetSize(360, 300);
    TestEditor::ResetStream(editor,
                            "A\xe2\x81\xa0"
                            "B",
                            5, 5);
    NativeFold::Paint(editor);
    const int left = static_cast<int>(editor->Send(PointXFromPosition, 0, 1));
    const int right = static_cast<int>(editor->Send(PointXFromPosition, 0, 4));
    const int top = static_cast<int>(editor->Send(PointYFromPosition, 0, 1));
    const int height = static_cast<int>(editor->Send(TextHeight, 0));
    Require(right > left + 2, "visible WJ has an on-screen representation box");
    const wxRect area(left, top, right - left, height);
    const auto visible = NativePaint::Read(editor, area);
    Require(editor->Send(PositionFromPointClose, left + 1, top + height / 2) == 1 &&
                editor->Send(PositionFromPointClose, right - 1, top + height / 2) == 4,
            "hit-testing the representation returns character boundaries, never a UTF-8 continuation byte");
    NativeSearch::Command(frame, command);
    NativeFold::Paint(editor);
    Require(NativePaint::Read(editor, area) != visible,
            "the named representation changes actual native widget pixels when hidden");
    NativeSearch::Command(frame, command);
    std::string line;
    for (int repeat = 0; repeat < 80; ++repeat)
        line += "a\xe2\x81\xa0"
                "b ";
    TestEditor::ResetStream(editor, line.c_str());
    const int wrap = frame->GetMenuBar()->FindMenuItem("View", "Word Wrap");
    NativeSearch::Command(frame, wrap);
    for (const auto zoom : {100, 150}) {
        editor->Send(SetZoom, zoom);
        NativeFold::Paint(editor);
        const auto visibleRows = editor->Send(WrapCount, 0);
        const auto before = DocumentState(frame);
        NativeSearch::Command(frame, command);
        NativeFold::Paint(editor);
        Require(visibleRows > editor->Send(WrapCount, 0) && DocumentState(frame) == before,
                "representation widths reflow wrapped rows at normal and enlarged zoom without changing the document");
        NativeSearch::Command(frame, command);
        NativeFold::Paint(editor);
        Require(editor->Send(WrapCount, 0) == visibleRows, "showing names restores the original wrapped layout");
    }
    NativeSearch::Command(frame, wrap);
    editor->Send(SetZoom, 100);
    editor->Send(SetXOffset, 100);
    NativeFold::Paint(editor);
    Require(editor->Send(GetXOffset) > 0, "unwrapped representations participate in horizontal scrolling");
    TestEditor::RequireVirtualRectangleUnchanged(
        editor,
        [frame, command] {
            NativeSearch::Command(frame, command);
            NativeSearch::Command(frame, command);
        },
        "representation toggles preserve rectangular selections and virtual space");
    std::puts(
        "PASS: actual representation pixels, hit-testing, wrapping, zoom, horizontal scroll and rectangular selection");
}
void Lifecycle(EditorFrame* frame, int command) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const auto native = NativeEncodingChoice();
    Require(native.has_value(), "the supported host exposes its native ANSI encoding");
    TestEditor::ResetStream(editor, "");
    const int ansi = frame->GetMenuBar()->FindMenuItem("File", "ANSI");
    Require(ansi != wxNOT_FOUND, "File exposes the original ANSI encoding command");
    NativeSearch::Command(frame, ansi);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    auto* item = frame->GetMenuBar()->FindItem(command);
    Require(!item->IsEnabled() && item->IsChecked() && editor->Send(GetCodePage) == Scintilla::CpUtf8 &&
                DisplayName(editor, "\xe2\x81\xa0").empty(),
            "logical ANSI disables the command and names while retaining the checked preference and UTF-8 core");
    const auto ansiState = DocumentState(frame);
    // A disabled GTK widget cannot be activated by a user. Dispatch the command
    // event directly to verify the handler's guard without forcing the native
    // checkbox into a state that bypasses its disabled menu contract.
    wxCommandEvent disabledCommand(wxEVT_MENU, command);
    frame->GetEventHandler()->ProcessEvent(disabledCommand);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(DocumentState(frame) == ansiState && item->IsChecked(),
            "an unavailable display command cannot change the document encoding or saved preference");
    Require(frame->SelectEncoding(EncodingChoice(native->encoding)),
            "select the same concrete codec without ANSI identity");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(item->IsEnabled() && item->IsChecked() && frame->DocumentEncoding() == native->encoding &&
                DisplayName(editor, "\xe2\x81\xa0") == "WJ",
            "the same concrete codec is eligible when its logical identity is explicit rather than native ANSI");
    if (const auto oem = NativeOemEncodingChoice()) {
        frame->SelectEncoding(*oem);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(item->IsEnabled() && DisplayName(editor, "\xe2\x81\xa0") == "WJ",
                "native OEM retains the original non-ANSI eligibility");
    }
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "UTF-8"));
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-unicode-controls");
    std::filesystem::create_directory(directory);
    const std::string utf8 = "A\xe2\x81\xa0"
                             "B\xe2\x80\x8e"
                             "C";
    const std::string utf16le("A\0\x60\x20"
                              "B\0\x0e\x20"
                              "C\0",
                              10);
    const std::string utf16be("\0A\x20\x60\0B\x20\x0e\0C", 10);
    const std::array<TestFiles::EncodedFixture, 6> fixtures{{
        {"utf8.txt", FileEncoding::Utf8, utf8, utf8 + "!"},
        {"utf8bom.txt", FileEncoding::Utf8, "\xef\xbb\xbf" + utf8, "\xef\xbb\xbf" + utf8 + "!"},
        {"utf16le.txt", FileEncoding::Utf16Le, "\xff\xfe" + utf16le, "\xff\xfe" + utf16le + std::string("!\0", 2)},
        {"utf16be.txt", FileEncoding::Utf16Be, "\xfe\xff" + utf16be, "\xfe\xff" + utf16be + std::string("\0!", 2)},
        {"gbk.txt", FileEncoding::Gbk,
         "A\xd6\xd0"
         "B",
         "A\xd6\xd0"
         "B!"},
        {"western.txt", FileEncoding::Windows1252,
         "A\xad"
         "B",
         "A\xad"
         "B!"},
    }};
    for (const auto& fixture : fixtures) {
        const auto path = directory / fixture.name;
        TestFiles::WriteBytes(path, fixture.before);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "Open loads the independently encoded fixture");
        Require(item->IsEnabled() && item->IsChecked() && DisplayName(editor, "\xe2\x81\xa0") == "WJ",
                "Open reapplies the saved preference for every eligible encoding");
        editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Undo);
        editor->Send(SetSel, static_cast<uintptr_t>(editor->Send(GetLength)), 0);
        const auto before = DocumentState(frame);
        NativeSearch::Command(frame, command);
        NativeSearch::Command(frame, command);
        NativeFold::Paint(editor);
        Require(DocumentState(frame) == before && frame->Save().status == SaveStatus::Saved &&
                    TestFiles::Bytes(path) == fixture.before && editor->Send(CanRedo),
                "display toggles preserve encoded disk bytes and pending Redo across Save");
        editor->Send(Redo);
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"representations retain saved edits", "Undo writes the original bytes",
                                         "Redo writes the independent expected bytes"});
        Require(frame->ReloadCurrentFile().status == OpenStatus::Opened && DisplayName(editor, "\xe2\x81\xa0") == "WJ",
                "Reload restores the document while retaining visible names");
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened &&
                    DisplayName(editor, "\xe2\x81\xa0") == "WJ",
                "save/reopen retains the preference and representation table");
    }
    const auto path = directory / "restart.txt";
    TestFiles::WriteBytes(path, "abc");
    Require(frame->OpenRestartDocument(TestFiles::Filename(path), native, {}, false, 1, 1).status ==
                    OpenStatus::Opened &&
                !item->IsEnabled() && item->IsChecked() && DisplayName(editor, "\xe2\x81\xa0").empty(),
            "restart loading uses the final logical ANSI identity after automatic decoding");
    Require(frame->OpenRestartDocument(TestFiles::Filename(path), EncodingChoice(FileEncoding::Utf8), {}, false, 1, 1)
                        .status == OpenStatus::Opened &&
                item->IsEnabled() && DisplayName(editor, "\xe2\x81\xa0") == "WJ",
            "restart loading an eligible encoding restores the retained preference");
    Require(frame->NewDocument() && DisplayName(editor, "\xe2\x81\xa0") == "WJ",
            "New retains the display preference under its effective default encoding");
    const int insertion = frame->GetMenuBar()->FindMenuItem("Edit", wxString::FromUTF8("WJ — &Word joiner"));
    Require(insertion != wxNOT_FOUND, "the delivered insertion command uses the common control-character table");
    NativeSearch::Command(frame, insertion);
    Require(NativeSearch::Text(editor) == "\xe2\x81\xa0" && DisplayName(editor, "\xe2\x81\xa0") == "WJ",
            "native insertion writes the original character while the shared table displays its original name");
    editor->Send(Undo);
    std::filesystem::remove_all(directory);
    std::puts("PASS: logical ANSI/explicit/OEM encoding, Open/Reload/New/restart, shared insertion and encoded "
              "Save/Undo/Redo bytes");
}
class UnicodeControlsApp final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_UNICODE_DPI96"))
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "create the complementary owned 96-DPI representation window");
#endif
        settings_.values = OriginalSettings{};
        settings_.values.wordWrap = false;
        auto* frame = new EditorFrame(&settings_);
        TestEditor::UseFixedWidthFont(frame->Editor());
        TestRenderer::Select(frame->Editor());
        std::println("Native representation DPI: {:d} x {:d}", frame->Editor()->GetDPI().x,
                     frame->Editor()->GetDPI().y);
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_UNICODE_DPI96"))
            Require(frame->Editor()->GetDPI().y == 96, "the complementary native representation fixture uses 96 DPI");
#endif
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "the owned native editor is presented before representation painting");
                auto* menu = frame->GetMenuBar();
                const int id = menu->FindMenuItem("View", "Unicode Control Character");
                Require(id != wxNOT_FOUND, "View exposes Unicode Control Character");
                auto* item = menu->FindItem(id);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(item->IsCheckable() && !item->IsChecked() && item->IsEnabled(),
                        "original Unicode control display defaults to unchecked and enabled for UTF-8");
                NativeSearch::Command(frame, id);
                Require(item->IsChecked(), "native menu enables the representation preference");
                std::puts("PASS: original native Unicode control display command and default");
                Table(frame, id);
                Geometry(frame, id);
                Lifecycle(frame, id);
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                result_ = 1;
            }
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
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(UnicodeControlsApp);
