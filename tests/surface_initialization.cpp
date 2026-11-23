#include "editor/editor_core.h"
#include "editor/platform/text_measurement.h"
#include "completion_allocation_fault.h"
#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "native_renderer_helpers.h"
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <print>
#include <cstdlib>
#include <exception>
#include <new>
#include <utility>
#include <vector>

// Observe the production paint state without adding an application test API.
struct EditorCoreObservation {
    static bool PaintIdle(const EditorControl& editor) {
        return editor.core_->paintState == decltype(editor.core_->paintState)::notPainting;
    }
    static Scintilla::Internal::Document& Document(EditorControl& editor) { return *editor.core_->pdoc; }
    static const Scintilla::Internal::Selection& Selection(const EditorControl& editor) { return editor.core_->sel; }
    static void InvalidateStyles(EditorControl& editor) { editor.core_->stylesValid = false; }
    static std::unique_ptr<Scintilla::Internal::Surface> DrawingSurface(const EditorControl& editor, wxDC* dc) {
        return editor.core_->CreateDrawingSurface(dc, true);
    }
};

namespace {
namespace core = Scintilla::Internal;
using Scintilla::Message;
constexpr auto DefaultStyle = static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::Default));
constexpr auto Ok = static_cast<uintptr_t>(std::to_underlying(Scintilla::Status::Ok));
constexpr auto BadAlloc = static_cast<intptr_t>(std::to_underlying(Scintilla::Status::BadAlloc));
constexpr std::string_view Sample = "native measurement retry";

void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}

void Initialize(core::Surface& surface, wxWindow& window, wxDC& borrowed, bool owned) {
    if (owned)
        surface.Init(&window);
    else
        surface.Init(&borrowed, &window);
}

void RequireMeasurement(core::Surface& surface, const core::Font& font, double expected) {
    Require(surface.Initialised(), "a retried surface has both drawing and measurement resources");
    std::array<double, Sample.size()> positions{};
    surface.MeasureWidthsUTF8(&font, Sample, positions.data());
    Require(surface.WidthTextUTF8(&font, Sample) == expected && positions.back() == expected,
            "retry preserves the native text width and final byte position");
}

void CheckInitialization(wxWindow& window, wxDC& borrowed, const core::Font& font, bool owned) {
    auto surface = core::Surface::Allocate(Scintilla::Technology::Default);
    Initialize(*surface, window, borrowed, owned);
    Require(surface->Initialised(), "the surface starts with a working native context");
    const double expected = surface->WidthTextUTF8(&font, Sample);
    Require(expected > 0, "the native measurement fixture has a positive width");

    // Exercise owned DC and measurement allocations separately. The borrowed
    // form first allocates TextMeasurement, then its private native snapshot.
    struct FailurePoint {
        size_t size;
        size_t skip;
    };
    constexpr std::array ownedPoints{FailurePoint{sizeof(wxClientDC), 0},
                                     FailurePoint{sizeof(core::TextMeasurement), 0}};
    constexpr std::array borrowedPoints{FailurePoint{0, 0}, FailurePoint{0, 1}};
    for (const auto& point : owned ? ownedPoints : borrowedPoints) {
        bool injected = false;
        {
            const CompletionAllocationFault::ScopedFailure failure(point.size, point.skip);
            Initialize(*surface, window, borrowed, owned);
            injected = failure.Injected();
        }
        Require(injected && !surface->Initialised(),
                "failed noexcept initialization releases its old context and publishes no partial state");
        surface->Release();
        surface->Release();
        Require(!surface->Initialised(), "release remains idempotent after failed initialization");
        Initialize(*surface, window, borrowed, owned);
        RequireMeasurement(*surface, font, expected);
    }

    // Switching ownership must discard the old clip before its drawing context.
    surface->SetClip(core::PRectangle(2, 2, 70, 50));
    Initialize(*surface, window, borrowed, !owned);
    Require(surface->Initialised(), "reinitialization can switch owned and borrowed native contexts");
    surface->Release();
    Require(borrowed.IsOk(), "releasing a surface preserves the caller's borrowed drawing context");
    std::puts(owned ? "PASS: owned surface allocation failure, release and retry"
                    : "PASS: borrowed surface object/data allocation failure, release and retry");
}

void CheckUnavailableContext(wxWindow& window, wxDC& borrowed, const core::Font& font) {
    auto surface = core::Surface::Allocate(Scintilla::Technology::Default);
    surface->Init(&borrowed, &window);
    Require(surface->Initialised(), "the unavailable-context fixture starts initialized");
    const double expected = surface->WidthTextUTF8(&font, Sample);
    surface->Init(static_cast<core::WindowID>(nullptr));
    Require(!surface->Initialised(), "a missing window cannot leave the previous surface initialized");
    surface->Init(static_cast<core::SurfaceID>(nullptr), &window);
    Require(!surface->Initialised(), "a missing borrowed context leaves an uninitialized surface");

    // wxGCDC explicitly supports a null graphics context and reports !IsOk()
    // on both supported backends; no OS resource exhaustion is required.
    wxGCDC unavailable(static_cast<wxGraphicsContext*>(nullptr));
    Require(!unavailable.IsOk(), "the native no-resource fixture has no graphics context");
    surface->Init(&unavailable, &window);
    Require(!surface->Initialised(), "an unavailable native context is rejected before use");
    surface->Init(&borrowed, &window);
    RequireMeasurement(*surface, font, expected);
    std::puts("PASS: missing and unavailable native contexts leave a reusable surface");
}

void CheckDrawingFactory(const EditorControl& editor, wxDC& borrowed, const core::Font& font) {
    bool injected = false;
    bool caught = false;
    {
        const CompletionAllocationFault::ScopedFailure failure(sizeof(core::TextMeasurement));
        try {
            const auto surface = EditorCoreObservation::DrawingSurface(editor, &borrowed);
        } catch (const std::bad_alloc&) {
            caught = true;
        }
        injected = failure.Injected();
    }
    Require(injected && caught, "the pinned drawing factory rejects incomplete measurement initialization");
    wxGCDC unavailable(static_cast<wxGraphicsContext*>(nullptr));
    caught = false;
    try {
        const auto surface = EditorCoreObservation::DrawingSurface(editor, &unavailable);
    } catch (const std::bad_alloc&) {
        caught = true;
    }
    Require(caught, "the pinned drawing factory rejects unavailable native resources");
    const auto retried = EditorCoreObservation::DrawingSurface(editor, &borrowed);
    Require(retried->Initialised() && retried->WidthTextUTF8(&font, Sample) > 0,
            "the pinned drawing factory returns a usable surface after retry");
    std::puts("PASS: the pinned borrowed-context drawing factory rejects failed initialization and retries");
}

void RequireClip(const wxDC& dc, const wxRect& expected) {
    wxRect actual;
    Require(dc.GetClippingBox(actual) && actual == expected,
            "failed clip allocation and cleanup preserve the caller's native clipping rectangle");
}

void CheckClipFailures(wxWindow& window, wxDC& borrowed) {
    const wxRect original(3, 4, 100, 70);
    const wxRect outer(10, 11, 70, 49);
    borrowed.SetClippingRegion(original);
    unsigned injectedCount = 0;
    bool completed = false;
    for (size_t skip = 0; skip < 64; ++skip) {
        auto surface = core::Surface::Allocate(Scintilla::Technology::Default);
        surface->Init(&borrowed, &window);
        Require(surface->Initialised(), "the clip fixture has a complete borrowed surface");
        surface->SetClip(core::PRectangle(10, 11, 80, 60));
        RequireClip(borrowed, outer);
        bool injected = false;
        bool caught = false;
        {
            const CompletionAllocationFault::ScopedFailure failure(0, skip);
            try {
                surface->SetClip(core::PRectangle(20, 22, 40, 41));
            } catch (const std::bad_alloc&) {
                caught = true;
            }
            injected = failure.Injected();
        }
        if (!injected) {
            surface->PopClip();
            RequireClip(borrowed, outer);
            surface->Release();
            RequireClip(borrowed, original);
            completed = true;
            break;
        }
        ++injectedCount;
        Require(caught, "clip allocation failure propagates through the surface contract");
        RequireClip(borrowed, outer);
        surface->SetClip(core::PRectangle(20, 22, 40, 41));
        surface->PopClip();
        RequireClip(borrowed, outer);
        surface->SetClip(core::PRectangle(20, 22, 40, 41));
        surface->Release();
        RequireClip(borrowed, original);
    }
    Require(completed && injectedCount >= 2, "clip fault injection covers storage and clip-object allocations");
    borrowed.DestroyClippingRegion();
    std::puts("PASS: clip allocation failures unwind, retain native clipping and permit retry");
}

struct DocumentState {
    std::string text;
    intptr_t anchor;
    intptr_t caret;
    intptr_t canUndo;
    intptr_t canRedo;
    intptr_t modified;

    explicit DocumentState(EditorControl* editor)
        : text(TestEditor::Text(editor)), anchor(editor->Send(Message::GetAnchor)),
          caret(editor->Send(Message::GetCurrentPos)), canUndo(editor->Send(Message::CanUndo)),
          canRedo(editor->Send(Message::CanRedo)), modified(editor->Send(Message::GetModify)) {}

    void RequireUnchanged(EditorControl* editor) const {
        using enum Scintilla::Message;
        Require(TestEditor::Text(editor) == text && editor->Send(GetAnchor) == anchor &&
                    editor->Send(GetCurrentPos) == caret && editor->Send(CanUndo) == canUndo &&
                    editor->Send(CanRedo) == canRedo && editor->Send(GetModify) == modified,
                "surface failure and repaint preserve text, selection, Undo/Redo and modified state");
    }
};

class PaintFailure final : public wxEvtHandler {
public:
    explicit PaintFailure(EditorControl* editor) : editor_(editor) { editor_->PushEventHandler(this); }
    ~PaintFailure() override { editor_->PopEventHandler(false); }

    void Complete() {
        // Stay at the head of the native handler chain: another pushed
        // observer would restrict this handler to processing its own table,
        // ending the failure scope before the production paint handler runs.
        editor_->Refresh(false);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!completed_ && std::chrono::steady_clock::now() < deadline) {
            editor_->Update();
            wxYield();
            if (!completed_) wxMilliSleep(1);
        }
        Require(completed_, "the native allocation-failure paint dispatch completes");
    }

    bool ProcessEvent(wxEvent& event) override {
        if (event.GetEventType() != wxEVT_PAINT || called) return wxEvtHandler::ProcessEvent(event);
        called = true;
        // Arm only inside the native paint dispatch, after Refresh/Update and
        // event-loop bookkeeping. Forward to the actual production handler.
        bool handled = false;
        {
            const CompletionAllocationFault::ScopedFailure failure(sizeof(core::TextMeasurement));
            handled = wxEvtHandler::ProcessEvent(event);
            injected = failure.Injected();
        }
        recovered = editor_->Send(Message::GetStatus) == BadAlloc && EditorCoreObservation::PaintIdle(*editor_);
        completed_ = true;
        return handled;
    }

    bool Recovered() const { return called && injected && recovered; }

private:
    bool called = false;
    bool injected = false;
    bool recovered = false;
    EditorControl* editor_;
    bool completed_ = false;
};

enum class DocumentFailurePoint { Insertion, Undo };

class CommittedModificationFailure final : public core::DocWatcher {
public:
    explicit CommittedModificationFailure(EditorControl& editor,
                                          DocumentFailurePoint point = DocumentFailurePoint::Insertion, size_t skip = 0)
        : editor_(editor), point_(point), skip_(skip) {
        Require(EditorCoreObservation::Document(editor_).AddWatcher(this, nullptr),
                "the document registers its post-commit failure observer");
    }
    ~CommittedModificationFailure() override {
        failure_.reset();
        EditorCoreObservation::Document(editor_).RemoveWatcher(this, nullptr);
    }
    CommittedModificationFailure(const CommittedModificationFailure&) = delete;
    CommittedModificationFailure& operator=(const CommittedModificationFailure&) = delete;
    CommittedModificationFailure(CommittedModificationFailure&&) = delete;
    CommittedModificationFailure& operator=(CommittedModificationFailure&&) = delete;

    bool Injected() const noexcept { return failure_ && failure_->Injected(); }
    void NotifySavePoint(core::Document*, void*, bool atSavePoint) noexcept override {
        if (!atSavePoint && point_ == DocumentFailurePoint::Insertion) {
            // InsertString has already committed its bytes before this
            // notification. Its ensuing visual update must finish the edit
            // even when refreshing styles cannot create a surface.
            Arm();
        }
    }
    void NotifyModifyAttempt(core::Document*, void*) noexcept override {
        // Injection is armed by committed edits or Undo, outside read-only modification attempts.
    }
    void NotifyModified(core::Document*, core::DocModification modification, void*) override {
        if (point_ == DocumentFailurePoint::Undo &&
            Scintilla::FlagSet(modification.modificationType, Scintilla::ModificationFlags::Undo) &&
            Scintilla::FlagSet(modification.modificationType, Scintilla::ModificationFlags::BeforeDelete))
            Arm();
    }
    void NotifyDeleted(core::Document*, void*) noexcept override {
        // This scoped observer is removed while the editor's document is still alive.
    }
    void NotifyStyleNeeded(core::Document*, void*, Sci::Position) override {
        // The editor owns styling; this observer arms resource failures at edit notifications.
    }
    void NotifyErrorOccurred(core::Document*, void*, Scintilla::Status) noexcept override {
        // The checks read the editor's error status after the operation completes.
    }
    void NotifyGroupCompleted(core::Document*, void*) noexcept override {
        // The selected save-point or BeforeDelete notification already controls each group's injection.
    }

private:
    void Arm() noexcept {
        if (!failure_) {
            EditorCoreObservation::InvalidateStyles(editor_);
            failure_.emplace(sizeof(wxClientDC), skip_);
        }
    }
    EditorControl& editor_;
    DocumentFailurePoint point_;
    size_t skip_;
    std::optional<CompletionAllocationFault::ScopedFailure> failure_;
};

void CheckCommittedModificationRecovery(EditorControl* editor) {
    using enum Scintilla::Message;
    TestEditor::ResetStream(editor, "alpha\nbeta\n", 5, 5);
    NativePaint::Complete(editor);
    const auto revision = editor->DocumentRevision();
    editor->Send(SetStatus, Ok);
    editor->Send(BeginUndoAction);
    bool injected = false;
    {
        CommittedModificationFailure failure(*editor);
        editor->Send(AddText, 1, reinterpret_cast<intptr_t>("x"));
        injected = failure.Injected();
    }
    editor->Send(EndUndoAction);
    Require(injected && editor->Send(GetStatus) == BadAlloc,
            "a committed insertion reaches the failing visual surface refresh");
    Require(TestEditor::Text(editor) == "alphax\nbeta\n" && editor->DocumentRevision() > revision &&
                editor->Send(GetCurrentPos) == 6 && editor->Send(GetModify),
            "a failed post-commit visual update completes document and selection notifications");
    editor->Send(SetStatus, Ok);
    editor->Send(BeginUndoAction);
    editor->Send(AddText, 1, reinterpret_cast<intptr_t>("y"));
    editor->Send(EndUndoAction);
    Require(TestEditor::Text(editor) == "alphaxy\nbeta\n",
            "the document mutation guard permits another insertion after visual recovery");
    editor->Send(Undo);
    Require(TestEditor::Text(editor) == "alphax\nbeta\n", "Undo removes the subsequent insertion");
    editor->Send(Undo);
    Require(TestEditor::Text(editor) == "alpha\nbeta\n" && !editor->Send(GetModify) && !editor->Send(CanUndo) &&
                editor->Send(CanRedo),
            "Undo of the failed-refresh insertion restores the original save point");
    editor->Send(Redo);
    editor->Send(Redo);
    Require(TestEditor::Text(editor) == "alphaxy\nbeta\n" && editor->Send(GetModify) && !editor->Send(CanRedo),
            "both original insertions remain available for Redo after visual recovery");
    NativePaint::Complete(editor);
    std::puts("PASS: post-commit surface failure completes notifications and preserves subsequent editing");
}

void CheckUndoRecovery(EditorControl* editor) {
    constexpr char Original[] = "alpha\nbeta\n";
    constexpr char Inserted[] = "alphax\ny\nbeta\n";
    unsigned injectedCount = 0;
    size_t skip = 0;
    for (;; ++skip) {
        TestEditor::ResetStream(editor, Original, 5, 5);
        editor->Send(Message::AddText, 3, reinterpret_cast<intptr_t>("x\ny"));
        NativePaint::Complete(editor);
        const auto revision = editor->DocumentRevision();
        editor->Send(Message::SetStatus, Ok);
        bool injected = false;
        {
            CommittedModificationFailure failure(*editor, DocumentFailurePoint::Undo, skip);
            editor->Send(Message::Undo);
            injected = failure.Injected();
        }
        Require(TestEditor::Text(editor) == Original && editor->DocumentRevision() > revision &&
                    !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                    editor->Send(Message::CanRedo),
                "Undo completes its text, revision and save-point notifications after visual allocation failure");
        TestEditor::RequireSelection(editor, 5, 5, "Undo restores the original caret despite visual resource failure");
        if (injected) {
            ++injectedCount;
            Require(editor->Send(Message::GetStatus) == BadAlloc, "Undo retains the visual allocation failure status");
        }
        editor->Send(Message::SetStatus, Ok);
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == Inserted && !editor->Send(Message::CanRedo),
                "Redo remains usable after a surface failure during Undo");
        editor->Send(Message::BeginUndoAction);
        editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("z"));
        editor->Send(Message::EndUndoAction);
        Require(TestEditor::Text(editor) == "alphax\nyz\nbeta\n",
                "later insertion is not blocked by the completed Undo notification");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == Inserted, "the later insertion retains a separate Undo action");
        if (!injected) break;
    }
    Require(injectedCount >= 2, "Undo fault injection reaches both style refresh and later visual surface creation");
    std::println("PASS: every Undo visual surface allocation preserves text, selection and future editing "
                 "(injected={:d}; first uninjected skip={:d})",
                 injectedCount, skip);
}

struct RectangleSelectionState {
    size_t main;
    core::SelectionRange rectangle;
    std::vector<core::SelectionRange> ranges;

    explicit RectangleSelectionState(const EditorControl& editor) {
        const auto& selection = EditorCoreObservation::Selection(editor);
        main = selection.Main();
        rectangle = selection.RectangularCopy();
        for (size_t range = 0; range < selection.Count(); ++range)
            ranges.push_back(selection.Range(range));
    }

    void RequireUnchanged(const EditorControl& editor) const {
        const auto& selection = EditorCoreObservation::Selection(editor);
        if (selection.Main() != main)
            std::println(stderr, "Rectangular main selection: {:d}; expected {:d}", selection.Main(), main);
        Require(selection.IsRectangular() && selection.Main() == main && selection.Count() == ranges.size() &&
                    selection.RectangularCopy() == rectangle,
                "Undo and visual retry preserve the rectangular endpoints, range count and original main row");
        for (size_t range = 0; range < ranges.size(); ++range)
            Require(selection.Range(range) == ranges[range],
                    "Undo and visual retry preserve every rectangular anchor, caret and virtual-space offset");
    }
};

void CheckRectangularUndoRecovery(EditorControl* editor) {
    constexpr char Original[] = "alpha\nbeta\nomega\n";
    unsigned injectedCount = 0;
    size_t skip = 0;
    editor->Send(Message::SetUndoSelectionHistory, 3);
    for (;; ++skip) {
        TestEditor::ResetStream(editor, Original);
        editor->Send(Message::SetRectangularSelectionAnchor, 1);
        editor->Send(Message::SetRectangularSelectionCaret, 14);
        NativePaint::Complete(editor);
        Require(editor->Send(Message::GetSelections) == 3, "the rectangular fixture covers three native text lines");
        // The range-building loop naturally leaves the final line as main.
        // Preserve a different main row through both Undo and a later repaint.
        editor->Send(Message::SetMainSelection, 0);
        const RectangleSelectionState before(*editor);
        editor->Send(Message::BeginUndoAction);
        editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("x"));
        editor->Send(Message::EndUndoAction);
        NativePaint::Complete(editor);
        const DocumentState inserted(editor);
        const auto revision = editor->DocumentRevision();
        editor->Send(Message::SetStatus, Ok);
        bool injected = false;
        {
            CommittedModificationFailure failure(*editor, DocumentFailurePoint::Undo, skip);
            editor->Send(Message::Undo);
            injected = failure.Injected();
        }
        before.RequireUnchanged(*editor);
        Require(TestEditor::Text(editor) == Original && editor->DocumentRevision() > revision &&
                    !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                    editor->Send(Message::CanRedo),
                "rectangular Undo completes text, revision, save-point and history updates after resource failure");
        if (injected) {
            ++injectedCount;
            Require(editor->Send(Message::GetStatus) == BadAlloc,
                    "rectangular Undo reports its failed visual resource");
        }
        editor->Send(Message::SetStatus, Ok);
        NativePaint::Complete(editor);
        before.RequireUnchanged(*editor);
        editor->Send(Message::Redo);
        inserted.RequireUnchanged(editor);
        editor->Send(Message::Undo);
        before.RequireUnchanged(*editor);
        Require(TestEditor::Text(editor) == Original && !editor->Send(Message::GetModify) &&
                    !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                "another Undo still restores the complete original rectangular selection and save point");
        if (!injected) break;
    }
    Require(injectedCount >= 3,
            "rectangular Undo fault injection reaches style refresh, range reconstruction and caret measurement");
    std::println("PASS: rectangular Undo resource failures preserve every range and main row across repaint and Redo "
                 "(injected={:d}; first uninjected skip={:d})",
                 injectedCount, skip);
}

void CheckNativeRecovery(EditorControl* editor) {
    using enum Scintilla::Message;
    TestRenderer::Select(editor);
    TestEditor::ResetStream(editor, "alpha\nbeta\n", 5, 5);
    for (const char* insertion : {"x", "y"}) {
        editor->Send(BeginUndoAction);
        editor->Send(AddText, 1, reinterpret_cast<intptr_t>(insertion));
        editor->Send(EndUndoAction);
    }
    editor->Send(Undo);
    Require(TestEditor::Text(editor) == "alphax\nbeta\n" && editor->Send(CanUndo) && editor->Send(CanRedo) &&
                editor->Send(GetModify),
            "the native failure fixture retains independent Undo and Redo actions");
    NativePaint::Complete(editor);
    const DocumentState before(editor);

    editor->Send(SetStatus, Ok);
    bool injected = false;
    intptr_t width = -1;
    {
        const CompletionAllocationFault::ScopedFailure failure(sizeof(wxClientDC));
        width = editor->Send(TextWidth, DefaultStyle, reinterpret_cast<intptr_t>(Sample.data()));
        injected = failure.Injected();
    }
    Require(injected && width == 0 && editor->Send(GetStatus) == BadAlloc,
            "measurement initialization failure returns through the original message status boundary");
    before.RequireUnchanged(editor);
    editor->Send(SetStatus, Ok);
    Require(editor->Send(TextWidth, DefaultStyle, reinterpret_cast<intptr_t>(Sample.data())) > 0 &&
                editor->Send(GetStatus) == static_cast<intptr_t>(std::to_underlying(Scintilla::Status::Ok)),
            "the original text-width message can retry after initialization failure");

    {
        PaintFailure failure(editor);
        failure.Complete();
        Require(failure.Recovered(),
                "actual native paint contains allocation failure and restores its idle paint state");
    }
    before.RequireUnchanged(editor);
    editor->Send(SetStatus, Ok);
    NativePaint::Complete(editor);
    Require(editor->Send(GetStatus) == static_cast<intptr_t>(std::to_underlying(Scintilla::Status::Ok)) &&
                EditorCoreObservation::PaintIdle(*editor),
            "actual native painting succeeds again after resource recovery");
    before.RequireUnchanged(editor);

    editor->Send(Undo);
    Require(TestEditor::Text(editor) == "alpha\nbeta\n" && !editor->Send(CanUndo) && editor->Send(CanRedo) &&
                !editor->Send(GetModify),
            "Undo still returns to the original document and save point after rendering failures");
    editor->Send(Redo);
    Require(TestEditor::Text(editor) == "alphax\nbeta\n", "the first Redo action survives rendering failures");
    editor->Send(Redo);
    Require(TestEditor::Text(editor) == "alphaxy\nbeta\n" && !editor->Send(CanRedo),
            "the original pending Redo action survives rendering failures");
    std::puts("PASS: native measurement/paint failures recover without changing document history");
}

void CheckSurfaces(EditorFrame* frame, bool presented) {
    Require(presented, "the native compositor presents the surface failure fixture");
    wxBitmap bitmap;
    Require(bitmap.Create(160, 100), "the borrowed-context fixture owns a native bitmap");
    wxMemoryDC borrowed(bitmap);
    const auto font = core::Font::Allocate(core::FontParameters(core::Platform::DefaultFont(), 12));
    CheckInitialization(*frame, borrowed, *font, true);
    CheckInitialization(*frame, borrowed, *font, false);
    CheckUnavailableContext(*frame, borrowed, *font);
    CheckDrawingFactory(*frame->Editor(), borrowed, *font);
    CheckClipFailures(*frame, borrowed);
    CheckNativeRecovery(frame->Editor());
    CheckCommittedModificationRecovery(frame->Editor());
    CheckUndoRecovery(frame->Editor());
    CheckRectangularUndoRecovery(frame->Editor());
}

class SurfaceInitializationApp final : public wxApp {
public:
    bool OnInit() override {
        std::set_terminate([] { std::_Exit(86); });
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                CheckSurfaces(frame, presented);
                result_ = 0;
            } catch (const std::exception& error) {
                std::println(stderr, "FAIL: {}", error.what());
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SurfaceInitializationApp);
