#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "toolbar_art_reference.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include "editor/occurrence_marking.h"
#include "editor/editor_ids.h"
#include <wx/toolbar.h>
#include <wx/statusbr.h>
#include <memory>
#include <wx/log.h>
#include <wx/dialog.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/settings.h>
#ifdef __WXMSW__
#include <commctrl.h>
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void CheckApplicationIcons(const EditorFrame* frame) {
    const auto& icons = frame->GetIcons();
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 256}) {
        const auto icon = icons.GetIconOfExactSize(size);
        Require(icon.IsOk() && icon.GetSize() == wxSize(size, size),
                "application icon retains every original size without fallback scaling");
    }
}
void OriginalToolbarAlpha(wxToolBar* toolbar) {
    using namespace ToolbarArtReference;
    const auto icon = toolbar->FindById(wxID_NEW)
                          ->GetNormalBitmapBundle()
                          .GetBitmap(wxSize(IconSizePixels, IconSizePixels))
                          .ConvertToImage();
    Require(icon.HasAlpha() && icon.GetSize() == wxSize(IconSizePixels, IconSizePixels),
            "original 150-percent toolbar artwork retains its alpha channel and physical size");
    Require(std::equal(NewIconAlpha.begin(), NewIconAlpha.end(), icon.GetAlpha()),
            "toolbar preserves every original anti-aliased edge alpha value instead of a binary mask");
}
#if defined(__WXGTK__) || defined(__WXMSW__)
void AwaitPaste(wxToolBar* toolbar, bool enabled) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    do {
        toolbar->UpdateWindowUI();
        wxYield();
        if (toolbar->GetToolEnabled(wxID_PASTE) == enabled) return;
        wxMilliSleep(10);
    } while (std::chrono::steady_clock::now() < deadline);
    Require(false, "native toolbar Paste follows asynchronous clipboard offers and document protection");
}

void ClipboardOffer(std::unique_ptr<wxDataObject> data) {
    wxClipboardLocker lock;
    if (!lock) throw std::runtime_error("private clipboard opens for a native toolbar offer");
    wxTheClipboard->Clear();
    if (!data) return;
#ifdef __WXGTK__
    // GTK retains the object before publishing the native offer, including a
    // publication failure. Keep local ownership until the lock is acquired.
    Require(wxTheClipboard->SetData(data.release()), "private clipboard accepts the offered native data");
#else
    // MSW transfers ownership only after OleSetClipboard succeeds.
    Require(wxTheClipboard->SetData(data.get()), "private clipboard accepts the offered native data");
    data.release();
#endif
}
#endif
void NativeSelectionColours(EditorControl* editor) {
    const auto foreground = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT);
    const auto background = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
    for (const auto [element, expected] : {std::pair{Scintilla::Element::SelectionText, foreground},
                                           std::pair{Scintilla::Element::SelectionBack, background}}) {
        const auto colour =
            static_cast<uint32_t>(editor->Send(Scintilla::Message::GetElementColour, static_cast<uintptr_t>(element)));
        const uint32_t native = 0xFF000000U | expected.Red() | (expected.Green() << 8U) | (expected.Blue() << 16U);
        std::printf("Selection element %d: expected=%08x actual=%08x\n", static_cast<int>(element), native, colour);
        Require(colour == native, "default selected text uses the native highlight foreground/background pair");
    }
}
void NativeFoldMargin(EditorControl* editor) {
    using enum Scintilla::Message;
    int left = 0;
    for (int margin = 0; margin < EditorMargin::Folding; ++margin)
        left += static_cast<int>(editor->Send(GetMarginWidthN, margin));
    const auto width = editor->Send(GetMarginWidthN, EditorMargin::Folding);
    Require(width >= 2, "default folding margin is visible");
    // wxSTC's native chrome pattern is used without fold-color overrides.
    const auto chrome = wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE);
    const auto highlight = wxSystemSettings::GetColour(wxSYS_COLOUR_3DHIGHLIGHT);
    const auto fill = highlight == *wxWHITE ? chrome : highlight;
    const auto pixels =
        NativePaint::Read(editor, wxRect(left + static_cast<int>(width) / 2, editor->GetClientSize().y / 2, 2, 2));
    Require(std::count(pixels.begin(), pixels.end(), fill) >= 2 &&
                std::count(pixels.begin(), pixels.end(), highlight) >= 2,
            "painted folding margin follows the native wxSTC chrome colors");
}
void CheckDisabledArtwork(const wxToolBarToolBase* tool, int command, int size, const wxColour& background) {
    const auto normal = tool->GetNormalBitmapBundle().GetBitmap(wxSize(size, size)).ConvertToImage();
    const auto luminance = normal.ConvertToGreyscale();
    const auto disabled = tool->GetDisabledBitmapBundle().GetBitmap(wxSize(size, size)).ConvertToImage();
    int visiblePixels = 0;
    unsigned darkest = 255;
    unsigned lightest = 0;
    wxPoint darkPoint;
    wxPoint lightPoint;
    for (int y = 0; y < disabled.GetHeight(); ++y) {
        for (int x = 0; x < disabled.GetWidth(); ++x) {
            Require(normal.GetAlpha(x, y) == disabled.GetAlpha(x, y),
                    "disabled Dark artwork preserves the supplied shape and edge transparency");
            if (disabled.GetAlpha(x, y) != wxALPHA_OPAQUE) continue;
            ++visiblePixels;
            const unsigned shade = luminance.GetRed(x, y);
            if (shade < darkest) {
                darkest = shade;
                darkPoint = wxPoint(x, y);
            }
            if (shade > lightest) {
                lightest = shade;
                lightPoint = wxPoint(x, y);
            }
        }
    }
    Require(visiblePixels > 0, "disabled Dark artwork contains visible native foreground strokes");
    const auto pixel = [&disabled](wxPoint point) {
        return wxColour(disabled.GetRed(point.x, point.y), disabled.GetGreen(point.x, point.y),
                        disabled.GetBlue(point.x, point.y));
    };
    if (command == wxID_SAVE || command == wxID_PASTE) {
        Require(darkest < lightest, "supplied Save/Paste artwork has opaque interior luminance detail");
        Require(pixel(darkPoint).GetLuminance() > pixel(lightPoint).GetLuminance(),
                "disabled Dark Save/Paste preserve opaque interior detail instead of a solid silhouette");
    }
    Require(pixel(darkPoint).GetLuminance() > background.GetLuminance(),
            "disabled Dark artwork retains visible foreground contrast against native chrome");
}

void DisabledArtworkColours(wxToolBar* toolbar) {
    if (!wxSystemSettings::GetAppearance().IsDark()) return;
    const auto foreground = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    const auto background = wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE);
    Require(foreground != background, "native Dark theme exposes a distinct disabled foreground");
    for (const int command : {wxID_SAVE, wxID_PASTE, wxID_UNDO, wxID_REDO, wxID_CUT}) {
        const auto* tool = toolbar->FindById(command);
        for (const int size : {16, 24, 32, 40, 48}) {
            CheckDisabledArtwork(tool, command, size, background);
        }
    }
    std::printf("PASS: native Dark disabled artwork foreground #%02x%02x%02x on #%02x%02x%02x\n", foreground.Red(),
                foreground.Green(), foreground.Blue(), background.Red(), background.Green(), background.Blue());
}
#ifdef __WXMSW__
void DumpStatusLayout(wxStatusBar* status, const char* stage) {
    const auto window = static_cast<HWND>(status->GetHandle());
    const auto client = status->GetClientSize();
    std::fprintf(stderr, "Status %s: client=%dx%d DPI=%dx%d font=%s char=%dx%d grip=%d\n", stage, client.x, client.y,
                 status->GetDPI().x, status->GetDPI().y, status->GetFont().GetNativeFontInfoDesc().ToUTF8().data(),
                 status->GetCharWidth(), status->GetCharHeight(),
                 wxSystemSettings::GetMetric(wxSYS_HSCROLL_ARROW_X, status));
    for (int field = 0; field < StatusField::Count; ++field) {
        wxRect rectangle;
        const bool valid = status->GetFieldRect(field, rectangle);
        std::array<wchar_t, 512> rendered{};
        SendMessageW(window, SB_GETTEXTW, field, reinterpret_cast<LPARAM>(rendered.data()));
        std::fprintf(stderr,
                     "Status field %d: width=%d rect=%d,%d,%d,%d valid=%d textWidth=%d logical='%s' native='%s'\n",
                     field, status->GetStatusWidth(field), rectangle.x, rectangle.y, rectangle.width, rectangle.height,
                     valid, status->GetTextExtent(status->GetStatusText(field)).x,
                     status->GetStatusText(field).ToUTF8().data(), wxString(rendered.data()).ToUTF8().data());
    }
}
#endif
void CheckDocumentStatus(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    NativeSelectionColours(editor);
    NativeFoldMargin(editor);
    for (const auto [text, size] : {std::pair{"", "0 bytes"}, std::pair{"a", "1 byte"}, std::pair{"ab", "2 bytes"}}) {
        editor->Send(SetText, 0, reinterpret_cast<intptr_t>(text));
        NativePaint::Complete(editor);
        Require(frame->GetStatusBar()->GetStatusText(StatusField::DocumentSize) == size,
                "document byte count uses singular only for one byte");
    }
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
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Lines)));
    NativePaint::Complete(editor);
    Require(status->GetStatusText(3) == "Sel 8 / 13" && status->GetStatusText(4) == "SelLn 2",
            "line selection reports original nonrectangular Unicode character and byte counts");
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Stream)));
    Require(frame->GetTitle().StartsWith("* "), "original modified title prefix includes its space");
    frame->SetSize(wxSize(1000, 700));
    editor->Send(GotoPos, 0);
    NativePaint::Complete(editor);
    for (int field = 0; field < StatusField::Count; ++field) {
        if (field == StatusField::Feedback) continue;
        wxRect rectangle;
        Require(status->GetFieldRect(field, rectangle) && rectangle.GetRight() <= status->GetClientSize().x &&
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
#ifdef __WXGTK__
    status->Update();
    Require(!status->GetField(StatusField::DocumentSize).IsEllipsized(),
            "native status painting displays the complete size beside its sizing grip");
#endif
}

void CheckToolbarClipboardAndHistory(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    auto* toolbar = frame->GetToolBar();
    DisabledArtworkColours(toolbar);
#if defined(__WXGTK__) || defined(__WXMSW__)
    ClipboardOffer(std::make_unique<wxTextDataObject>("native toolbar text"));
    AwaitPaste(toolbar, true);
    editor->Send(SetReadOnly, 1);
    AwaitPaste(toolbar, false);
    editor->Send(SetReadOnly, 0);
    AwaitPaste(toolbar, true);
    auto custom = std::make_unique<wxCustomDataObject>(wxDataFormat("application/x-wxnotepad-appearance"));
    custom->SetData(3, "art");
    ClipboardOffer(std::move(custom));
    AwaitPaste(toolbar, false);
    ClipboardOffer(std::make_unique<wxTextDataObject>("second native text offer"));
    AwaitPaste(toolbar, true);
    ClipboardOffer(nullptr);
    AwaitPaste(toolbar, false);
#endif
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
            "disabled history commands have native themed artwork");
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(toolbar->GetToolEnabled(wxID_UNDO), "editing enables the same toolbar Undo command");
    wxCommandEvent undo(wxEVT_TOOL, wxID_UNDO);
    undo.SetEventObject(toolbar);
    toolbar->GetEventHandler()->ProcessEvent(undo);
    Require(!editor->Send(GetModify) && editor->Send(CanRedo),
            "toolbar activation executes the real Undo and retains Redo/save point");
}

void CheckStatusActions(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    auto* status = frame->GetStatusBar();
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
    wxTheApp->CallAfter([frame, &encodingDialog, &encodingFailure] {
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
}

void CheckToolbarLayout(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    auto* toolbar = frame->GetToolBar();
    Require(toolbar->FindById(wxID_REPLACE), "default toolbar includes the delivered Replace command");
    const auto command = [frame](const char* menu, const char* label) {
        return frame->GetMenuBar()->FindMenuItem(menu, label);
    };
    // Frozen from the pinned default toolbar order, with commands
    // without a delivered host menu action omitted.
    const auto expectedOrder = std::to_array<int>({wxID_NEW,
                                                   command("File", "New Window"),
                                                   wxID_OPEN,
                                                   wxID_SEPARATOR,
                                                   wxID_SAVE,
                                                   wxID_SAVEAS,
                                                   command("File", "Save Copy..."),
                                                   wxID_SEPARATOR,
                                                   wxID_UNDO,
                                                   wxID_REDO,
                                                   wxID_SEPARATOR,
                                                   wxID_CUT,
                                                   wxID_COPY,
                                                   wxID_PASTE,
                                                   wxID_SEPARATOR,
                                                   wxID_FIND,
                                                   wxID_REPLACE,
                                                   wxID_SEPARATOR,
                                                   command("View", "Word Wrap"),
                                                   wxID_SEPARATOR,
                                                   command("View", "Default Levels"),
                                                   wxID_SEPARATOR,
                                                   command("View", "Zoom In"),
                                                   command("View", "Zoom Out")});
    Require(toolbar->GetToolsCount() == expectedOrder.size(), "default toolbar retains original delivered groups");
    for (size_t index = 0; index < expectedOrder.size(); ++index) {
        const auto* tool = toolbar->GetToolByPos(static_cast<int>(index));
        Require(expectedOrder[index] != wxNOT_FOUND &&
                    (tool->IsSeparator() ? wxID_SEPARATOR : tool->GetId()) == expectedOrder[index],
                "native toolbar order and separators match the delivered original commands");
    }
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
    Require(toolbar->IsShown() && editor->HasFocus(), "Ctrl F11 restores toolbar without taking editor focus");
    Require(NativeSearch::Text(editor) == text && frame->DocumentEncoding() == encoding &&
                frame->DocumentIsModified() == dirty,
            "toolbar visibility is observational for document bytes, encoding and save point");
}

void CheckMultipleSelectionStatus(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    auto* status = frame->GetStatusBar();
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Rectangle)));
    editor->Send(SetRectangularSelectionAnchor, 1);
    editor->Send(SetRectangularSelectionCaret, 9);
    NativePaint::Complete(editor);
    Require(status->GetStatusText(3) == "Sel -- / --", "rectangular selection preserves original unknown counts");
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Stream)));
    editor->Send(ClearSelections);
    editor->Send(SetSelection, 1, 0);
    editor->Send(AddSelection, 9, 6);
    NativePaint::Complete(editor);
    Require(status->GetStatusText(3) == "Sel 1 / 6", "multiple selection combines selected bytes and CRLF "
                                                     "separator with original main-range scalar count");
    editor->Send(SetCopySeparator, 0, reinterpret_cast<intptr_t>(" / "));
    editor->Send(SetSelectionNCaret, 1, 13);
    NativePaint::Complete(editor);
    Require(status->GetStatusText(3) == "Sel 2 / 11",
            "multiple selection byte count follows the configured copy separator");
    editor->Send(SetCopySeparator, 0, reinterpret_cast<intptr_t>(""));
}

#ifdef __WXMSW__
void CheckNarrowStatus(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    auto* status = frame->GetStatusBar();
    frame->SetSize(wxSize(620, 430));
    NativePaint::Complete(editor);
    if (status->GetStatusWidth(StatusField::Scheme) != 0) {
        // At smaller native fonts 620 pixels can display all metadata.
        // Verify this observed state before forcing the hidden state.
        for (int field = StatusField::Scheme; field <= StatusField::Zoom; ++field) {
            wxRect rectangle;
            std::array<wchar_t, 512> rendered{};
            SendMessageW(static_cast<HWND>(status->GetHandle()), SB_GETTEXTW, field,
                         reinterpret_cast<LPARAM>(rendered.data()));
            Require(status->GetFieldRect(field, rectangle) && rectangle.GetRight() <= status->GetClientSize().x &&
                        rectangle.width >= status->GetTextExtent(status->GetStatusText(field)).x &&
                        wxString(rendered.data()) == status->GetStatusText(field),
                    "metadata that fits at 620 pixels retains every complete native caption");
        }
        wxRect firstMetadata;
        wxRect documentSize;
        Require(status->GetFieldRect(StatusField::Scheme, firstMetadata) &&
                    status->GetFieldRect(StatusField::DocumentSize, documentSize),
                "native rectangles identify the counters and final size reservation");
        const int grip = wxSystemSettings::GetMetric(wxSYS_HSCROLL_ARROW_X, status);
        const int narrowClientWidth = firstMetadata.x + documentSize.width + grip;
        Require(narrowClientWidth < status->GetClientSize().x,
                "measured narrow geometry removes the metadata reservation");
        frame->SetClientSize(wxSize(narrowClientWidth, frame->GetClientSize().y));
        NativePaint::Complete(editor);
    }
    Require(status->GetStatusWidth(StatusField::Scheme) == 0,
            "the narrow native fixture reaches the metadata-hidden state");
    for (int field = StatusField::Scheme; field <= StatusField::Zoom; ++field) {
        std::array<wchar_t, 512> rendered{};
        SendMessageW(static_cast<HWND>(status->GetHandle()), SB_GETTEXTW, field,
                     reinterpret_cast<LPARAM>(rendered.data()));
        if (rendered[0] != 0) DumpStatusLayout(status, "narrow metadata caption failure");
        Require(rendered[0] == 0, "narrow native panes do not paint fragments of hidden metadata");
    }
    frame->SetSize(wxSize(1000, 700));
    editor->Send(GotoPos, 0);
    NativePaint::Complete(editor);
    std::array<wchar_t, 512> restored{};
    SendMessageW(static_cast<HWND>(status->GetHandle()), SB_GETTEXTW, StatusField::Encoding,
                 reinterpret_cast<LPARAM>(restored.data()));
    Require(wxString(restored.data()) == "UTF-8", "normal resize restores the native encoding text");
}

#endif
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
                CheckApplicationIcons(frame);
                Require(frame->GetToolBar() && frame->GetToolBar()->IsShown(),
                        "default toolbar is present and visible");
                Require(frame->GetToolBar()->FindById(wxID_NEW)->GetNormalBitmap().IsOk(),
                        "toolbar command displays its original artwork");
                Require(frame->GetStatusBar()->GetFieldsCount() == 13, "status exposes the original thirteen fields");
                OriginalToolbarAlpha(frame->GetToolBar());
                CheckDocumentStatus(frame);
                CheckToolbarClipboardAndHistory(frame);
                CheckStatusActions(frame);
                CheckToolbarLayout(frame);
                CheckMultipleSelectionStatus(frame);
#ifdef __WXMSW__
                CheckNarrowStatus(frame);
#endif
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
