#include "app/process_observer.h"
#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "diagnostics/trace.h"
#include <wx/ffile.h>
#include <wx/textfile.h>
#include <wx/menu.h>
#include <wx/statusbr.h>
#include <wx/log.h>
#include <cstdio>
#include <utility>
#ifdef __WXMSW__
#include <windows.h>
#endif
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace {
#ifdef WXNOTEPAD_DIAGNOSTICS
unsigned long long DiagnosticRequest(const wxString& sequence) {
    unsigned long long value = 0;
    sequence.ToULongLong(&value);
    return value;
}
#endif

void DispatchObservedCommand(EditorFrame& frame, const wxString& operation, const wxString& source, wxString& result) {
    result = "failed";
    if (operation == "open" && !source.empty())
        result = frame.Open(source).status == OpenStatus::Opened ? "ok" : "failed";
    if (operation == "save") result = frame.Save().status == SaveStatus::Saved ? "ok" : "failed";
    if (operation == "close") result = "ok";
    if (operation == "menu") {
        const int id = frame.GetMenuBar()->FindMenuItem(source.BeforeFirst('\t'), source.AfterFirst('\t'));
        if (id != wxNOT_FOUND) {
            wxCommandEvent event(wxEVT_MENU, id);
            result = frame.GetEventHandler()->ProcessEvent(event) ? "ok" : "failed";
            frame.UpdateWindowUI(wxUPDATE_UI_RECURSE);
        }
    }
}
} // namespace

ProcessObserver::ProcessObserver(EditorFrame& frame, wxString directory)
    : frame_(&frame), observation_(std::move(directory)), observer_(this) {
    frame_->Bind(wxEVT_DESTROY, &ProcessObserver::OnFrameDestroyed, this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Observe(); });
    CallAfter([this] {
        if (frame_) {
            Publish();
            observer_.Start(10);
        }
    });
}

ProcessObserver::~ProcessObserver() {
    observer_.Stop();
    if (frame_) frame_->Unbind(wxEVT_DESTROY, &ProcessObserver::OnFrameDestroyed, this);
}

void ProcessObserver::OnFrameDestroyed(wxWindowDestroyEvent& event) {
    if (event.GetEventObject() == frame_) {
        observer_.Stop();
        frame_ = nullptr;
    }
    event.Skip();
}

// Opt-in external observation of this actual, shown application. No alternate
// editor host or hidden operation path: Open/Save are the frame's commands.
void ProcessObserver::Publish() {
    if (!frame_) return;
    frame_->Update();
    frame_->Editor()->Update();
    lastDirty_ = frame_->Editor()->Send(Scintilla::Message::GetModify) != 0;
    lastActive_ = frame_->IsActive();
    wxString backend = "Windows";
#ifdef __WXGTK__
    backend = G_OBJECT_TYPE_NAME(gdk_display_get_default());
#endif
    const char* encoding = frame_->DocumentEncoding() == FileEncoding::Utf8      ? "UTF-8"
                           : frame_->DocumentEncoding() == FileEncoding::Utf16Le ? "UTF-16LE"
                                                                                 : "CP936";
    const wxString contents =
        wxString::Format(
            "sequence\t%s\nresult\t%s\nlength\t%lld\ndirty\t%d\nshown\t%d\nfocus\t%d\nactive\t%d\nbackend\t%s\neditor_"
            "handle\t%llu\nwrap_mode\t%lld\nwrap_rows\t%lld\nclient_width\t%d\nline_numbers\t%d\nwhitespace\t%lld\neol_"
            "mode\t%lld\nzoom\t%lld\n"
            "encoding\t%s\nbom\t%d\n"
            "tab_width\t%lld\nindent_width\t%lld\nuse_tabs\t%lld\ntab_indents\t%lld\nbackspace_unindents\t%lld\nauto_"
            "indent\t%d\n",
            sequence_, result_, static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetLength)),
            lastDirty_, frame_->IsShown() && !frame_->IsIconized(), wxWindow::FindFocus() == frame_->Editor(),
            lastActive_, backend,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frame_->Editor()->GetHandle())),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetWrapMode)),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::WrapCount, 0)),
            frame_->Editor()->GetClientSize().x, frame_->Editor()->LineNumbersVisible(),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetViewWS)),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetEOLMode)),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetZoom)), encoding,
            frame_->DocumentHasBom(), static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetTabWidth)),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetIndent)),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetUseTabs)),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetTabIndents)),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetBackSpaceUnIndents)),
            frame_->Editor()->AutoIndentEnabled()) +
        "document_status\t" + frame_->GetStatusBar()->GetStatusText() + "\n" +
        wxString::Format(
            "line_selection\t%d\ncurrent_line_mode\t%d\ncurrent_line_subline\t%lld\n"
            "caret_style\t%lld\ncaret_width\t%lld\ncaret_period\t%lld\n"
            "mark_occurrences\t%d\ncode_folding\t%d\nmatch_braces\t%d\ncurrent_block\t%d\nfold_margin\t%lld\n",
            static_cast<int>(frame_->Editor()->GetLineSelectionMode()),
            static_cast<int>(frame_->Editor()->GetCurrentLineHighlightMode()),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetCaretLineHighlightSubLine)),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetCaretStyle)),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetCaretWidth)),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetCaretPeriod)),
            frame_->Editor()->Occurrences().Options(), frame_->Editor()->CodeFoldingVisible(),
            frame_->Editor()->VisualBraceMatching(), frame_->Editor()->CurrentBlockHighlighted(),
            static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetMarginWidthN, 2))) +
        wxString::Format("bookmark_margin\t%d\nchange_history\t%lld\nauto_shrink_scroll_width\t%d\n",
                         frame_->Editor()->BookmarkMarginVisible(),
                         static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetChangeHistory)),
                         frame_->Editor()->AutoShrinkScrollWidth());
    // A reader can temporarily deny replacement on Windows. IPC errors must
    // stay out of wxLogGui: its deferred modal dialog can outlive the frame.
    const wxLogNull noGuiLog;
    wxFFile state(observation_ + "/state.tmp", "wb");
    const bool written = state.IsOpened() && state.Write(contents);
    const bool closed = state.IsOpened() && state.Close();
    bool published = false;
    if (written && closed) {
        // wxRenameFile can fall back to copying over an open destination. Readers
        // must see a complete old or new snapshot, so retry a failed rename.
#ifdef __WXMSW__
        published = MoveFileExW((observation_ + "/state.tmp").wc_str(), (observation_ + "/state").wc_str(),
                                MOVEFILE_REPLACE_EXISTING) != 0;
#else
        published = ::rename((observation_ + "/state.tmp").utf8_str(), (observation_ + "/state").utf8_str()) == 0;
#endif
    }
    if (!published && !publishPending_) {
        const unsigned long error = wxSysErrorCode();
        const char* stage = !written ? "write" : !closed ? "close" : "rename";
        std::fprintf(stderr,
                     "Observation publication failed: stage=%s error=%lu sequence=%s result=%s directory=%s; retry "
                     "pending\n",
                     stage, error, sequence_.utf8_str().data(), result_.utf8_str().data(),
                     observation_.utf8_str().data());
        std::fflush(stderr);
    }
    publishPending_ = !published;
}

void ProcessObserver::CommandFailure(const char* stage, unsigned long error) {
    if (!commandPending_) {
        std::fprintf(stderr, "Observation command failed: stage=%s error=%lu last_ack=%s directory=%s; retry pending\n",
                     stage, error, sequence_.utf8_str().data(), observation_.utf8_str().data());
        std::fflush(stderr);
    }
    commandPending_ = true;
}

std::optional<ProcessObserver::Command> ProcessObserver::ConsumeCommand(const wxString& path) {
    // Keep the logging guard with file consumption; it ends before editor dispatch.
    const wxLogNull noGuiLog;
    wxTextFile command(path);
    if (!command.Open()) {
        CommandFailure("open", wxSysErrorCode());
        return std::nullopt;
    }
    if (command.GetLineCount() < 2) {
        CommandFailure("packet", 0);
        return std::nullopt;
    }
    Command packet{command[0], command[1], command.GetLineCount() > 2 ? command[2] : wxString()};
    if (!command.Close()) {
        CommandFailure("close", wxSysErrorCode());
        return std::nullopt;
    }
    if (!wxRemoveFile(path)) {
        CommandFailure("remove", wxSysErrorCode());
        return std::nullopt;
    }
    return packet;
}

void ProcessObserver::Observe() {
    if (!frame_) return;
    const wxString path = observation_ + "/command";
    if (wxFileExists(path)) {
        const auto command = ConsumeCommand(path);
        if (!command) return;
        commandPending_ = false;
        sequence_ = command->sequence;
        WXNOTE_TRACE("observer.request", "\ttarget=%p\trequest=%llu", static_cast<void*>(frame_),
                     DiagnosticRequest(sequence_));
        const auto& operation = command->operation;
        DispatchObservedCommand(*frame_, operation, command->source, result_);
        Publish();
        WXNOTE_TRACE("observer.ack", "\ttarget=%p\trequest=%llu\tok=%d", static_cast<void*>(frame_),
                     DiagnosticRequest(sequence_), result_ == "ok");
        if (operation == "close") frame_->Close();
    } else {
        commandPending_ = false;
        if (publishPending_ || (frame_->Editor()->Send(Scintilla::Message::GetModify) != 0) != lastDirty_ ||
            frame_->IsActive() != lastActive_)
            Publish();
    }
}
