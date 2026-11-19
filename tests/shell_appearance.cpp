#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include "editor/occurrence_marking.h"
#include <wx/toolbar.h>
#include <wx/statusbr.h>
#include <memory>
#include <wx/log.h>
#include <wx/dialog.h>
#ifdef __WXMSW__
#include <commctrl.h>
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class AppearanceApp final : public wxApp {
public:
    bool OnInit() override {
        wxLog::SetActiveTarget(new wxLogStderr);
        settings_ =
            std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory() / "appearance.ini"));
        settings_->values.selectOption &= ~SelectOption_CopyPasteBufferAsFindText;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "main window is natively presented");
                Require(frame->GetToolBar() && frame->GetToolBar()->IsShown(),
                        "default toolbar is present and visible");
                Require(frame->GetToolBar()->FindById(wxID_NEW)->GetNormalBitmap().IsOk(),
                        "toolbar command displays its original artwork");
                Require(frame->GetStatusBar()->GetFieldsCount() == 13, "status exposes the original thirteen fields");
                using enum Scintilla::Message;
                auto* editor = frame->Editor();
                editor->Send(SetText, 0, reinterpret_cast<intptr_t>("one\r\n\t中😀"));
                editor->Send(SetTabWidth, 4);
                editor->Send(GotoPos, 13);
                NativePaint::Complete(editor);
                auto* status = frame->GetStatusBar();
                Require(status->GetStatusText(0) == "Ln 2 / 2" && status->GetStatusText(1) == "Col 7 / 6" &&
                            status->GetStatusText(2) == "Ch 4 / 3" && status->GetStatusText(3) == "Sel 0 / 0" &&
                            status->GetStatusText(4) == "SelLn 0" && status->GetStatusText(5) == "Fnd 0",
                        "original line, column, scalar, selection and match fields follow tabs and Unicode");
                editor->Send(SetSel, 0, 13);
                NativePaint::Complete(editor);
                Require(status->GetStatusText(3) == "Sel 8 / 13" && status->GetStatusText(4) == "SelLn 2",
                        "original selection reports separate scalar and byte counts and selected lines");
                Require(frame->GetTitle().StartsWith("* "), "original modified title prefix includes its space");
                frame->SetSize(wxSize(1000, 700));
                editor->Send(GotoPos, 0);
                NativePaint::Complete(editor);
                for (int field = 0; field < StatusField::Count; ++field) {
                    if (field == StatusField::Feedback) continue;
                    wxRect rectangle;
                    Require(status->GetFieldRect(field, rectangle) &&
                                rectangle.GetRight() <= status->GetClientSize().x &&
                                rectangle.width >= status->GetTextExtent(status->GetStatusText(field)).x,
                            "every original field is readable within the captured normal window geometry");
#ifdef __WXMSW__
                    std::array<wchar_t, 512> rendered{};
                    SendMessageW(static_cast<HWND>(status->GetHandle()), SB_GETTEXTW, field,
                                 reinterpret_cast<LPARAM>(rendered.data()));
                    Require(wxString(rendered.data()) == status->GetStatusText(field),
                            "native status text is fully rendered without ellipsis at normal geometry");
#endif
                }
#ifdef __WXMSW__
                Require(frame->GetToolBar()->GetToolBitmapSize() == frame->FromDIP(wxSize(16, 16)),
                        "native toolbar uses the original physical bitmap resolution at fractional DPI");
#endif
                auto* toolbar = frame->GetToolBar();
                editor->Send(EmptyUndoBuffer);
                editor->Send(SetSavePoint);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!toolbar->GetToolEnabled(wxID_UNDO) && !toolbar->GetToolEnabled(wxID_REDO) &&
                            !frame->GetMenuBar()->IsEnabled(wxID_UNDO) && !frame->GetMenuBar()->IsEnabled(wxID_REDO),
                        "clean native toolbar Undo and Redo share the menu's disabled state");
#ifdef __WXMSW__
                Require(!SendMessageW(static_cast<HWND>(toolbar->GetHandle()), TB_ISBUTTONENABLED, wxID_UNDO, 0) &&
                            !SendMessageW(static_cast<HWND>(toolbar->GetHandle()), TB_ISBUTTONENABLED, wxID_REDO, 0),
                        "native toolbar buttons are actually disabled for no-edit history");
#endif
                Require(toolbar->FindById(wxID_UNDO)->GetDisabledBitmap().IsOk(),
                        "disabled history commands use the original blended artwork");
                editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(toolbar->GetToolEnabled(wxID_UNDO), "editing enables the same toolbar Undo command");
                wxCommandEvent undo(wxEVT_TOOL, wxID_UNDO);
                undo.SetEventObject(toolbar);
                toolbar->GetEventHandler()->ProcessEvent(undo);
                Require(!editor->Send(GetModify) && editor->Send(CanRedo),
                        "toolbar activation executes the real Undo and retains Redo/save point");
                wxRect overtype;
                Require(status->GetFieldRect(StatusField::Overtype, overtype), "native overtype field has a rectangle");
                wxMouseEvent mode(wxEVT_LEFT_DCLICK);
                mode.SetPosition(overtype.GetPosition() + wxPoint(overtype.width / 2, overtype.height / 2));
                mode.SetEventObject(status);
                const auto oldMode = editor->Send(GetOvertype);
                status->GetEventHandler()->ProcessEvent(mode);
                Require(editor->Send(GetOvertype) != oldMode, "status overtype action changes the real editor mode");
                status->GetEventHandler()->ProcessEvent(mode);
                Require(editor->Send(GetOvertype) == oldMode && !editor->Send(GetModify) && editor->Send(CanRedo),
                        "status mode actions preserve document bytes, history and save point");
                bool encodingDialog = false;
                std::exception_ptr encodingFailure;
                CallAfter([frame, &encodingDialog, &encodingFailure] {
                    try {
                        for (auto* child : frame->GetChildren()) {
                            auto* dialog = wxDynamicCast(child, wxDialog);
                            if (!dialog || dialog->GetTitle() != "Select Encoding" || !dialog->IsModal()) continue;
                            encodingDialog = true;
                            NativeSearch::Click(dialog, wxID_CANCEL);
                            return;
                        }
                        throw std::runtime_error("status encoding action opens the shared native encoding dialog");
                    } catch (...) {
                        encodingFailure = std::current_exception();
                    }
                });
                wxRect encodingField;
                Require(status->GetFieldRect(StatusField::Encoding, encodingField), "native encoding field has bounds");
                wxMouseEvent chooseEncoding(wxEVT_LEFT_DCLICK);
                chooseEncoding.SetPosition(encodingField.GetPosition() +
                                           wxPoint(encodingField.width / 2, encodingField.height / 2));
                chooseEncoding.SetEventObject(status);
                status->GetEventHandler()->ProcessEvent(chooseEncoding);
                if (encodingFailure) std::rethrow_exception(encodingFailure);
                Require(encodingDialog && frame->DocumentEncoding() == FileEncoding::Utf8 && !editor->Send(GetModify) &&
                            editor->Send(CanRedo),
                        "cancelling status encoding action preserves current encoding and history");
                Require(toolbar->FindById(wxID_REPLACE), "default toolbar includes the delivered Replace command");
                for (size_t index = 0; index < toolbar->GetToolsCount(); ++index) {
                    auto* tool = toolbar->GetToolByPos(static_cast<int>(index));
                    if (tool->IsSeparator()) continue;
                    Require(frame->GetMenuBar()->FindItem(tool->GetId()) && !tool->GetShortHelp().empty(),
                            "each toolbar tool owns an existing real menu command and tooltip");
                    for (int size : {16, 24, 32, 40, 48})
                        Require(tool->GetNormalBitmapBundle().GetBitmap(wxSize(size, size)).IsOk(),
                                "original artwork is available at every supplied native resolution");
                }
                const auto text = NativeSearch::Text(editor);
                const auto encoding = frame->DocumentEncoding();
                const auto dirty = frame->DocumentIsModified();
                NativeKeys::Shortcut(frame, WXK_F11, true);
                Require(!toolbar->IsShown(), "Ctrl F11 hides toolbar alone");
                NativeKeys::Shortcut(frame, WXK_F11, true);
                Require(toolbar->IsShown() && editor->HasFocus(),
                        "Ctrl F11 restores toolbar without taking editor focus");
                Require(NativeSearch::Text(editor) == text && frame->DocumentEncoding() == encoding &&
                            frame->DocumentIsModified() == dirty,
                        "toolbar visibility is observational for document bytes, encoding and save point");
                editor->Send(SetSelectionMode,
                             static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Rectangle)));
                editor->Send(SetRectangularSelectionAnchor, 1);
                editor->Send(SetRectangularSelectionCaret, 9);
                NativePaint::Complete(editor);
                Require(status->GetStatusText(3) == "Sel -- / --",
                        "rectangular selection preserves original unknown counts");
                editor->Send(SetSelectionMode,
                             static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Stream)));
                editor->Send(ClearSelections);
                editor->Send(SetSelection, 1, 0);
                editor->Send(AddSelection, 9, 6);
                NativePaint::Complete(editor);
                Require(status->GetStatusText(3) == "Sel 1 / 6", "multiple selection combines selected bytes and CRLF "
                                                                 "separator with original main-range scalar count");
                result_ = 0;
                std::puts("PASS: native default toolbar");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
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
    int OnExit() override { return result_; }

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(AppearanceApp);
