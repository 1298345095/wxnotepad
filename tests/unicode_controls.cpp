#include "diagnostics/print.h"
#include "editor/editor_core.h"
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
#include <exception>
#include <optional>
#include <print>
#include <stdexcept>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

// The existing friend permits test-owned, side-effect-free field observations.
// No geometry getter, font measurement, core message or event pump runs here.
struct EditorCoreObservation {
    struct Snapshot {
        int wrapWidth;
        Scintilla::Internal::PRectangle rcPaint;
        int textStart;
        int rightMarginWidth;
        int lineHeight;
        intptr_t wrapStart;
        intptr_t wrapEnd;
        bool needsWrap;
        int paintState;
        bool paintingAllText;
    };
    static Snapshot Capture(const EditorControl& editor) noexcept {
        const auto& core = *editor.core_;
        return {core.wrapWidth,
                core.rcPaint,
                core.vs.textStart,
                core.vs.rightMarginWidth,
                core.vs.lineHeight,
                core.wrapPending.start,
                core.wrapPending.end,
                core.wrapPending.NeedsWrap(),
                static_cast<int>(core.paintState),
                core.paintingAllText};
    }
};

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
enum class ObservationMode { None, Cached, Measuring };
class GeometryDiagnostics {
public:
    GeometryDiagnostics(EditorFrame* frame, bool minimal, ObservationMode observation)
        : frame_(frame), minimal_(minimal), observation_(observation) {}
    ~GeometryDiagnostics() {
        const bool failed = std::uncaught_exceptions() != 0;
        const char* observation = observation_ == ObservationMode::None        ? "none"
                                  : observation_ == ObservationMode::Measuring ? "measuring"
                                                                               : "cached";
        std::fprintf(stderr, "[DEBUG-unicode-275] outcome=%s mode=%s observation=%s samples=%zu",
                     failed ? "fail" : "pass", minimal_ ? "minimal" : "full", observation, count_);
        for (size_t index = 0; index < count_; ++index) {
            const auto& sample = samples_[index];
            if (sample.rows >= 0) {
                std::fprintf(stderr, " %s_z%d=%lld", sample.phase, sample.zoom, static_cast<long long>(sample.rows));
                if (observation_ == ObservationMode::Measuring)
                    std::fprintf(stderr, "@measured:%dx%d", sample.editorClientMeasured.x,
                                 sample.editorClientMeasured.y);
#ifdef __WXGTK__
                if (observation_ != ObservationMode::None)
                    std::fprintf(stderr, "@cache:%dx%d,valid:%d,gtk:%dx%d", sample.editorClientCache.x,
                                 sample.editorClientCache.y, sample.editorClientValid, sample.gtkEditorClient.width,
                                 sample.gtkEditorClient.height);
#endif
            }
        }
        if (count_) PrintGeometry(samples_[count_ - 1]);
        std::fputc('\n', stderr);
        if (failed) {
            for (size_t index = 0; index < count_; ++index) {
                const auto& sample = samples_[index];
                std::fprintf(stderr, "[DEBUG-unicode-275] phase=%s zoom=%d rows=%lld", sample.phase, sample.zoom,
                             static_cast<long long>(sample.rows));
                PrintGeometry(sample);
                std::fputc('\n', stderr);
            }
        }
        for (size_t index = 0; index < count_; ++index) {
            const auto& sample = samples_[index];
            if (sample.coreBefore) {
                PrintCore(sample, "before", *sample.coreBefore);
                PrintCore(sample, "after", *sample.coreAfter);
            }
        }
        std::fflush(stderr);
    }
    std::optional<EditorCoreObservation::Snapshot> BeforeWrapCount() const noexcept {
        if (observation_ == ObservationMode::None) return std::nullopt;
        return EditorCoreObservation::Capture(*frame_->Editor());
    }
    void Observe(const char* phase, int zoom = 0, intptr_t rows = -1,
                 std::optional<EditorCoreObservation::Snapshot> coreBefore = std::nullopt) {
        auto& sample = samples_[count_++];
        sample.phase = phase;
        sample.zoom = zoom;
        sample.rows = rows;
        if (coreBefore) {
            sample.coreBefore = coreBefore;
            sample.coreAfter = EditorCoreObservation::Capture(*frame_->Editor());
        }
        if (observation_ == ObservationMode::None) return;
        auto* editor = frame_->Editor();
#ifdef __WXGTK__
        // These public wxGTK fields are read directly: GetClientSize can measure GTK widgets.
        sample.frameCache = {frame_->m_width, frame_->m_height};
        sample.frameClientCache = {frame_->m_clientWidth, frame_->m_clientHeight};
        sample.frameClientValid = frame_->m_useCachedClientSize;
        sample.editorCache = {editor->m_width, editor->m_height};
        sample.editorClientCache = {editor->m_clientWidth, editor->m_clientHeight};
        sample.editorClientValid = editor->m_useCachedClientSize;
#endif
        if (observation_ == ObservationMode::Measuring) {
            // Retain the old measuring getters only in the explicit A/B control.
            sample.frameMeasured = frame_->GetSize();
            sample.frameClientMeasured = frame_->GetClientSize();
            sample.editorMeasured = editor->GetSize();
            sample.editorClientMeasured = editor->GetClientSize();
        }
#ifdef __WXGTK__
        // gtk_widget_get_allocation only copies the widget's current allocation.
        sample.gtkFrame = Allocation(frame_->m_widget);
        sample.gtkFrameClient = Allocation(frame_->GetConnectWidget());
        sample.gtkEditor = Allocation(editor->m_widget);
        sample.gtkEditorClient = Allocation(editor->GetConnectWidget());
#endif
    }

private:
    struct Sample {
        const char* phase = "";
        int zoom = 0;
        intptr_t rows = -1;
        std::optional<EditorCoreObservation::Snapshot> coreBefore, coreAfter;
        wxSize frameMeasured, frameClientMeasured, editorMeasured, editorClientMeasured;
#ifdef __WXGTK__
        wxSize frameCache, frameClientCache, editorCache, editorClientCache;
        bool frameClientValid = false;
        bool editorClientValid = false;
        wxRect gtkFrame, gtkFrameClient, gtkEditor, gtkEditorClient;
#endif
    };
    static void PrintCore(const Sample& sample, const char* side, const EditorCoreObservation::Snapshot& core) {
        std::fprintf(stderr,
                     "[DEBUG-unicode-275] core_phase=%s zoom=%d rows=%lld side=%s wrap_width=%d"
                     " paint_rect=%g,%g,%g,%g paint_size=%gx%g text_start=%d right_margin=%d line_height=%d"
                     " wrap_pending=%d wrap_range=%lld,%lld paint_state=%d painting_all_text=%d\n",
                     sample.phase, sample.zoom, static_cast<long long>(sample.rows), side, core.wrapWidth,
                     static_cast<double>(core.rcPaint.left), static_cast<double>(core.rcPaint.top),
                     static_cast<double>(core.rcPaint.right), static_cast<double>(core.rcPaint.bottom),
                     static_cast<double>(core.rcPaint.right - core.rcPaint.left),
                     static_cast<double>(core.rcPaint.bottom - core.rcPaint.top), core.textStart, core.rightMarginWidth,
                     core.lineHeight, core.needsWrap, static_cast<long long>(core.wrapStart),
                     static_cast<long long>(core.wrapEnd), core.paintState, core.paintingAllText);
    }
    void PrintGeometry(const Sample& sample) const {
        if (observation_ == ObservationMode::None) return;
#ifdef __WXGTK__
        std::fprintf(stderr,
                     " frame_cache=%dx%d frame_client_cache=%dx%d frame_client_valid=%d"
                     " editor_cache=%dx%d editor_client_cache=%dx%d editor_client_valid=%d",
                     sample.frameCache.x, sample.frameCache.y, sample.frameClientCache.x, sample.frameClientCache.y,
                     sample.frameClientValid, sample.editorCache.x, sample.editorCache.y, sample.editorClientCache.x,
                     sample.editorClientCache.y, sample.editorClientValid);
        std::fprintf(stderr,
                     " gtk_frame=%d,%d,%dx%d gtk_frame_client=%d,%d,%dx%d"
                     " gtk_editor=%d,%d,%dx%d gtk_editor_client=%d,%d,%dx%d",
                     sample.gtkFrame.x, sample.gtkFrame.y, sample.gtkFrame.width, sample.gtkFrame.height,
                     sample.gtkFrameClient.x, sample.gtkFrameClient.y, sample.gtkFrameClient.width,
                     sample.gtkFrameClient.height, sample.gtkEditor.x, sample.gtkEditor.y, sample.gtkEditor.width,
                     sample.gtkEditor.height, sample.gtkEditorClient.x, sample.gtkEditorClient.y,
                     sample.gtkEditorClient.width, sample.gtkEditorClient.height);
#endif
        if (observation_ == ObservationMode::Measuring)
            std::fprintf(stderr,
                         " frame_measured=%dx%d frame_client_measured=%dx%d"
                         " editor_measured=%dx%d editor_client_measured=%dx%d",
                         sample.frameMeasured.x, sample.frameMeasured.y, sample.frameClientMeasured.x,
                         sample.frameClientMeasured.y, sample.editorMeasured.x, sample.editorMeasured.y,
                         sample.editorClientMeasured.x, sample.editorClientMeasured.y);
    }
#ifdef __WXGTK__
    static wxRect Allocation(GtkWidget* widget) {
        GtkAllocation allocation;
        gtk_widget_get_allocation(widget, &allocation);
        return {allocation.x, allocation.y, allocation.width, allocation.height};
    }
#endif
    EditorFrame* frame_;
    bool minimal_;
    ObservationMode observation_;
    // Fixed storage and delayed output keep diagnostics out of the event loop.
    std::array<Sample, 10> samples_{};
    size_t count_ = 0;
};
void Geometry(EditorFrame* frame, int command, bool minimal, ObservationMode observation, bool settledViewport) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    GeometryDiagnostics diagnostics(frame, minimal, observation);
    diagnostics.Observe("before-resize");
    const auto requested = frame->FromDIP(wxSize(360, 300));
    if (settledViewport) {
        frame->SetClientSize(requested);
        frame->Layout();
    } else {
        frame->SetSize(360, 300);
    }
    const auto settled = [frame, editor, requested] {
        if (frame->GetClientSize() != requested || editor->GetSize() != requested) return false;
#ifdef __WXGTK__
        const auto drawingSizeIs = [](GdkWindow* window, const wxSize& size) {
            return window && gdk_window_get_width(window) == size.x && gdk_window_get_height(window) == size.y;
        };
        GtkAllocation allocation;
        gtk_widget_get_allocation(GTK_WIDGET(editor->GetHandle()), &allocation);
        return wxSize(allocation.width, allocation.height) == requested &&
               drawingSizeIs(frame->GTKGetDrawingWindow(), requested);
#else
        return true;
#endif
    };
    const auto paint = [&] {
        if (settledViewport)
            NativePaint::Complete(editor, settled);
        else
            NativeFold::Paint(editor);
    };
    if (settledViewport) paint();
    diagnostics.Observe("resize-requested");
    if (!minimal) {
        TestEditor::ResetStream(editor,
                                "A\xe2\x81\xa0"
                                "B",
                                5, 5);
        paint();
        diagnostics.Observe("resize-painted");
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
        paint();
        Require(NativePaint::Read(editor, area) != visible,
                "the named representation changes actual native widget pixels when hidden");
        NativeSearch::Command(frame, command);
    }
    std::string line;
    for (int repeat = 0; repeat < 80; ++repeat)
        line += "a\xe2\x81\xa0"
                "b ";
    TestEditor::ResetStream(editor, line.c_str());
    const int wrap = frame->GetMenuBar()->FindMenuItem("View", "Word Wrap");
    NativeSearch::Command(frame, wrap);
    diagnostics.Observe("wrap-enabled");
    for (const auto zoom : {100, 150}) {
        editor->Send(SetZoom, zoom);
        paint();
        const auto visibleBefore = diagnostics.BeforeWrapCount();
        const auto visibleRows = editor->Send(WrapCount, 0);
        diagnostics.Observe("visible", zoom, visibleRows, visibleBefore);
        const auto before = minimal ? std::nullopt : std::optional{DocumentState(frame)};
        NativeSearch::Command(frame, command);
        paint();
        const auto hiddenBefore = diagnostics.BeforeWrapCount();
        const auto hiddenRows = editor->Send(WrapCount, 0);
        diagnostics.Observe("hidden", zoom, hiddenRows, hiddenBefore);
        Require(visibleRows > hiddenRows && (minimal || DocumentState(frame) == *before),
                "representation widths reflow wrapped rows at normal and enlarged zoom without changing the document");
        NativeSearch::Command(frame, command);
        paint();
        const auto restoredBefore = diagnostics.BeforeWrapCount();
        const auto restoredRows = editor->Send(WrapCount, 0);
        diagnostics.Observe("restored", zoom, restoredRows, restoredBefore);
        Require(restoredRows == visibleRows, "showing names restores the original wrapped layout");
    }
    if (minimal) {
        std::puts("PASS: minimal native representation wrapping restores both original row counts");
        return;
    }
    NativeSearch::Command(frame, wrap);
    editor->Send(SetZoom, 100);
    editor->Send(SetXOffset, 100);
    paint();
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
        wxString mode;
        const bool minimal = wxGetEnv("WXNOTEPAD_UNICODE_MINIMAL", &mode) && mode == "1";
        wxString viewport;
        const bool settledViewport = wxGetEnv("WXNOTEPAD_UNICODE_SETTLED", &viewport) && viewport == "1";
        wxString requestedObservation;
        wxGetEnv("WXNOTEPAD_UNICODE_OBSERVATION", &requestedObservation);
        const auto observation = requestedObservation == "none"        ? ObservationMode::None
                                 : requestedObservation == "measuring" ? ObservationMode::Measuring
                                                                       : ObservationMode::Cached;
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_UNICODE_DPI96"))
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "create the complementary owned 96-DPI representation window");
#endif
        settings_.values = OriginalSettings{};
        settings_.values.wordWrap = false;
        if (settledViewport) settings_.values.showMenu = false;
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
        NativeKeys::AfterPresentation(frame, [this, frame, minimal, observation, settledViewport](bool presented) {
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
                if (!minimal) Table(frame, id);
                Geometry(frame, id, minimal, observation, settledViewport);
                if (!minimal) Lifecycle(frame, id);
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
